/*
 *  Copyright 2019-2025 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#pragma once

#include "PlatformDefinitions.h"

#if PLATFORM_WIN32

#    include "../Win32/interface/Win32FileSystem.hpp"

#elif PLATFORM_UNIVERSAL_WINDOWS

#    include "../UWP/interface/UWPFileSystem.hpp"

#elif PLATFORM_ANDROID

#    include "../Android/interface/AndroidFileSystem.hpp"

#elif PLATFORM_LINUX

#    include "../Linux/interface/LinuxFileSystem.hpp"

#elif PLATFORM_MACOS || PLATFORM_IOS || PLATFORM_TVOS

#    include "../Apple/interface/AppleFileSystem.hpp"

#elif PLATFORM_WEB

#    include "../Emscripten/interface/EmscriptenFileSystem.hpp"

#else
#    error Unknown platform. Please define one of the following macros as 1:  PLATFORM_WIN32, PLATFORM_UNIVERSAL_WINDOWS, PLATFORM_ANDROID, PLATFORM_LINUX, PLATFORM_MACOS, PLATFORM_IOS, PLATFORM_TVOS, PLATFORM_WEB.
#endif

DILIGENT_BEGIN_NAMESPACE(Diligent)

#if PLATFORM_WIN32

using FileSystem = WindowsFileSystem;
using CFile      = WindowsFile;

#elif PLATFORM_UNIVERSAL_WINDOWS

using FileSystem = WindowsStoreFileSystem;
using CFile      = WindowsStoreFile;

#elif PLATFORM_ANDROID

using FileSystem = AndroidFileSystem;
using CFile      = AndroidFile;

#elif PLATFORM_LINUX

using FileSystem = LinuxFileSystem;
using CFile      = LinuxFile;

#elif PLATFORM_MACOS || PLATFORM_IOS || PLATFORM_TVOS

using FileSystem = AppleFileSystem;
using CFile      = AppleFile;

#elif PLATFORM_WEB

using FileSystem = EmscriptenFileSystem;
using CFile      = EmscriptenFile;

#else
#    error Unknown platform. Please define one of the following macros as 1:  PLATFORM_WIN32, PLATFORM_UNIVERSAL_WINDOWS, PLATFORM_ANDROID, PLATFORM_LINUX, PLATFORM_MACOS, PLATFORM_IOS, PLATFORM_TVOS, PLATFORM_WEB.
#endif

DILIGENT_END_NAMESPACE // namespace Diligent
