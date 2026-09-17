/*
 * Author: doe300
 *
 * See the file "LICENSE" for the full license governing this code. See the copyright statement below for the original
 * code:
 */
/*
Copyright (c) 2012, Broadcom Europe Ltd.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "Mailbox.h"

#include "hal.h"

#include <cstdio>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <system_error>
#include <unistd.h>

using namespace vc4cl;

#define MAJOR_NUM 100
#define IOCTL_MBOX_PROPERTY _IOWR(MAJOR_NUM, 0, char*)
#define DEVICE_FILE_NAME "/dev/vcio"

Mailbox::Mailbox() : fd(open(DEVICE_FILE_NAME, 0))
{
    if(fd < 0)
    {
        DEBUG_LOG(DebugLevel::SYSCALL, std::cout << "Failed to open mailbox: " << DEVICE_FILE_NAME << std::endl)
        return; // Do not throw, hal.cpp will check fd < 0
    }

    ignoreReturnValue(enableQPU(true) ? CL_SUCCESS : CL_OUT_OF_RESOURCES, __FILE__, __LINE__,
        "Failed to enable QPUs");
}

Mailbox::~Mailbox()
{
    if(fd >= 0)
    {
        ignoreReturnValue(enableQPU(false) ? CL_SUCCESS : CL_OUT_OF_RESOURCES, __FILE__, __LINE__,
            "Failed to disable QPUs");
        close(fd);
        DEBUG_LOG(DebugLevel::SYSCALL, std::cout << "[VC4CL] Mailbox file descriptor closed: " << fd << std::endl)
    }
}

// -------------------------------------------------------------
// VCSM-CMA IOCTL Definitions (Standalone)
// -------------------------------------------------------------
#define VC_SM_CMA_RESOURCE_NAME               32
#define VC_SM_CMA_MAGIC_TYPE                  'J'

enum vc_sm_cma_cmd_e {
    VC_SM_CMA_CMD_ALLOC = 0x5A,
    VC_SM_CMA_CMD_IMPORT_DMABUF,
    VC_SM_CMA_CMD_CLEAN_INVALID2,
    VC_SM_CMA_CMD_LAST
};

enum vc_sm_cma_cache_e {
    VC_SM_CMA_CACHE_NONE,
    VC_SM_CMA_CACHE_HOST,
    VC_SM_CMA_CACHE_VC,
    VC_SM_CMA_CACHE_BOTH,
};

struct vc_sm_cma_ioctl_alloc {
    uint32_t size;
    uint32_t num;
    uint32_t cached;
    uint32_t pad;
    uint8_t name[VC_SM_CMA_RESOURCE_NAME];
    int32_t handle;
    uint32_t vc_handle;
    uint64_t dma_addr;
};

#define VC_SM_CMA_IOCTL_MEM_ALLOC \
    _IOR(VC_SM_CMA_MAGIC_TYPE, VC_SM_CMA_CMD_ALLOC, struct vc_sm_cma_ioctl_alloc)
// -------------------------------------------------------------

std::unique_ptr<DeviceBuffer> Mailbox::allocateBuffer(
    const std::shared_ptr<SystemAccess>& system, unsigned sizeInBytes, CacheType cacheType)
{
    // Make size page-aligned
    unsigned allocSize = sizeInBytes;
    if (allocSize % 4096 != 0)
        allocSize += 4096 - (allocSize % 4096);

    int vcsm_fd = open("/dev/vcsm-cma", O_RDWR);
    if(vcsm_fd < 0) {
        std::cout << "[VC4CL] Failed to open /dev/vcsm-cma. Is the driver loaded?" << std::endl;
        return nullptr;
    }

    vc_sm_cma_cache_e cached = VC_SM_CMA_CACHE_NONE;
    if(cacheType == CacheType::BOTH_CACHED)
        cached = VC_SM_CMA_CACHE_BOTH;
    else if(cacheType == CacheType::HOST_CACHED)
        cached = VC_SM_CMA_CACHE_HOST;
    else if(cacheType == CacheType::GPU_CACHED)
        cached = VC_SM_CMA_CACHE_VC;

    vc_sm_cma_ioctl_alloc alloc = {};
    alloc.size = allocSize;
    alloc.num = 1;
    alloc.cached = cached;
    
    if (ioctl(vcsm_fd, VC_SM_CMA_IOCTL_MEM_ALLOC, &alloc) < 0) {
        std::cout << "[VC4CL] VC_SM_CMA_IOCTL_MEM_ALLOC failed" << std::endl;
        close(vcsm_fd);
        return nullptr;
    }

    int dmabuf_fd = alloc.handle;
    close(vcsm_fd); // The dmabuf fd holds the reference now

    void* hostPointer = mmap(nullptr, allocSize, PROT_READ | PROT_WRITE, MAP_SHARED, dmabuf_fd, 0);
    if(hostPointer == MAP_FAILED) {
        std::cout << "[VC4CL] Failed to mmap dmabuf fd" << std::endl;
        close(dmabuf_fd);
        return nullptr;
    }

    DevicePointer qpuPointer(static_cast<uint32_t>(alloc.dma_addr));

    DEBUG_LOG(DebugLevel::MEMORY,
        std::cout << "Allocated " << sizeInBytes << " (aligned " << allocSize << ") bytes via vcsm-cma: fd " 
                  << dmabuf_fd << ", vc_handle " << alloc.vc_handle << ", device address "
                  << std::hex << "0x" << qpuPointer << ", host address " << hostPointer << std::dec << std::endl)

    return std::unique_ptr<DeviceBuffer>{new DeviceBuffer(system, static_cast<unsigned>(dmabuf_fd), qpuPointer, hostPointer, sizeInBytes)};
}

bool Mailbox::deallocateBuffer(const DeviceBuffer* buffer)
{
    if(buffer->hostPointer != nullptr)
    {
        unsigned allocSize = buffer->size;
        if (allocSize % 4096 != 0)
            allocSize += 4096 - (allocSize % 4096);
        munmap(buffer->hostPointer, allocSize);
    }
    if(buffer->memHandle != 0)
    {
        // For vcsm-cma dmabuf, closing the fd releases the memory and VideoCore handle automatically
        close(static_cast<int>(buffer->memHandle));
        DEBUG_LOG(DebugLevel::MEMORY,
            std::cout << "Deallocated " << buffer->size << " bytes via vcsm-cma: fd " << buffer->memHandle
                      << ", device address " << std::hex << "0x" << buffer->qpuPointer << ", host address "
                      << buffer->hostPointer << std::dec << std::endl)
    }
    return true;
}

ExecutionHandle Mailbox::executeCode(uint32_t codeAddress, unsigned valueR0, unsigned valueR1, unsigned valueR2,
    unsigned valueR3, unsigned valueR4, unsigned valueR5) const
{
    MailboxMessage<MailboxTag::EXECUTE_CODE, 7, 1> msg(
        {codeAddress, valueR0, valueR1, valueR2, valueR3, valueR4, valueR5});
    if(mailboxCall(msg.buffer.data()) < 0)
        return ExecutionHandle{false};
    return ExecutionHandle{msg.getContent(0) == 0};
}

ExecutionHandle Mailbox::executeQPU(unsigned numQPUs, std::pair<uint32_t*, uint32_t> controlAddress, bool flushBuffer,
    std::chrono::milliseconds timeout) const
{
    if(timeout.count() > 0xFFFFFFFF)
    {
        DEBUG_LOG(DebugLevel::SYSCALL,
            std::cout << "Timeout is too big, needs fit into a 32-bit integer: " << timeout.count() << std::endl)
        return ExecutionHandle{false};
    }
    /*
     * "By default the qpu_execute call does a GPU side L1 and L2 data cache flush before executing the qpu code. If you
     * are happy it is safe not to do this, setting noflush=1 will be a little quicker." see:
     * https://github.com/raspberrypi/firmware/issues/747
     */
    MailboxMessage<MailboxTag::EXECUTE_QPU, 4, 1> msg(
        {numQPUs, controlAddress.second, static_cast<unsigned>(!flushBuffer), static_cast<unsigned>(timeout.count())});
    if(mailboxCall(msg.buffer.data()) < 0)
        return ExecutionHandle{false};
    return ExecutionHandle{msg.getContent(0) == 0};
}

