# DiligentCore Project Guide

## Project Overview

DiligentCore is a modern, cross-platform low-level graphics API that serves as the foundation module of [Diligent Engine](https://github.com/DiligentGraphics/DiligentEngine).

### Supported Rendering Backends
- Direct3D11
- Direct3D12
- OpenGL / OpenGLES
- Vulkan
- WebGPU
- Metal (Commercial version DiligentCorePro)

### Tech Stack
- **Language**: C++
- **Build System**: CMake (Visual Studio generator commonly used on Windows)
- **Testing Framework**: GoogleTest (`ThirdParty/googletest`)
- **Code Formatting**: clang-format 10.0.0 (see `.clang-format`)

## Codebase Structure

```
DiligentCore/
├── Graphics/          # Graphics backend implementations and shader tools
├── Platforms/         # Platform-specific implementations (Win32/UWP/Android/Linux/Apple/Emscripten)
├── Primitives/        # Infrastructure and common interfaces
├── Common/            # Common implementations
├── BuildTools/        # CMake utilities, format validation, .NET packaging tools
├── Tests/             # Unit tests / Integration tests
├── doc/               # Important documentation
├── plan/              # Build plans
└── ThirdParty/        # Third-party dependencies (included as submodules)
```

## Common Commands

### Getting the Source Code
```bash
# Clone (with submodules)
git clone --recursive https://github.com/DiligentGraphics/DiligentCore.git

# If already cloned but missing submodules
git submodule update --init --recursive
```

### CMake Build (Windows)

**One-click build (Recommended)**:
```bash
build-x64-Debug.bat
```

**Manual build**:
```bash
# Configure
cmake -S . -B build\x64\Debug -G "Visual Studio 17 2022" -A x64 \
    -DCMAKE_INSTALL_PREFIX=install\x64\Debug \
    -DDILIGENT_BUILD_TESTS=ON \
    -DDILIGENT_DEVELOPMENT=ON \
    -DDILIGENT_NO_FORMAT_VALIDATION=OFF

# Build
cmake --build build\x64\Debug --config Debug

# Install
cmake --build build\x64\Debug --config Debug --target install
```

### Format Validation
```bash
# Windows validation script
cd BuildTools/FormatValidation

validate_format_win.bat

# Or use CMake target
cmake --build build\x64\Debug --target DiligentCore-ValidateFormatting
```

### Testing

Test executables are located in `build\x64\Debug\Tests\...` subdirectories and support gtest parameters:

```bash
TestExecutable.exe --gtest_filter=TestName*
```

Example for testing InlineConstants:

```bash
cd "\build\x64\Debug\Tests\DiligentCoreAPITest\Debug\"

DiligentCoreAPITest --mode=gl --gtest_filter="DrawCommandTest*:InlineConstants*"

DiligentCoreAPITest --mode=gl --gtest_filter="DrawCommandTest*:InlineConstants*" --non_separable_progs

DiligentCoreAPITest --mode=vk --gtest_filter="DrawCommandTest*:InlineConstants*"
```

### .NET / NuGet Packaging
```bash
python -m pip install -r ./BuildTools/.NET/requirements.txt
python ./BuildTools/.NET/dotnet-build-package.py -c Debug -d ./
```

## Code Style and Conventions

### clang-format
- Configuration file: `.clang-format` (customized based on Microsoft style)
- CI validates formatting; build fails if check doesn't pass
- Temporarily disable formatting: `// clang-format off` / `// clang-format on`

### Header Include Order

**Header files (.hpp)**:
1. System/standard library headers
2. Diligent Engine interface headers
3. Base class implementation headers
4. Object implementation headers
5. Other dependency headers

**Source files (.cpp)**:
1. Precompiled header (pch.h)
2. Corresponding header file for this source file
3. System/standard library headers
4. Interface headers
5. Object implementation headers
6. Other dependency headers

## Important Notes

- Copyright date needs to be updated if we apply changes to source/header files.

## Serena Workflow and Progressive Disclosure

### Serena Memories (Keep Context Lean)

1. Use `list_memories` first to discover available project memories (do not read all memories by default).
2. Use `read_memory` only for the specific memory files required by the current task.
3. If memory content is missing, stale, or insufficient, switch to targeted repository reads or symbol/search-based lookup, then maintain memory quality with `write_memory`, `edit_memory`, or `delete_memory`.

### High-Level Repository Information (Prefer Memories First)

Keep the following topics in Serena memories and load them on demand:

- Project purpose/background: `project_overview`
- Tech stack: `tech_stack`
- Directory and module structure: `project_structure`
- Common development commands: `suggested_commands`
- Code style and conventions: `code_style_conventions`
- Development guidelines and caveats: `development_guidelines`
- Post-task checklist: `task_completion_checklist`
- Module-specific topics (for example, graphics backends or platform layers) as separate memory files when needed

### Source-File Entry Points When Memories Are Insufficient (Read On Demand)

- Project documentation: `README.md`, `ReleaseHistory.md`, `doc/`
- Build and configuration entry points: `CMakeLists.txt`, `build-x64-Debug.bat`, `BuildTools/`, `Directory.Build.props`, `Directory.Packages.props`
- Main implementation modules: `Graphics/`, `Platforms/`, `Primitives/`, `Common/`, `Tests/`
- CI and automation: `.github/workflows/`, `appveyor.yml`
- Large directories (avoid full scans): `ThirdParty/`, `build/`, `media/`

### Progressive Disclosure Key Points

- Start from memories, then narrow to single files/symbols; avoid scanning the whole repository at once.
- For large or dependency-heavy areas, prefer targeted lookup over full-directory reads.

### Startup Rule

- Always run `activate_project` when the agent starts.
