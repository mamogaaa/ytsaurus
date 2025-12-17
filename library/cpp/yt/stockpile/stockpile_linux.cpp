#include "stockpile.h"

#include <thread>
#include <mutex>
#include <atomic>
#include <optional>

#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <util/system/thread.h>
#include <util/string/split.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

namespace {

constexpr int MADV_STOCKPILE = 0x59410004;

enum class EStockpileStrategy
{
    MadviseStockpile,
    MemoryReclaim,
    None,
};

std::atomic<EStockpileStrategy> StockpileStrategy{EStockpileStrategy::MadviseStockpile};
std::optional<TString> MemoryReclaimPath;

bool IsMadviseStockpileSupported()
{
    // Try once with minimal size to check if syscall is supported.
    int result = ::madvise(nullptr, 1, MADV_STOCKPILE);
    return !(result == -1 && errno == EINVAL);
}

std::optional<TString> FindMemoryReclaimPath()
{
    // Read /proc/self/cgroup to find our cgroup path.
    // Format for cgroup v2: "0::/path/to/cgroup"
    int fd = ::open("/proc/self/cgroup", O_RDONLY);
    if (fd == -1) {
        return std::nullopt;
    }

    char buf[4096];
    ssize_t bytesRead = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);

    if (bytesRead <= 0) {
        return std::nullopt;
    }
    buf[bytesRead] = '\0';

    // Look for cgroup v2 line: "0::/some/path"
    TString content(buf);
    for (TStringBuf line : StringSplitter(content).Split('\n')) {
        if (line.StartsWith("0::/")) {
            TString cgroupPath(line.SubStr(3)); // Skip "0::"
            // Trim trailing whitespace/newline
            while (!cgroupPath.empty() && (cgroupPath.back() == '\n' || cgroupPath.back() == '\r' || cgroupPath.back() == ' ')) {
                cgroupPath.pop_back();
            }
            TString reclaimPath = "/sys/fs/cgroup" + cgroupPath + "/memory.reclaim";
            // Check if file exists and is writable
            if (::access(reclaimPath.c_str(), W_OK) == 0) {
                return reclaimPath;
            }
            // Also try root cgroup
            if (::access("/sys/fs/cgroup/memory.reclaim", W_OK) == 0) {
                return TString("/sys/fs/cgroup/memory.reclaim");
            }
        }
    }

    return std::nullopt;
}

void DoStockpileMadvise(i64 bufferSize)
{
    ::madvise(nullptr, bufferSize, MADV_STOCKPILE);
}

void DoStockpileMemoryReclaim(i64 bufferSize)
{
    if (!MemoryReclaimPath) {
        return;
    }

    int fd = ::open(MemoryReclaimPath->c_str(), O_WRONLY);
    if (fd == -1) {
        return;
    }

    // Write number of bytes to reclaim
    char buf[32];
    int len = ::snprintf(buf, sizeof(buf), "%ld", static_cast<long>(bufferSize));
    // Ignore errors - best effort
    [[maybe_unused]] auto result = ::write(fd, buf, len);
    ::close(fd);
}

void RunStockpile(const TStockpileOptions& options)
{
    TThread::SetCurrentThreadName("Stockpile");

    while (true) {
        switch (StockpileStrategy.load(std::memory_order_relaxed)) {
            case EStockpileStrategy::MadviseStockpile:
                DoStockpileMadvise(options.BufferSize);
                break;
            case EStockpileStrategy::MemoryReclaim:
                DoStockpileMemoryReclaim(options.BufferSize);
                break;
            case EStockpileStrategy::None:
                // Nothing to do, but keep thread alive in case strategy changes
                break;
        }
        Sleep(options.Period);
    }
}

EStockpileStrategy DetectStockpileStrategy()
{
    // First, try Yandex kernel's MADV_STOCKPILE
    if (IsMadviseStockpileSupported()) {
        return EStockpileStrategy::MadviseStockpile;
    }

    // Fallback: try cgroup v2 memory.reclaim (Linux 5.19+)
    MemoryReclaimPath = FindMemoryReclaimPath();
    if (MemoryReclaimPath) {
        return EStockpileStrategy::MemoryReclaim;
    }

    // No supported mechanism available
    return EStockpileStrategy::None;
}

} // namespace

void ConfigureStockpile(const TStockpileOptions& options)
{
    static std::once_flag OnceFlag;
    std::call_once(OnceFlag, [options] {
        StockpileStrategy.store(DetectStockpileStrategy(), std::memory_order_relaxed);

        if (StockpileStrategy.load() == EStockpileStrategy::None) {
            // No stockpile mechanism available, don't start threads
            return;
        }

        for (int i = 0; i < options.ThreadCount; i++) {
            std::thread(RunStockpile, options).detach();
        }
    });
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