bool Mailbox::readValue(SystemQuery query, uint32_t& output) noexcept
{
    switch(query)
    {
    case SystemQuery::CURRENT_QPU_CLOCK_RATE_IN_HZ:
    {
        QueryMessage<MailboxTag::GET_CLOCK_RATE> msg({static_cast<uint32_t>(VC4Clock::V3D)});
        if(!readMailboxMessage(msg))
            return false;
        output = msg.getContent(1);
        return true;
    }
    case SystemQuery::MAXIMUM_QPU_CLOCK_RATE_IN_HZ:
    {
        QueryMessage<MailboxTag::GET_MAX_CLOCK_RATE> msg({static_cast<uint32_t>(VC4Clock::V3D)});
        if(!readMailboxMessage(msg))
            return false;
        output = msg.getContent(1);
        return true;
    }
    case SystemQuery::CURRENT_ARM_CLOCK_RATE_IN_HZ:
    {
        QueryMessage<MailboxTag::GET_CLOCK_RATE> msg({static_cast<uint32_t>(VC4Clock::ARM)});
        if(!readMailboxMessage(msg))
            return false;
        output = msg.getContent(1);
        return true;
    }
    case SystemQuery::MAXIMUM_ARM_CLOCK_RATE_IN_HZ:
    {
        QueryMessage<MailboxTag::GET_MAX_CLOCK_RATE> msg({static_cast<uint32_t>(VC4Clock::ARM)});
        if(!readMailboxMessage(msg))
            return false;
        output = msg.getContent(1);
        return true;
    }
    case SystemQuery::QPU_TEMPERATURE_IN_MILLI_DEGREES:
    {
        QueryMessage<MailboxTag::GET_TEMPERATURE> msg({0});
        //"Return the temperature of the SoC in thousandths of a degree C. id should be zero."
        if(!readMailboxMessage(msg))
            return false;
        output = msg.getContent(1);
        return true;
    }
    case SystemQuery::TOTAL_ARM_MEMORY_IN_BYTES:
    {
        SimpleQueryMessage<MailboxTag::ARM_MEMORY> msg;
        if(!readMailboxMessage(msg))
            return false;
        output = msg.getContent(1);
        return true;
    }
    case SystemQuery::TOTAL_GPU_MEMORY_IN_BYTES:
    {
        SimpleQueryMessage<MailboxTag::VC_MEMORY> msg;
        if(!readMailboxMessage(msg))
            return false;
        output = msg.getContent(1);
        return true;
    }
    case SystemQuery::TOTAL_VPM_MEMORY_IN_BYTES:
    default:
        return false;
    }
    return false;
}

