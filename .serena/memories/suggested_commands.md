# DiligentCore 常用命令（Windows）

## 获取源码（含 submodule）
- 克隆：`git clone --recursive https://github.com/DiligentGraphics/DiligentCore.git`
- 若已克隆但缺 submodule：`git submodule update --init --recursive`

## CMake 构建（推荐脚本）
- 一键 Debug x64（VS2022）：`build-x64-Debug.bat`
  - 脚本内部等价于：
    - 配置：`cmake -G "Visual Studio 17 2022" -B build\x64\Debug -A x64 -DCMAKE_INSTALL_PREFIX=install\x64\Debug ...`
    - 构建+安装：`cmake --build build\x64\Debug --config Debug --target install`

## CMake 构建（手动示例）
- 配置：`cmake -S . -B build\x64\Debug -G "Visual Studio 17 2022" -A x64 -DCMAKE_INSTALL_PREFIX=install\x64\Debug -DDILIGENT_BUILD_TESTS=ON -DDILIGENT_DEVELOPMENT=ON -DDILIGENT_NO_FORMAT_VALIDATION=OFF`
- 构建：`cmake --build build\x64\Debug --config Debug`
- 安装：`cmake --build build\x64\Debug --config Debug --target install`

## 格式化/格式校验
- 配置文件：`.clang-format`
- 项目固定版本：`BuildTools/FormatValidation/clang-format_10.0.0.exe`
- Windows 校验脚本：`BuildTools/FormatValidation/validate_format_win.bat`
- CMake 目标（生成后可用）：`DiligentCore-ValidateFormatting`

## .NET / NuGet 打包
- 安装 Python 依赖：`python -m pip install -r ./BuildTools/.NET/requirements.txt`
- 打包示例：`python ./BuildTools/.NET/dotnet-build-package.py -c Debug -d ./`
  - 可选参数：`dotnet-tests`（跑 .NET 测试）、`dotnet-publish`（发布）、`free-memory`（内存不足时）

## 测试
- CMake 侧：先用 `-DDILIGENT_BUILD_TESTS=ON` 生成测试目标后编译。
- 运行：测试可执行文件位于构建目录下的 `Tests\...` 子目录（gtest 参数如 `--gtest_filter=...`）。