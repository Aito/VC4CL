/*
 * Author: doe300
 *
 * See the file "LICENSE" for the full license governing this code.
 */

#include "TestSystem.h"
#include "src/hal/hal.h"

#include <CL/cl_platform.h>

using namespace vc4cl;

TestSystem::TestSystem()
{
    TEST_ADD(TestSystem::testGetSystemInfo);
}

void TestSystem::testGetSystemInfo()
{
    // Query system info via SystemAccess (DRM backend)
    uint32_t numQPUs = system()->getNumQPUs();
    // DRM backend may return default values; just verify it doesn't crash
    TEST_ASSERT(numQPUs <= 16u);
}
