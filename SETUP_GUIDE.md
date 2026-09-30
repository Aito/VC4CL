# VC4CL (VideoCore IV OpenCL) - Debian Trixie (Kernel 6.x) Setup Guide

This document is the official guide for building and configuring VC4CL on modern Raspberry Pi OS environments (e.g., Debian Bookworm / Trixie, Kernel 6.x series).

## 1. Background and New Architecture
Previously, VC4CL depended on Broadcom's proprietary legacy libraries (`libbcm_host.so`, `libvcsm.so`), which have been completely removed from modern Linux environments.

An approach utilizing the standard Linux DRM (`vc4_drm`) driver was also investigated. However, due to kernel security specifications, **VPM DMA (memory writes) are completely blocked at the driver level**, making it impossible to execute Compute Shaders.

Ultimately, VC4CL evolved into a standalone architecture independent of external legacy libraries. It fully supports modern environments by implementing **memory allocation via direct IOCTLs to VCSM-CMA (`/dev/vcsm-cma`)** which is standard on modern kernels, and **QPU bare-metal execution via Mailbox (`/dev/vcio`)**.

---

## 2. Hardware System Requirements and Configuration (Crucial)

- **Target Hardware:** Raspberry Pi 1 / 2 / 3 / Zero (VideoCore IV)
- **Target OS:** Raspberry Pi OS (Debian Bookworm or Trixie / Kernel 6.x)

### ⚠️ KMS Driver Interrupt Conflict
Modern OSs use the Wayland desktop by default, which relies on `vc4-kms-v3d` (KMS display driver). However, enabling this driver causes the **kernel to intercept V3D hardware interrupts (IRQ)**. As a result, VC4CL (via Mailbox) cannot detect computation completion, leading to a 30-second timeout.

To ensure fast and proper VC4CL execution, switching to **X11 (Framebuffer mode) or Headless environment** is mandatory.

#### Setup Instructions (To maintain an X11 Desktop)
1. Run `sudo raspi-config`.
2. Navigate to `Advanced Options` -> `Wayland` and select **`X11`**.
3. Open `/boot/firmware/config.txt` (or `/boot/config.txt`) and comment out the KMS overlay:
   ```text
   #dtoverlay=vc4-kms-v3d
   ```
4. Save and reboot your Raspberry Pi (`sudo reboot`).

---

## 3. Installing Required Development Packages

Install the compiler, CMake, and OpenCL-related headers (including the ICD loader) required for the build.

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    opencl-headers \
    ocl-icd-dev \
    ocl-icd-opencl-dev \
    clinfo
```

---

## 4. Building and Installing VC4CL (Runtime)

```bash
# 1. Clone
git clone https://github.com/Aito/VC4CL
cd VC4CL

# 2. CMake Configuration (BUILD_ICD=ON is required)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_ICD=ON

# 3. Build
cmake --build build -j$(nproc)

# 4. Run Unit Tests (Verify 100% Pass)
cd build
sudo ctest --output-on-failure

# 5. Install
sudo cmake --install .

# 6. Register to OpenCL System (ICD)
sudo mkdir -p /etc/OpenCL/vendors
echo "/usr/local/lib/libVC4CL.so" | sudo tee /etc/OpenCL/vendors/VC4CL.icd
sudo ldconfig
```

---

## 5. Verification

Run the `clinfo` command and verify that the `VideoCore IV GPU` is recognized.
*(Note: Depending on user permissions, `sudo` might be required to access `/dev/vcio`)*

```bash
sudo clinfo
```

If configured correctly, platform and device information will be output as follows:

```text
Number of platforms                               1
  Platform Name                                   OpenCL for the Raspberry Pi VideoCore IV GPU
...
Number of devices                                 1
  Device Name                                     VideoCore IV GPU
...
```

---

## 6. Next Step: Installing the VC4C Compiler

The `VC4CL` built so far serves as the OpenCL **"Runtime"**.
To build kernel source code (OpenCL C) within an OpenCL application, the **[VC4C](https://github.com/Aito/VC4C)** compiler must be installed on your system to perform translation at runtime.

By installing VC4C, the `clinfo` output for `Compiler Available` will change to `Yes`, completing your OpenCL development environment.
