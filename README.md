# SDVX Training Master

**English** | [简体中文](README.zh.md)

Training Master is a training mod for SDVX. Give you lot of training features

## Download

Go Action page and download latest build

## Use

Put `trainingmaster.dll` and `trainingmaster.ini` into contents root or modules folder, then use spice2x to inject hook

Open game then press `Insert`

## Build

On Windows, install CMake, Git and Visual Studio 2026 with the C++ desktop
workload, then run `build.bat`. The DLL is written to
`build/Release/trainingmaster.dll`. Set `BUILD_JOBS` to change parallelism.

On Linux, install CMake, Ninja, Git and the LLVM MinGW UCRT toolchain, then add
the toolchain's `bin` directory to `PATH` and run `bash build.sh`. The Windows
x64 DLL is written to `build-linux-cross/trainingmaster.dll`. Set `BUILD_JOBS`
to change parallelism, or set `LLVM_MINGW_ROOT` to the toolchain directory.

## Features

| Feature | Description |
| --- | --- |
| Playback controls | Seek, pause, resume, or play in reverse |
| Playback rate | Adjust speed from 0.25x to 1.00x, with preset buttons |
| Looping | Mark IN and OUT points, repeat continuously, or loop once |
| Loop buffer | Set the time added before and after the selected loop |
| Chart view | View notes and lasers and edit loop points |
| AutoPlay | Enable native automatic play |
| Lane speed | Lock lane speed |
| Camera | Block camera movement during training |
| Gauge | Override the gauge type and initial value |

## License

Copyright (c) 2026 Takanashi Ryo. Project source is licensed under the [GNU General Public License v3.0 only](LICENSE) (GPL-3.0-only)

## 3rd Party

- [Dear ImGui](https://github.com/ocornut/imgui)
- [MinHook](https://github.com/tsudakageyu/minhook)

## AI

All code written and reversed by AI
UI design by human
