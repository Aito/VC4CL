/*
 * Author: doe300
 *
 * See the file "LICENSE" for the full license governing this code.
 */

#include "hal.h"

#include "DRM.h"
#include "emulator.h"

#include <cstdlib>
#include <unistd.h>
#include <iostream>

using namespace vc4cl;

static bool getEmulated()
{
#ifdef MOCK_HAL
    return true;
#else
    return std::getenv("VC4CL_EMULATOR");
#endif
}

static std::pair<bool, CacheType> getForcedCacheType()
{
    auto envvar = std::getenv("VC4CL_CACHE_FORCE");
    if(!envvar)
        return std::make_pair(false, CacheType::UNCACHED);
    std::string env(envvar);
    auto start = env.find_first_of("0123456789");
    if(start != std::string::npos)
        return std::make_pair(true, static_cast<CacheType>(strtoul(env.data() + start, nullptr, 0)));
    return std::make_pair(false, CacheType::UNCACHED);
}

static std::unique_ptr<DRM> initializeDRM(bool isEmulated)
{
    if(isEmulated || std::getenv("VC4CL_NO_DRM"))
        return nullptr;
    return std::unique_ptr<DRM>(new DRM());
}

SystemAccess::SystemAccess() :
    isEmulated(getEmulated()), 
    executionMode(ExecutionMode::DRM), 
    memoryManagement(MemoryManagement::DRM),	
    forcedCacheType(getForcedCacheType()), 
    drm(initializeDRM(isEmulated))
{
    if(isEmulated)
        DEBUG_LOG(DebugLevel::SYSTEM_ACCESS, std::cout << "[VC4CL] Using emulated system accesses " << std::endl)
    if(drm)
        DEBUG_LOG(DebugLevel::SYSTEM_ACCESS,
            std::cout << "[VC4CL] Using DRM for: kernel execution, memory allocation, system queries" << std::endl)

    if(forcedCacheType.first)
    {
        std::string cacheType;
        switch(forcedCacheType.second)
        {
        case CacheType::UNCACHED:
            cacheType = "uncached";
            break;
        case CacheType::HOST_CACHED:
            cacheType = "host cached";
            break;
        case CacheType::GPU_CACHED:
            cacheType = "GPU cached";
            break;
        case CacheType::BOTH_CACHED:
            cacheType = "host and GPU cached";
            break;
        }
        DEBUG_LOG(
            DebugLevel::SYSTEM_ACCESS, std::cout << "[VC4CL] Forcing memory caching type: " << cacheType << std::endl)
    }
}

uint32_t SystemAccess::getTotalVPMMemory()
{
    /*
     * Assume all accessible VPM memory:
     * Due to a hardware bug (HW-2253), user programs can only use the first 64 rows of VPM, resulting in a total of 4KB
     * available VPM cache size (64 * 16 * sizeof(uint))
     */
    return querySystem(SystemQuery::TOTAL_VPM_MEMORY_IN_BYTES, 64 * 16 * sizeof(uint32_t));
}

uint32_t SystemAccess::querySystem(SystemQuery query, uint32_t defaultValue)
{
    uint32_t value = defaultValue;
    if(isEmulated)
        return getEmulatedSystemQuery(query);
    if(drm && drm->readValue(query, value))
        return value;
    return defaultValue;
}

std::string SystemAccess::getModelType()
{
    if(isEmulated)
        return "(emulated)";
    return "Linux DRM Backend";
}

std::string SystemAccess::getProcessorType()
{
    if(isEmulated)
        return "(emulated)";
    return "VC4/V3D";
}

std::unique_ptr<DeviceBuffer> SystemAccess::allocateBuffer(
    unsigned sizeInBytes, const std::string& name, CacheType cacheType)
{
    if(isEmulated)
        return allocateEmulatorBuffer(shared_from_this(), sizeInBytes);
    auto effectiveCacheType = forcedCacheType.first ? forcedCacheType.second : cacheType;
    if(drm)
        return drm->allocateBuffer(shared_from_this(), sizeInBytes, effectiveCacheType);
    return nullptr;
}

std::unique_ptr<DeviceBuffer> SystemAccess::allocateGPUOnlyBuffer(
    unsigned sizeInBytes, const std::string& name, CacheType cacheType)
{
    return allocateBuffer(sizeInBytes, name, cacheType);
}

bool SystemAccess::deallocateBuffer(const DeviceBuffer* buffer)
{
    if(isEmulated)
        deallocateEmulatorBuffer(buffer);
    else if(drm)
        return drm->deallocateBuffer(buffer);
    return false;
}

bool SystemAccess::flushCPUCache(const std::vector<const DeviceBuffer*>& buffers)
{
    return true; 
}

ExecutionHandle SystemAccess::executeQPU(unsigned numQPUs, std::pair<uint32_t*, unsigned> controlAddress,
    bool flushBuffer, std::chrono::milliseconds timeout, const std::vector<uint32_t>& boHandles)
{
    if(isEmulated)
        return ExecutionHandle(emulateQPU(numQPUs, controlAddress.second, timeout));
    if(drm)
        return drm->executeQPU(numQPUs, controlAddress, flushBuffer, timeout, boHandles);
    return ExecutionHandle{false};
}

std::shared_ptr<SystemAccess>& vc4cl::system()
{
    static std::shared_ptr<SystemAccess> sys{new SystemAccess()};
    return sys;
}