/*
 * use ioctl to send mbox property message
 */
int Mailbox::mailboxCall(void* buffer) const
{
    unsigned* p = reinterpret_cast<unsigned*>(buffer);
    unsigned size = *p;
    DEBUG_LOG(DebugLevel::SYSCALL, {
        std::cout << "Mailbox buffer before:";
        for(unsigned i = 0; i < size / 4; ++i)
            std::cout << ' ' << std::hex << std::setfill('0') << std::setw(8) << p[i] << std::dec;
        std::cout << std::endl;
    })

    int ret_val = ioctl(fd, IOCTL_MBOX_PROPERTY, buffer);
    if(ret_val < 0)
    {
        DEBUG_LOG(DebugLevel::SYSCALL, std::cout << "ioctl_set_msg failed: " << ret_val << std::endl)
        perror("[VC4CL] Error in mbox_property");
        throw std::system_error(errno, std::system_category(), "Failed to set mailbox property");
    }

    DEBUG_LOG(DebugLevel::SYSCALL, {
        std::cout << "Mailbox buffer after:";
        for(unsigned i = 0; i < size / 4; ++i)
            std::cout << ' ' << std::hex << std::setfill('0') << std::setw(8) << p[i] << std::dec;
        std::cout << std::endl;
    })
    return ret_val;
}

bool Mailbox::enableQPU(bool enable) const
{
    QueryMessage<MailboxTag::ENABLE_QPU> msg({static_cast<unsigned>(enable)});
    if(mailboxCall(msg.buffer.data()) < 0)
        return false;
    /*
     * If the mailbox is already running/being used, 0x80000000 is returned (see #16).
     * This seems to be also true for shutting down the mailbox,
     * which hints to some kind of reference-counter within the VC4 hard-/firmware
     * only returning 0 for the first open/last close and 0x80000000 otherwise.
     */
    return msg.getContent(0) == 0 || msg.getContent(0) == 0x80000000;
}

unsigned Mailbox::memAlloc(unsigned sizeInBytes, unsigned alignmentInBytes, MemoryFlag flags) const
{
    MailboxMessage<MailboxTag::ALLOCATE_MEMORY, 3, 1> msg({sizeInBytes, alignmentInBytes, flags});
    if(mailboxCall(msg.buffer.data()) < 0)
        return 0;
    return msg.getContent(0);
}

DevicePointer Mailbox::memLock(unsigned handle) const
{
    QueryMessage<MailboxTag::LOCK_MEMORY> msg({handle});
    if(mailboxCall(msg.buffer.data()) < 0)
        return DevicePointer(0);
    return DevicePointer(msg.getContent(0));
}

bool Mailbox::memUnlock(unsigned handle) const
{
    QueryMessage<MailboxTag::UNLOCK_MEMORY> msg({handle});
    if(mailboxCall(msg.buffer.data()) < 0)
        return false;
    return msg.getContent(0) == 0;
}

bool Mailbox::memFree(unsigned handle) const
{
    QueryMessage<MailboxTag::RELEASE_MEMORY> msg({handle});
    if(mailboxCall(msg.buffer.data()) < 0)
        return false;
    return msg.getContent(0) == 0;
}

CHECK_RETURN bool Mailbox::checkReturnValue(unsigned value) const
{
    if((value >> 31) == 1) // 0x8000000x
    {
        // 0x80000000 on success
        // 0x80000001 on failure
        DEBUG_LOG(DebugLevel::SYSCALL,
            std::cout << "Mailbox request: " << (((value & 0x1) == 0x1) ? "failed" : "succeeded") << std::endl)
        return value == 0x80000000;
    }
    else
    {
        DEBUG_LOG(DebugLevel::SYSCALL, std::cout << "Unknown return code: " << value << std::endl)
        return false;
    }
}
