# nvfbc-recorder2

A Cpp program that hooks into a patched NvFBC dll and records the screen. All recording is kept on-GPU by copying the frame buffer from NvFBC to a CUDA buffer, where we translate it from ARGB to NV12 and then send it to NVENC to encode to H264, before writing directly to a file. Understandably only runs on Nvidia GPUs.

**NOTE**: I am very new to c++ and don't consider this project to be a working program. It mainly serves as a reference point and inspiration. If you have issues related to building the project/dependencies, probably don't turn to me for help, although I will try to.
Tested on GTX 1070Ti with CUDA toolkit 12.9.

Prerequisites (for build):
- [Patched NvFBC64.dll /NvFBC.dll](https://github.com/keylase/nvidia-patch/tree/master/win/nvfbcwrp)
- CUDA Toolkit supported by your GPU.
- Might also have to manually enable NvFBC capturing by using NvFBCEnable.exe from the binary files in the NvFBC Capture SDK.
- Might be something else. If it doesn't work then contact me.

With some changes, this could work on Linux, where it wouldn't need to patch the NvFBC library.

[Apache licensed](LICENSE.txt)


  
