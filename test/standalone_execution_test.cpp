#include <CL/cl.h>
#include <iostream>
#include <vector>
#include <string.h>

const uint64_t kernel_binary[] = {
#include "hello_world_vector.hex"
};

int main() {
    cl_int err;
    cl_uint num_platforms;
    err = clGetPlatformIDs(0, nullptr, &num_platforms);
    if (err != CL_SUCCESS || num_platforms == 0) {
        std::cerr << "No OpenCL platforms found." << std::endl;
        return 1;
    }

    std::vector<cl_platform_id> platforms(num_platforms);
    clGetPlatformIDs(num_platforms, platforms.data(), nullptr);
    
    cl_platform_id platform = platforms[0];

    cl_uint num_devices;
    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 0, nullptr, &num_devices);
    if (err != CL_SUCCESS || num_devices == 0) {
        std::cerr << "No OpenCL devices found." << std::endl;
        return 1;
    }

    std::vector<cl_device_id> devices(num_devices);
    clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, num_devices, devices.data(), nullptr);
    cl_device_id device = devices[0];

    cl_context context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
    if (!context) {
        std::cerr << "Failed to create context." << std::endl;
        return 1;
    }

    cl_command_queue queue = clCreateCommandQueue(context, device, 0, &err);
    
    // Create program from binary
    size_t lengths[1] = { sizeof(kernel_binary) };
    const unsigned char* binaries[1] = { reinterpret_cast<const unsigned char*>(kernel_binary) };
    cl_int binary_status;
    
    cl_program program = clCreateProgramWithBinary(context, 1, &device, lengths, binaries, &binary_status, &err);
    if (err != CL_SUCCESS) {
        std::cerr << "Failed to create program with binary." << std::endl;
        return 1;
    }

    err = clBuildProgram(program, 1, &device, nullptr, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        std::cerr << "Failed to build program." << std::endl;
        return 1;
    }

    cl_kernel kernel = clCreateKernel(program, "hello_world", &err);
    if (!kernel) {
        std::cerr << "Failed to create kernel." << std::endl;
        return 1;
    }

    const int num_elements = 16;
    size_t size = num_elements * sizeof(char); // char16 in OpenCL means 16 chars = 16 bytes
    
    cl_mem buffer_in = clCreateBuffer(context, CL_MEM_READ_ONLY, size, nullptr, &err);
    cl_mem buffer_out = clCreateBuffer(context, CL_MEM_WRITE_ONLY, size, nullptr, &err);

    std::vector<char> host_in(num_elements);
    for (int i = 0; i < num_elements; ++i) {
        host_in[i] = 'A' + i;
    }
    
    clEnqueueWriteBuffer(queue, buffer_in, CL_TRUE, 0, size, host_in.data(), 0, nullptr, nullptr);

    clSetKernelArg(kernel, 0, sizeof(cl_mem), &buffer_in);
    clSetKernelArg(kernel, 1, sizeof(cl_mem), &buffer_out);

    size_t global_work_size = 1; // 1 work item that processes char16
    size_t local_work_size = 1;

    std::cout << "Submitting kernel for execution..." << std::endl;
    err = clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, &global_work_size, &local_work_size, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        std::cerr << "Failed to enqueue kernel: " << err << std::endl;
        return 1;
    }

    std::vector<char> host_out(num_elements, 0);
    clEnqueueReadBuffer(queue, buffer_out, CL_TRUE, 0, size, host_out.data(), 0, nullptr, nullptr);

    bool success = true;
    for (int i = 0; i < num_elements; ++i) {
        if (host_out[i] != host_in[i]) {
            std::cerr << "Mismatch at " << i << ": expected " << host_in[i] << ", got " << host_out[i] << std::endl;
            success = false;
        }
    }

    if (success) {
        std::cout << "SUCCESS! Data correctly copied by QPU." << std::endl;
    }

    clReleaseMemObject(buffer_in);
    clReleaseMemObject(buffer_out);
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);

    return success ? 0 : 1;
}
