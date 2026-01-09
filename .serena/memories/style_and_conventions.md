# DiligentCore 代码风格与约定

## clang-format
- 项目使用 clang-format 统一代码格式：`.clang-format`（风格基于 Microsoft 并做了大量定制）。
- 为保证一致性，项目文档要求使用 clang-format `10.0.0`：`BuildTools/FormatValidation/clang-format_10.0.0.exe`。
- CI 会校验格式；本地可用：
  - `BuildTools/FormatValidation/validate_format_win.bat`
  - 或 CMake 生成的 `DiligentCore-ValidateFormatting` 目标（见 `BuildTools/CMake/BuildUtils.cmake`）。
- 可用注释临时禁用自动格式化（谨慎使用）：
  - `// clang-format off` / `// clang-format on`

## 构建相关约定
- `CMakeLists.txt` 定义了常用开关：
  - `DILIGENT_BUILD_TESTS`：是否构建测试（默认 OFF）。
  - `DILIGENT_NO_FORMAT_VALIDATION`：是否禁用格式校验（默认 ON；若希望构建阶段校验，需设为 OFF）。
- Windows 上常用 Visual Studio 生成器与 `-A x64` 平台参数（见 `build-x64-Debug.bat`）。

## 命名/接口习惯（从 README/API 片段可见）
- C++ API 常以 `Diligent` 命名空间组织；引擎初始化通过各后端的 `IEngineFactory*` 进行（D3D11/D3D12/GL/Vk/WebGPU 等）。