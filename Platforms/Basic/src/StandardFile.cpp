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

#include "StandardFile.hpp"
#include "DebugUtilities.hpp"
#include "Errors.hpp"

namespace Diligent
{

StandardFile::StandardFile(const FileOpenAttribs& OpenAttribs) :
    BasicFile{OpenAttribs},
    m_pFile{nullptr}
{
#if PLATFORM_LINUX || PLATFORM_MACOS || PLATFORM_IOS || PLATFORM_TVOS || PLATFORM_WEB
    String OpenModeStr = GetOpenModeStr();
    m_pFile            = fopen(m_OpenAttribs.strFilePath, OpenModeStr.c_str());
    if (m_pFile == nullptr)
    {
        LOG_ERROR_AND_THROW("Failed to open file ", m_OpenAttribs.strFilePath,
                            "\nThe following error occurred: ", strerror(errno));
    }
#endif
}

StandardFile::~StandardFile()
{
    if (m_pFile)
    {
        fclose(m_pFile);
        m_pFile = nullptr;
    }
}

bool StandardFile::Read(IDataBlob* pData)
{
    VERIFY_EXPR(pData != nullptr);
    size_t FileSize = GetSize();
    pData->Resize(FileSize);
    return Read(pData->GetDataPtr(), pData->GetSize());
}

bool StandardFile::Read(void* Data, size_t Size)
{
    VERIFY(m_pFile, "File is not opened");
    if (!m_pFile)
        return false;
    size_t BytesRead = fread(Data, 1, Size, m_pFile);

    return BytesRead == Size;
}

bool StandardFile::Write(const void* Data, size_t Size)
{
    VERIFY(m_pFile, "File is not opened");
    if (!m_pFile)
        return false;
    size_t BytesWritten = fwrite(Data, 1, Size, m_pFile);

    return BytesWritten == Size;
}

size_t StandardFile::GetSize()
{
    VERIFY(m_pFile, "File is not opened");
    long OrigPos = ftell(m_pFile);
    fseek(m_pFile, 0, SEEK_END);
    long FileSize = ftell(m_pFile);

    fseek(m_pFile, OrigPos, SEEK_SET);
    return FileSize;
}

size_t StandardFile::GetPos()
{
    VERIFY(m_pFile, "File is not opened");
    if (!m_pFile)
        return 0;

    return ftell(m_pFile);
}

bool StandardFile::SetPos(size_t Offset, FilePosOrigin Origin)
{
    VERIFY(m_pFile, "File is not opened");
    if (!m_pFile)
        return false;

    int orig = SEEK_SET;
    switch (Origin)
    {
        // clang-format off
        case FilePosOrigin::Start: orig = SEEK_SET; break;
        case FilePosOrigin::Curr:  orig = SEEK_CUR; break;
        case FilePosOrigin::End:   orig = SEEK_END; break;
        // clang-format on
        default: UNEXPECTED("Unknown origin");
    }

    return fseek(m_pFile, static_cast<long>(Offset), orig) == 0;
}

} // namespace Diligent
