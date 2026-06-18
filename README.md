# nvfbc-recorder2

A Cpp program that hooks into a patched NvFBC dll and records the screen. All recording is kept on-GPU by copying the frame buffer from NvFBC to a CUDA buffer, where we translate it from ARGB to NV12 and then send it to NVENC to encode to H264, before writing directly to a file. Understandably only runs on Nvidia GPUs.

**NOTE**: I am very new to c++ and don't consider this project to be a working program. It mainly serves as a reference point and inspiration. If you have issues related to building the project/dependencies, probably don't turn to me for help, although I will try to.
Tested on GTX 1070Ti with CUDA toolkit 12.9.

Prerequisites (for build):
- [Patched NvFBC64.dll / NvFBC.dll](https://github.com/keylase/nvidia-patch/tree/master/win/nvfbcwrp)
- CUDA Toolkit supported by your GPU.
- Might also have to manually enable NvFBC capturing by using NvFBCEnable.exe from the binary files in the NvFBC Capture SDK.
- Might be something else. If it doesn't work then contact me.

With some changes, this could work on Linux, where it wouldn't need to patch the NvFBC library.

DVR branch
--

This branch allows you to record in DVR mode, saving the screen in a circular buffer and allowing you to save the last 30 or so seconds.

How to record: 
- nvfbc-recorder2.exe to start
- press F10 anywhere to stop
- output is `replay_saved.h264` in the same directory
- run `ffmpeg -y -r 30 -i .\replay_saved.h264 -c copy video.mp4` to convert to mp4 (replacing -r 30 with your set frame rate)
- repeat

Currently all the settings are static and not changeable dynamically. Rebuilds are required.


Frame rate is currently set at multiple places as I haven't unified them yet, but searching for these values:
- `const int FPS =` (nvfbc-recorder2.cpp)
- `m_stInitEncParams.frameRateNum = ` (Encoder.cpp)
- `m_stEncodeConfig.encodeCodecConfig.h264Config.idrPeriod = ` (Encoder.cpp)
- `m_stEncodeConfig.gopLength = ` (Encoder.cpp)

and replacing them should allow you to change the frame rate.

Since this uses CUDA to record from NvFBC, you can only downscale using a CUDA kernel, which is included in [this kernel](nvfbc-recorder2\kernels.cu) but can be customized. (Keep in mind to build with your valid CUDA toolkit)
There are variables `int targetW` and `int targetH `in nvfbc-recorder2.cpp and in Encoder.cpp `m_stInitEncParams.encodeWidth `and `m_stInitEncParams.encodeHeight`. The rest should be handled.

To change bitrate, search for 8000000 in nvfbc-recorder2.cpp. There should be 2 places to switch them out.

Generally, encoding settings are located in Encoder.cpp#SetupEncoder.


[Apache licensed](LICENSE.txt)