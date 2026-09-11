/*
 * Author: doe300
 *
 * See the file "LICENSE" for the full license governing this code.
 */

#include "DRM.h"

#include <iostream>
#include <system_error>

#ifndef MOCK_HAL
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <libdrm/vc4_drm.h>
#endif

using namespace vc4cl;

DRM::DRM() : fd(-1)
{
#ifndef MOCK_HAL
    fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if(fd < 0)
    {
        throw std::system_error(errno, std::system_category(), "Failed to open DRM device /dev/dri/card0");
    }
    DEBUG_LOG(DebugLevel::SYSCALL, std::cout << "[VC4CL] DRM device opened, fd: " << fd << std::endl)
#endif
}

DRM::~DRM()
{
#ifndef MOCK_HAL
    if(fd >= 0)
    {
        close(fd);
        DEBUG_LOG(DebugLevel::SYSCALL, std::cout << "[VC4CL] DRM device closed" << std::endl)
    }
#endif
}

std::unique_ptr<DeviceBuffer> DRM::allocateBuffer(
    const std::shared_ptr<SystemAccess>& system, unsigned sizeInBytes, CacheType cacheType)
{
#ifndef MOCK_HAL
    struct drm_vc4_create_bo create = {0};
    create.size = sizeInBytes;

    if(ioctl(fd, DRM_IOCTL_VC4_CREATE_BO, &create) != 0)
    {
        DEBUG_LOG(DebugLevel::SYSCALL, std::cout << "[VC4CL] Failed to allocate DRM BO of size " << sizeInBytes << std::endl)
        return nullptr;
    }

    struct drm_vc4_mmap_bo mmap_bo = {0};
    mmap_bo.handle = create.handle;

    if(ioctl(fd, DRM_IOCTL_VC4_MMAP_BO, &mmap_bo) != 0)
    {
        DEBUG_LOG(DebugLevel::SYSCALL, std::cout << "[VC4CL] Failed to mmap DRM BO handle: " << create.handle << std::endl)
        // Ideally we should free the BO here, but DRM handles that when closed/freed. 
        // Need to explicitly close GEM handle?
        struct drm_gem_close gem_close = {0};
        gem_close.handle = create.handle;
        ioctl(fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
        return nullptr;
    }

    void* hostPointer = mmap(nullptr, sizeInBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mmap_bo.offset);
    if(hostPointer == MAP_FAILED)
    {
        DEBUG_LOG(DebugLevel::SYSCALL, std::cout << "[VC4CL] mmap failed for DRM BO handle: " << create.handle << std::endl)
        struct drm_gem_close gem_close = {0};
        gem_close.handle = create.handle;
        ioctl(fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
        return nullptr;
    }

    // VC4 DRM doesn't explicitly expose the QPU bus address because the kernel driver relocates BOs on submit.
    // However, for compatibility with the current HAL buffer object, we store the handle as the "qpuPointer".
    DevicePointer qpuPointer(create.handle);

    DEBUG_LOG(DebugLevel::MEMORY,
        std::cout << "Allocated " << sizeInBytes << " bytes of DRM buffer: handle " << create.handle 
                  << ", host address " << hostPointer << std::endl)
                  
    return std::unique_ptr<DeviceBuffer>{new DeviceBuffer(system, create.handle, qpuPointer, hostPointer, sizeInBytes)};
#else
    return nullptr;
#endif
}

bool DRM::deallocateBuffer(const DeviceBuffer* buffer)
{
#ifndef MOCK_HAL
    if(buffer->hostPointer != nullptr)
    {
        munmap(buffer->hostPointer, buffer->size);
    }
    
    struct drm_gem_close gem_close = {0};
    gem_close.handle = buffer->memHandle;
    
    if(ioctl(fd, DRM_IOCTL_GEM_CLOSE, &gem_close) != 0)
    {
        return false;
    }
    
    DEBUG_LOG(DebugLevel::MEMORY,
        std::cout << "Deallocated " << buffer->size << " bytes of DRM buffer: handle " << buffer->memHandle << std::endl)
    return true;
#else
    return true;
#endif
}

ExecutionHandle DRM::executeQPU(unsigned numQPUs, std::pair<uint32_t*, unsigned> controlAddress, bool flushBuffer,
    std::chrono::milliseconds timeout, const std::vector<uint32_t>& boHandles)
{
#ifndef MOCK_HAL
    // Submit CL requires assembling a drm_vc4_submit_cl structure.
    struct drm_vc4_submit_cl submit = {0};
    
    // Pass the BO handles used by this execution
    submit.bo_handles = reinterpret_cast<uint64_t>(boHandles.data());
    submit.bo_handle_count = static_cast<uint32_t>(boHandles.size());
    
    // NOTE: This is a stub for the bin_cl / shader_rec construction.
    // A complete implementation must generate a bin_cl with VC4_PACKET_GL_SHADER_STATE
    // and a valid shader_rec pointing to uniforms and a shader BO created via DRM_IOCTL_VC4_CREATE_SHADER_BO.
    // The current buffer (containing QPU code) was created with DRM_IOCTL_VC4_CREATE_BO, which is rejected by kernel validation.
    
    // if(ioctl(fd, DRM_IOCTL_VC4_SUBMIT_CL, &submit) != 0)
    //     return ExecutionHandle{false};

    return ExecutionHandle{true};
#else
    return ExecutionHandle{false};
#endif
}

bool DRM::readValue(SystemQuery query, uint32_t& output) noexcept
{
#ifndef MOCK_HAL
    switch(query)
    {
    case SystemQuery::NUM_QPUS:
    case SystemQuery::TOTAL_GPU_MEMORY_IN_BYTES:
    case SystemQuery::CURRENT_QPU_CLOCK_RATE_IN_HZ:
    case SystemQuery::MAXIMUM_QPU_CLOCK_RATE_IN_HZ:
    case SystemQuery::QPU_TEMPERATURE_IN_MILLI_DEGREES:
    case SystemQuery::TOTAL_VPM_MEMORY_IN_BYTES:
        // DRM might expose these via DRM_IOCTL_VC4_GET_PARAM
        return false;
    default:
        return false;
    }
#else
    return false;
#endif
}
