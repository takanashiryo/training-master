# SDVX 训练大师

[English](README.md) | **简体中文**

训练大师是一款 SDVX 训练模组，提供多种练习功能

## 下载

前往 Actions 页面下载最新构建版本

## 使用

将 `trainingmaster.dll` 和 `trainingmaster.ini` 放入 contents 根目录或 modules 文件夹，然后使用 spice2x 注入

启动游戏后按 `Insert` 打开界面

## 构建

在 Windows 上安装 CMake、Git 和包含 C++ 桌面开发组件的 Visual Studio 2026，
然后运行 `build.bat`。DLL 输出到 `build/Release/trainingmaster.dll`。
可通过 `BUILD_JOBS` 设置并行数。

在 Linux 上安装 CMake、Ninja、Git 和 LLVM MinGW UCRT 工具链，将工具链的
`bin` 目录加入 `PATH`，然后运行 `bash build.sh`。Windows x64 DLL 输出到
`build-linux-cross/trainingmaster.dll`。可通过 `BUILD_JOBS` 设置并行数，或通过
`LLVM_MINGW_ROOT` 指定工具链目录。

## 功能

| 功能 | 说明 |
| --- | --- |
| 播放控制 | 定位、暂停、继续或倒放 |
| 播放倍率 | 将速度调整为 0.25x 至 1.00x，并提供预设按钮 |
| 循环 | 标记 IN 和 OUT 点，持续循环或循环一次 |
| 循环缓冲 | 设置选中循环区域前后增加的时间 |
| 谱面预览 | 查看音符和旋钮轨迹，并编辑循环点 |
| 自动演奏 | 启用游戏原生自动演奏 |
| 谱面流速 | 固定谱面流速 |
| 镜头 | 在训练期间阻止镜头移动 |
| 血条 | 覆盖血条类型和初始值 |

## 许可证

版权所有 © 2026 Takanashi Ryo。项目源代码采用 [GNU 通用公共许可证第 3 版](LICENSE)（仅限 GPL-3.0）

## 第三方组件

- [Dear ImGui](https://github.com/ocornut/imgui)
- [MinHook](https://github.com/tsudakageyu/minhook)

## AI

所有代码均由 AI 编写，逆向工程由 AI 完成
界面由人类设计
