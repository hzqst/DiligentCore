# DiligentCore 项目概览

## 项目用途
- `DiligentCore` 是一个现代、跨平台的低层图形 API，实现了多个渲染后端，作为 `Diligent Engine` 的基础模块。
- 支持后端：Direct3D11、Direct3D12、OpenGL/OpenGLES、Vulkan、WebGPU；Metal 后端在本仓库中提示为商业版本（`DiligentCorePro`）。
- 模块自包含，可独立于整个引擎单独构建。

## 技术栈
- 语言：C++（项目激活时检测为 `cpp`）。
- 构建系统：CMake（根目录 `CMakeLists.txt`），Windows 常用生成器为 Visual Studio。
- 测试：GoogleTest（`ThirdParty/googletest` 与 `Tests/` CMake 子项目）。
- 格式化：clang-format（见 `doc/code_formatting.md` 与 `.clang-format`）。
- 额外：提供 .NET/NuGet 打包脚本与 .NET 测试（见 `BuildTools/.NET`、`Graphics/GraphicsEngine.NET`、`Tests/DiligentCoreTest.NET`）。

## 代码库结构（粗略）
- `Graphics/`：各图形后端与相关工具、Shader 工具等。
- `Platforms/`：平台相关实现（Win32/UWP/Android/Linux/Apple/Emscripten 等）。
- `Primitives/`：基础设施与通用接口/工具。
- `Common/`：通用实现。
- `BuildTools/`：CMake 辅助、格式校验、.NET 打包等工具。
- `Tests/`：单元测试/集成测试、测试框架。
- `ThirdParty/`：第三方依赖（大量以 submodule 方式引入）。

## 入口与产物
- 本仓库主要产物是库（静态/动态取决于平台与构建配置）。
- 若开启 `DILIGENT_BUILD_TESTS`，会生成 `Tests/` 下的测试可执行文件（gtest 风格，可用 `--gtest_filter=...`）。
- .NET/NuGet 产物由 `BuildTools/.NET/dotnet-build-package.py` 生成。