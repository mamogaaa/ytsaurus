#pragma once

#include "public.h"

#include <yt/yt/client/api/public.h>

#include <yt/yt/library/profiling/sensor.h>

namespace NYT::NAuth {

////////////////////////////////////////////////////////////////////////////////

struct ICypressUserManager
    : public virtual TRefCounted
{
    virtual TFuture<NObjectClient::TObjectId> CreateUser(const TString& user) = 0;
};

DEFINE_REFCOUNTED_TYPE(ICypressUserManager)

////////////////////////////////////////////////////////////////////////////////

ICypressUserManagerPtr CreateCypressUserManager(
    TCypressUserManagerConfigPtr config,
    NApi::IClientPtr client);

////////////////////////////////////////////////////////////////////////////////

ICypressUserManagerPtr CreateCachingCypressUserManager(
    TCachingCypressUserManagerConfigPtr config,
    ICypressUserManagerPtr userManager,
    NProfiling::TProfiler profiler);

////////////////////////////////////////////////////////////////////////////////

}
