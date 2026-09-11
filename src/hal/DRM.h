/*
 * Author: doe300
 *
 * See the file "LICENSE" for the full license governing this code.
 */

#ifndef VC4CL_DRM
#define VC4CL_DRM

#include "hal.h"
#include <chrono>
#include <memory>
#include <string>

namespace vc4cl
{
    class DRM
    {
    public:
        DRM();
        ~DRM();

        std::unique_ptr<DeviceBuffer> allocateBuffer(
            const std::shared_ptr<SystemAccess>& system, unsigned sizeInBytes, CacheType cacheType);
        bool deallocateBuffer(const DeviceBuffer* buffer);

        ExecutionHandle executeQPU(unsigned numQPUs, std::pair<uint32_t*, unsigned> controlAddress, bool flushBuffer,
            std::chrono::milliseconds timeout, const std::vector<uint32_t>& boHandles);

        bool readValue(SystemQuery query, uint32_t& output) noexcept;

    private:
        int fd;
    };
}

#endif /* VC4CL_DRM */
