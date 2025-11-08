# YTsaurus Changelog & Snapshot Format - Полная Спецификация

## 📋 СОДЕРЖАНИЕ
1. [Changelog Format](#changelog-format)
2. [Changelog Index Format](#changelog-index-format)
3. [Snapshot Format](#snapshot-format)
4. [Валидация](#валидация)
5. [Примеры](#примеры)

---

# CHANGELOG FORMAT

## Константы

```c
PageAlignment = 4096 байт (4 KB)
QWordAlignment = 8 байт
```

**Важно:** Все записи в changelog выравниваются по границе 4KB страниц!

---

## Структура Changelog файла

```
┌─────────────────────────────────────────────────────────────────┐
│ TChangelogHeader (40 байт)                                      │
├─────────────────────────────────────────────────────────────────┤
│ Metadata (TChangelogMeta protobuf, размер = MetaSize)          │
├─────────────────────────────────────────────────────────────────┤
│ Padding (размер = PaddingSize, выравнивание до 4KB)            │
├─────────────────────────────────────────────────────────────────┤
│ Record 0:                                                       │
│   - TChangelogRecordHeader (40 байт)                           │
│   - Payload (PayloadSize байт)                                 │
│   - Page Padding (PagePaddingSize байт, выравн. до 4KB)       │
├─────────────────────────────────────────────────────────────────┤
│ Record 1:                                                       │
│   - TChangelogRecordHeader (40 байт)                           │
│   - Payload (PayloadSize байт)                                 │
│   - Page Padding (PagePaddingSize байт)                        │
├─────────────────────────────────────────────────────────────────┤
│ ...                                                             │
└─────────────────────────────────────────────────────────────────┘
```

---

## TChangelogHeader (40 байт, offset 0)

**Размер:** Ровно 40 байт
**Выравнивание:** Начинается с offset 0
**Структура:**

```
Offset | Размер | Тип    | Имя                | Описание
-------|--------|--------|--------------------|----------------------------------
0      | 8      | ui64   | Signature          | 0x3530303044435459 (YTCD0005)
8      | 4      | i32    | FirstRecordOffset  | Offset первой записи в файле
12     | 4      | i32    | MetaSize           | Размер metadata (protobuf)
16     | 4      | i32    | UnusedMustBeMinus2 | Должно быть -2 (0xFFFFFFFE)
20     | 4      | i32    | PaddingSize        | Размер padding после metadata
24     | 16     | TGuid  | Uuid               | UUID changelog'а
```

### TGuid (16 байт)

```
Offset | Размер | Тип  | Описание
-------|--------|------|------------------
0      | 8      | ui64 | Parts[0] (low)
8      | 8      | ui64 | Parts[1] (high)
```

**Пример UUID в hex:**
```
24-31: 01 23 45 67 89 AB CD EF  (Parts[0])
32-39: FE DC BA 98 76 54 32 10  (Parts[1])
```

### Валидация TChangelogHeader:

```python
def validate_changelog_header(data):
    assert len(data) >= 40

    # 1. Signature
    signature = struct.unpack('<Q', data[0:8])[0]
    assert signature == 0x3530303044435459, "Invalid signature"

    # 2. FirstRecordOffset должен быть >= 40 и кратен PageAlignment
    first_record = struct.unpack('<i', data[8:12])[0]
    assert first_record >= 40
    assert first_record % 4096 == 0, "FirstRecordOffset not page-aligned"

    # 3. UnusedMustBeMinus2
    unused = struct.unpack('<i', data[16:20])[0]
    assert unused == -2, f"UnusedMustBeMinus2 = {unused}, expected -2"

    # 4. MetaSize + PaddingSize должны дополнять до FirstRecordOffset
    meta_size = struct.unpack('<i', data[12:16])[0]
    padding_size = struct.unpack('<i', data[20:24])[0]
    assert 40 + meta_size + padding_size == first_record

    return True
```

---

## Metadata (Protobuf TChangelogMeta)

**Offset:** 40
**Размер:** MetaSize (из заголовка)

**Структура protobuf:**
```protobuf
message TChangelogMeta {
    // Обычно пустое или содержит служебную информацию
}
```

**В большинстве случаев MetaSize = 0** (пустой protobuf).

---

## Padding

**Offset:** 40 + MetaSize
**Размер:** PaddingSize
**Заполнение:** Обычно нули

**Назначение:** Выравнивание до границы страницы (4KB).

```
FirstRecordOffset = 40 + MetaSize + PaddingSize
FirstRecordOffset % 4096 == 0
```

**Пример:**
```
Если MetaSize = 0:
  40 + 0 + PaddingSize = 4096
  PaddingSize = 4056
```

---

## TChangelogRecordHeader (40 байт)

**Размер:** Ровно 40 байт
**Для каждой записи:**

```
Offset | Размер | Тип       | Имя              | Описание
-------|--------|-----------|------------------|------------------------------
0      | 4      | i32       | RecordIndex      | Индекс записи (0, 1, 2, ...)
4      | 4      | i32       | PayloadSize      | Размер payload (мутация)
8      | 8      | TChecksum | Checksum         | Checksum payload (ui64)
16     | 4      | i32       | PagePaddingSize  | Padding до границы страницы
20     | 16     | TGuid     | ChangelogUuid    | UUID changelog'а (дубликат)
36     | 4      | ui32      | Padding          | Выравнивание (обычно 0)
```

**Важно:**
- RecordIndex должен совпадать с номером записи (0-based)
- ChangelogUuid должен совпадать с UUID из TChangelogHeader
- PagePaddingSize рассчитывается для выравнивания всей записи по 4KB

---

## Payload (мутация)

**Offset:** RecordHeaderOffset + 40
**Размер:** PayloadSize (из TChangelogRecordHeader)

### Структура Payload:

```
┌────────────────────────────────────────────┐
│ TFixedMutationHeader (8 байт)              │
│   - i32 HeaderSize                         │
│   - i32 DataSize                           │
├────────────────────────────────────────────┤
│ TMutationHeader (protobuf, HeaderSize)     │
├────────────────────────────────────────────┤
│ Mutation Data (protobuf, DataSize)         │
└────────────────────────────────────────────┘
```

### TFixedMutationHeader (8 байт):

```
Offset | Размер | Тип | Имя        | Описание
-------|--------|-----|------------|----------------------------------
0      | 4      | i32 | HeaderSize | Размер TMutationHeader (protobuf)
4      | 4      | i32 | DataSize   | Размер данных мутации (protobuf)
```

**Проверка:**
```
PayloadSize = 8 + HeaderSize + DataSize
```

### TMutationHeader (protobuf):

**Важные поля:**
```protobuf
message TMutationHeader {
    required string mutation_type = 1;     // Тип мутации
    required uint64 timestamp = 2;         // Timestamp
    required uint64 random_seed = 3;       // Random seed
    required int32 segment_id = 5;         // ⭐ SEGMENT ID (Changelog ID)
    required int32 record_id = 6;          // Record ID внутри segment
    optional TGuid mutation_id = 7;        // GUID мутации
    optional int32 reign = 8 [default=0];  // Reign
    required uint64 prev_random_seed = 9;  // Предыдущий random seed
    required int64 sequence_number = 10;   // ⭐ Sequence number
    optional int32 term = 11 [default=0];  // Term
}
```

**⭐ Критические поля для восстановления:**

1. **segment_id** (field 5) - ID этого changelog'а (124, 125, 126...)
2. **sequence_number** (field 10) - Глобальный номер мутации
3. **mutation_type** (field 1) - Тип операции (например, "NYT.NCypressServer.NProto.TReqCreateNode")

---

## Page Padding

**Offset:** PayloadOffset + PayloadSize
**Размер:** PagePaddingSize (из TChangelogRecordHeader)
**Заполнение:** Нули

**Назначение:** Выравнивание записи до границы 4KB.

```
RecordTotalSize = 40 + PayloadSize + PagePaddingSize
RecordTotalSize % 4096 == 0
```

**Пример:**
```
RecordHeader = 40 байт
Payload = 512 байт
NextPageBoundary = 4096
PagePaddingSize = 4096 - (40 + 512) = 3544 байт
```

---

## Checksum Calculation

**Алгоритм:** FarmHash (Google)

```cpp
// Checksum считается от payload (не включая заголовок записи)
TChecksum checksum = GetChecksum(payload_data);
```

**Валидация:**
```python
import farmhash

def validate_record_checksum(record_header, payload):
    expected = struct.unpack('<Q', record_header[8:16])[0]
    actual = farmhash.hash64(payload)
    assert expected == actual, "Checksum mismatch"
```

---

# CHANGELOG INDEX FORMAT

## Структура Index файла

```
┌─────────────────────────────────────────────────────────────────┐
│ TChangelogIndexHeader (8 байт)                                  │
├─────────────────────────────────────────────────────────────────┤
│ Segment 0:                                                      │
│   - TChangelogIndexSegmentHeader (16 байт)                      │
│   - TChangelogIndexRecord[RecordCount] (по 16 байт каждый)     │
├─────────────────────────────────────────────────────────────────┤
│ Segment 1:                                                      │
│   - TChangelogIndexSegmentHeader (16 байт)                      │
│   - TChangelogIndexRecord[RecordCount] (по 16 байт каждый)     │
├─────────────────────────────────────────────────────────────────┤
│ ...                                                             │
└─────────────────────────────────────────────────────────────────┘
```

---

## TChangelogIndexHeader (8 байт, offset 0)

```
Offset | Размер | Тип  | Имя       | Описание
-------|--------|------|-----------|-------------------------
0      | 8      | ui64 | Signature | 0x3530303049435459 (YTCI0005)
```

**Валидация:**
```python
signature = struct.unpack('<Q', data[0:8])[0]
assert signature == 0x3530303049435459, "Invalid index signature"
```

---

## TChangelogIndexSegmentHeader (16 байт)

**Для каждого сегмента индекса:**

```
Offset | Размер | Тип   | Имя         | Описание
-------|--------|-------|-------------|----------------------------------
0      | 8      | ui64  | Checksum    | Checksum сегмента (без самой checksums)
8      | 4      | i32   | RecordCount | Количество записей в сегменте
12     | 4      | ui32  | Padding     | Выравнивание (обычно 0)
```

**После заголовка следует RecordCount записей TChangelogIndexRecord.**

---

## TChangelogIndexRecord (16 байт)

**Для каждой записи в индексе:**

```
Offset | Размер | Тип | Имя    | Описание
-------|--------|-----|--------|------------------------------------
0      | 8      | i64 | Offset | Offset записи в changelog файле
8      | 8      | i64 | Length | Длина записи (Header + Payload + Padding)
```

**Пример:**
```
Record 0: Offset=4096,  Length=4096   (запись занимает ровно 1 страницу)
Record 1: Offset=8192,  Length=8192   (запись занимает 2 страницы)
Record 2: Offset=16384, Length=4096   (запись занимает 1 страницу)
```

---

## Checksum сегмента индекса

**Checksum считается от данных сегмента, ИСКЛЮЧАЯ саму checksum:**

```
ChecksumData = [RecordCount (4) | Padding (4) | Records (RecordCount * 16)]
Checksum = FarmHash(ChecksumData)
```

**Валидация:**
```python
def validate_index_segment(data, offset):
    # Читаем checksum
    expected_checksum = struct.unpack('<Q', data[offset:offset+8])[0]

    # Читаем RecordCount
    record_count = struct.unpack('<i', data[offset+8:offset+12])[0]

    # Данные для checksum (без самой checksum)
    segment_size = 8 + record_count * 16  # sizeof(ui64) + RecordCount * sizeof(Record)
    checksum_data = data[offset+8:offset+segment_size]

    # Валидация
    actual_checksum = farmhash.hash64(checksum_data)
    assert expected_checksum == actual_checksum, "Index segment checksum mismatch"
```

---

# SNAPSHOT FORMAT

## Структура Snapshot файла

```
┌─────────────────────────────────────────────────────────────────┐
│ TSnapshotHeader (44 байта)                                      │
├─────────────────────────────────────────────────────────────────┤
│ Metadata (protobuf TSnapshotMeta, размер = MetaSize)           │
├─────────────────────────────────────────────────────────────────┤
│ Compressed Snapshot Data (CompressedLength байт)               │
│   Codec указан в заголовке                                      │
│   После декомпрессии размер = UncompressedLength              │
└─────────────────────────────────────────────────────────────────┘
```

---

## TSnapshotHeader (44 байта, offset 0)

```
Offset | Размер | Тип     | Имя                | Описание
-------|--------|---------|--------------------|---------------------------------
0      | 8      | ui64    | Signature          | 0x3330303053535459 (YTSS0003)
8      | 4      | i32     | SnapshotId         | ⭐ SEGMENT ID (123, 124...)
12     | 8      | ui64    | CompressedLength   | Размер сжатых данных
20     | 8      | ui64    | UncompressedLength | Размер после декомпрессии
28     | 8      | ui64    | Checksum           | Checksum несжатых данных
36     | 1      | ECodec  | Codec              | Codec сжатия (enum)
37     | 3      | ui8[3]  | Padding            | Выравнивание
40     | 4      | i32     | MetaSize           | Размер metadata
```

### ECodec значения:

```
0 = None (без сжатия)
1 = Snappy
2 = Lz4
3 = Lz4HighCompression
4 = QuickLz
5 = Brotli (наиболее вероятный для production)
6 = Zstd
...
```

**⭐ SnapshotId - это SEGMENT ID snapshot'а!**

---

## Metadata (Protobuf TSnapshotMeta)

**Offset:** 44
**Размер:** MetaSize

```protobuf
message TSnapshotMeta {
    optional int64 sequence_number = 2;       // Sequence number последней мутации
    optional uint64 random_seed = 3;          // Random seed
    optional uint64 state_hash = 4;           // State hash
    optional uint64 timestamp = 5;            // Timestamp
    optional int32 last_segment_id = 6;       // ⭐ ID последнего changelog
    optional int32 last_record_id = 7;        // Record ID в последнем changelog
    optional int32 last_mutation_term = 8;    // Term последней мутации
    optional bool read_only = 10;             // Read-only режим
}
```

**⭐ Критические поля:**
- **last_segment_id** - последний обработанный changelog (например, 123)
- **sequence_number** - последний sequence number в snapshot'е

---

# ВАЛИДАЦИЯ

## Быстрая проверка типа файла

```python
def identify_file_type(filepath):
    with open(filepath, 'rb') as f:
        sig = f.read(8)

        if len(sig) < 8:
            return None

        signature = struct.unpack('<Q', sig)[0]

        if signature == 0x3530303044435459:
            return 'CHANGELOG'
        elif signature == 0x3330303053535459:
            return 'SNAPSHOT'
        elif signature == 0x3530303049435459:
            return 'CHANGELOG_INDEX'
        else:
            return None
```

---

## Валидация Changelog

```python
def validate_changelog(filepath):
    with open(filepath, 'rb') as f:
        # 1. Заголовок
        header = f.read(40)
        assert len(header) == 40, "Header too short"

        # 2. Signature
        sig = struct.unpack('<Q', header[0:8])[0]
        assert sig == 0x3530303044435459, "Invalid signature"

        # 3. FirstRecordOffset
        first_record = struct.unpack('<i', header[8:12])[0]
        assert first_record >= 40, "FirstRecordOffset too small"
        assert first_record % 4096 == 0, "Not page-aligned"

        # 4. UnusedMustBeMinus2
        unused = struct.unpack('<i', header[16:20])[0]
        assert unused == -2, f"UnusedMustBeMinus2 = {unused}"

        # 5. MetaSize + PaddingSize
        meta_size = struct.unpack('<i', header[12:16])[0]
        padding_size = struct.unpack('<i', header[20:24])[0]
        assert 40 + meta_size + padding_size == first_record

        # 6. UUID (проверяем, что не нулевой)
        uuid_bytes = header[24:40]
        assert uuid_bytes != b'\x00' * 16, "NULL UUID"

        print(f"✅ Valid changelog header")
        print(f"   FirstRecordOffset: {first_record}")
        print(f"   MetaSize: {meta_size}")
        print(f"   PaddingSize: {padding_size}")

        return True
```

---

## Извлечение Segment ID из Changelog

```python
def extract_segment_id(filepath):
    """Извлекает segment_id из первой мутации в changelog"""
    with open(filepath, 'rb') as f:
        # Читаем заголовок
        header = f.read(40)
        first_record_offset = struct.unpack('<i', header[8:12])[0]

        # Переходим к первой записи
        f.seek(first_record_offset)

        # Читаем TChangelogRecordHeader
        record_header = f.read(40)
        payload_size = struct.unpack('<i', record_header[4:8])[0]

        # Читаем payload
        payload = f.read(payload_size)

        # Читаем TFixedMutationHeader
        header_size = struct.unpack('<i', payload[0:4])[0]

        # Парсим protobuf TMutationHeader
        mutation_header = payload[8:8+header_size]

        # Ищем field 5 (segment_id)
        segment_id = parse_protobuf_field(mutation_header, field_number=5)

        return segment_id

def parse_protobuf_field(data, field_number):
    """Упрощенный парсер protobuf для извлечения int32 поля"""
    i = 0
    while i < len(data):
        # Читаем tag
        tag = data[i]
        i += 1

        field = tag >> 3
        wire_type = tag & 0x7

        if field == field_number and wire_type == 0:
            # Читаем varint
            value = 0
            shift = 0
            while i < len(data):
                byte = data[i]
                i += 1
                value |= (byte & 0x7F) << shift
                if (byte & 0x80) == 0:
                    return value
                shift += 7
        else:
            # Пропускаем значение
            if wire_type == 0:  # varint
                while i < len(data) and data[i] & 0x80:
                    i += 1
                i += 1
            elif wire_type == 1:  # 64-bit
                i += 8
            elif wire_type == 2:  # length-delimited
                length = data[i]
                i += 1 + length
            elif wire_type == 5:  # 32-bit
                i += 4

    return None
```

---

# ПРИМЕРЫ

## Пример 1: Hex dump начала Changelog файла

```
Offset    00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F  ASCII
--------  -----------------------------------------------  ----------------
00000000  59 54 43 44 30 30 30 35 00 10 00 00 00 00 00 00  YTCD0005........
                                    ^^^^^^^^^^              FirstRecordOffset = 0x1000 = 4096
00000010  FE FF FF FF D8 0F 00 00 01 23 45 67 89 AB CD EF  .........#Eg....
          ^^^^^^^^^^^                                       UnusedMustBeMinus2 = -2
                      ^^^^^^^^^^^                           PaddingSize = 0x0FD8 = 4056
                                  ^^^^^^^^^^^^^^^^^^^       UUID start
00000020  FE DC BA 98 76 54 32 10 00 00 00 00 00 00 00 00  ....vT2.........
          ^^^^^^^^^^^^^^^^^^^                               UUID end
                                  ^^^^^^^^^^^^^^^^^^^       Padding start

... (4056 байт padding, обычно нули) ...

00001000  00 00 00 00 20 02 00 00 A1 B2 C3 D4 E5 F6 07 08  .... ...........
          ^^^^^^^^^^^                                       RecordIndex = 0
                      ^^^^^^^^^^^                           PayloadSize = 0x220 = 544
                                  ^^^^^^^^^^^^^^^^^^^       Checksum start
```

---

## Пример 2: Hex dump начала Snapshot файла

```
Offset    00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F  ASCII
--------  -----------------------------------------------  ----------------
00000000  59 54 53 53 30 30 30 33 7B 00 00 00 00 20 E5 1E  YTSS0003{.... ..
                                    ^^^^^^^^^^              SnapshotId = 0x7B = 123
                                                ^^^^^^^^^^^  CompressedLength start
00000010  00 00 00 00 00 40 F1 3A 00 00 00 00 11 22 33 44  .....@.:....."3D
                      ^^^^^^^^^^^^^^^^^^^                   UncompressedLength
                                              ^^^^^^^^^^^    Checksum start
00000020  55 66 77 88 06 00 00 00 10 00 00 00 ...          Uf......
          ^^^^^^^^^^^                                       Checksum end
                      ^^                                    Codec = 6 (Zstd)
                         ^^^^^^^                            Padding
                                  ^^^^^^^^^^^               MetaSize = 0x10 = 16
```

---

## Пример 3: Определение повреждения

```python
def diagnose_file(filepath):
    with open(filepath, 'rb') as f:
        data = f.read(1024)

        if len(data) < 8:
            return "TOO_SHORT"

        sig = struct.unpack('<Q', data[0:8])[0]

        if sig == 0x3530303044435459:
            # Changelog
            if len(data) < 40:
                return "CHANGELOG_TRUNCATED_HEADER"

            unused = struct.unpack('<i', data[16:20])[0]
            if unused != -2:
                return f"CHANGELOG_CORRUPTED (UnusedMustBeMinus2={unused})"

            first_record = struct.unpack('<i', data[8:12])[0]
            if first_record % 4096 != 0:
                return f"CHANGELOG_MISALIGNED (FirstRecordOffset={first_record})"

            return "CHANGELOG_OK"

        elif sig == 0x3330303053535459:
            # Snapshot
            if len(data) < 44:
                return "SNAPSHOT_TRUNCATED_HEADER"

            snapshot_id = struct.unpack('<i', data[8:12])[0]
            if snapshot_id < 0 or snapshot_id > 100000:
                return f"SNAPSHOT_INVALID_ID ({snapshot_id})"

            return f"SNAPSHOT_OK (ID={snapshot_id})"

        else:
            return f"UNKNOWN_SIGNATURE (0x{sig:016X})"
```

---

# КРИТИЧЕСКИЕ МОМЕНТЫ ДЛЯ ВОССТАНОВЛЕНИЯ

## 1. Минимальный размер файлов

```
Changelog:       >= 4096 байт (минимум header + padding до первой страницы)
Snapshot:        >= 44 байт (header)
Changelog Index: >= 24 байт (header + 1 сегмент)
```

## 2. Магические константы

```
YTCD0005 = 0x3530303044435459  (Changelog)
YTSS0003 = 0x3330303053535459  (Snapshot)
YTCI0005 = 0x3530303049435459  (Changelog Index)
-2       = 0xFFFFFFFE          (UnusedMustBeMinus2 в Changelog)
```

## 3. Выравнивания

```
Changelog записи:     Кратны 4096 байт
Структуры заголовков: Кратны 8 байт
```

## 4. Типичные размеры файлов

```
Changelog 000124-000128: 10 MB - 500 MB (зависит от активности)
Snapshot 000123:         500 MB - 5 GB (зависит от размера Cypress)
Changelog Index:         100 KB - 10 MB
```

## 5. Идентификация по содержимому

**Если имя файла потеряно, ищите:**

1. **Первые 8 байт = сигнатура**
2. **Для Changelog:** byte[16:20] == 0xFE 0xFF 0xFF 0xFF (= -2)
3. **Для Snapshot:** byte[8:12] = segment_id (обычно 100-150)

---

# СКРИПТ ДЛЯ МАССОВОЙ ИДЕНТИФИКАЦИИ

См. `/tmp/identify_yt_files.py` - полный Python скрипт с:
- Парсингом всех форматов
- Извлечением segment_id
- Валидацией checksums
- Поиском критичных файлов

**Использование:**
```bash
python3 /tmp/identify_yt_files.py /path/to/recovered/files/
```

---

# ЧЕКЛИСТ ВОССТАНОВЛЕНИЯ

```
[ ] 1. Запустить identify_yt_files.py на восстановленных файлах
[ ] 2. Найти все changelog с segment_id 124-128
[ ] 3. Валидировать каждый найденный changelog (проверить header)
[ ] 4. Скопировать с правильными именами: /yt/master/changelogs/000124 и т.д.
[ ] 5. Проверить права доступа (chown yt-master:yt-master)
[ ] 6. Применить патч timestamp_manager.cpp
[ ] 7. Запустить мастер
[ ] 8. Проверить логи восстановления
```

**Если все changelogs найдены и валидны → ПОЛНОЕ ВОССТАНОВЛЕНИЕ БЕЗ ПОТЕРЬ!**

---

END OF SPECIFICATION
