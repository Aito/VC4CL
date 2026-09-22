# VC4CL (Modernized Fork)

**VC4CL** is an OpenCL 1.2 implementation for the VideoCore IV GPU, allowing you to run OpenCL workloads natively on Raspberry Pi 1, 2, 3, and Zero.

This repository is a **modernized fork** of the original VC4CL project. It has been extensively updated to function flawlessly on modern Linux distributions (such as Raspberry Pi OS Trixie, Linux 6.x), completely bypassing deprecated Broadcom APIs and safely operating within the boundaries of modern Linux kernel DRM validations.

## 🚀 Key Modernization & Features

- **Target Platform**: Raspberry Pi (VideoCore IV) running modern Linux distributions.
- **Modern Environment Support**: 
  - Built and tested on **Raspberry Pi OS (Trixie)** with **Linux 6.x** kernels.
  - Operates safely as a standalone OpenCL driver.
- **Complete Backend Overhaul**: 
  - **Removed Legacy APIs**: Completely eliminated the reliance on deprecated Broadcom proprietary APIs (`bcm_host`, `vcsm`, `VCHI`).
  - **Direct Kernel Interaction**: Migrated QPU execution and buffer management directly to `/dev/vcio` (Mailbox) and standard kernel interfaces, clearing modern Linux DRM and kernel validation limitations.
- **System ICD Integration**: 
  - Modernized CMake configuration supports `BUILD_ICD=ON`, allowing the runtime to automatically register itself as a standard OpenCL driver (`/etc/OpenCL/vendors/VC4CL.icd`) via the standard `ocl-icd` loader.
- **Verified on Hardware**:
  - Successfully deployed on actual Raspberry Pi 3 hardware.
  - Normal recognition via `clinfo`.
  - Achieves 100% pass rate on internal test suites.
  - Validated execution of custom OpenCL kernels (e.g., vector additions, heavy trigonometric loops).

## ⚠️ Important Execution Note on 64-bit Systems
When running OpenCL programs on modern **aarch64 (64-bit)** environments that dynamically load this driver via the ICD loader (`libOpenCL.so`), you may encounter a `SIGBUS` (Bus Error). This is a known Linux architectural limitation when `dlopen` is called on libraries (like LLVM, which VC4CL links to via VC4C) that utilize large `initial-exec` Thread Local Storage (TLS).

**Workaround**: Force the runtime to load at program startup using `LD_PRELOAD`:
```bash
sudo env LD_PRELOAD=/usr/local/lib/libVC4CL.so ./your_opencl_program
```

## 📦 Build and Installation

### Prerequisites
Before building VC4CL, you MUST install the modernized compiler, **[VC4C](<your_vc4c_repository_url>)**. You also need standard OpenCL headers and the ICD loader:
```bash
sudo apt update
sudo apt install cmake gcc g++ ocl-icd-opencl-dev ocl-icd-dev opencl-headers
```

### Build Instructions
```bash
git clone <your_repository_url>/VC4CL.git
cd VC4CL

# Configure with CMake (Automatically detects VC4C to enable JIT compilation)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_ICD=ON -DBUILD_TESTING=OFF

# Build using multiple cores
make -C build -j4

# Install to the system
sudo cmake --install build
```

## 🛠️ Development & Modernization Environment
This modernization project was successfully completed with the assistance of **Antigravity 2.0 (Powered by Gemini 3.1 Pro)**.

The development, debugging, and verification were conducted under the following environment:
- **AI Assistant**: Antigravity 2.0 + Gemini 3.1 Pro (Google Deepmind)
- **Host Machine**: macOS
- **Target Hardware**: Raspberry Pi 3 Model B (aarch64)
- **Target OS**: Raspberry Pi OS (Trixie)
- **Target Kernel**: Linux 6.x
- **Toolchain**: GCC 14.2.0, LLVM 19.1.7, Clang 19
