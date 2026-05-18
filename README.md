# PBC-gAPI

**PBC-gAPI** is a custom Graphics API implemented completely from scratch. It is designed to deliver high-performance rendering across a diverse range of hardware, from resource-constrained embedded devices like the **ESP32** to high-performance, **CUDA-compatible NVIDIA graphics cards**.

## Project Structure & Core Files

The repository currently centers around two core files:

*   **`kernel.cu`**: The primary pipeline designed for CUDA-enabled GPUs, leveraging Hardware Device Context (HDC) for accelerated rendering.
*   **`terminalPBC_API.cpp`**: Contains the core shader code used by the CUDA pipeline, alongside **`ShaderTri`**, a standalone function engineered to execute the rasterization and shading pipeline directly on the CPU.

> **Note on Development Status:** 
> As this project is actively under development, the demonstration samples are currently embedded within the main CUDA pipeline file. To improve modularity and ease of integration for external projects, the samples and the core pipeline architecture will be split into separate modules in a future update.

## Authority and Copyright

This project is the sole property of **Pablo B. C.** 

The author's name and copyright authority must be explicitly preserved and inferred across all source files, documentation, and derivatives, regardless of any future modifications, refactoring, or community contributions.
