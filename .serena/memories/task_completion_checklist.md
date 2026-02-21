# 完成任务后的检查清单（建议）

## 通用
- 确认本次改动覆盖的模块目录：`Common/`、`Graphics/`、`Platforms/`、`Primitives/`、`Tests/` 等。

## 格式
- 本地运行格式校验（与 CI 一致的 clang-format 10.0.0）：
  - `BuildTools/FormatValidation/validate_format_win.bat`
  - 或构建 CMake 目标：`DiligentCore-ValidateFormatting`

## 构建
- 至少用一个常用配置完成编译（Windows 常见为 VS2022 x64 Debug）：
  - `build-x64-Debug.bat`
  - 或手动 `cmake -S . -B build\x64\Debug ...` + `cmake --build ...`

## 测试（若本次改动影响测试相关或核心渲染逻辑）
- 用 `-DDILIGENT_BUILD_TESTS=ON` 构建测试后，运行对应 `Tests\...` 下的 gtest 可执行文件（支持 `--gtest_filter=...`）。
- 若涉及 .NET/NuGet：可在打包脚本中开启 `dotnet-tests`。