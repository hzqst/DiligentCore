/*
 *  Copyright 2019-2026 Diligent Graphics LLC
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

#include "../../../../Graphics/GraphicsEngine/include/PipelineStateBase.hpp"

#include "TestingEnvironment.hpp"

#include "gtest/gtest.h"

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

class ValidateSpecializationConstantsTest : public ::testing::Test
{
protected:
    static DeviceFeatures GetEnabledFeatures()
    {
        DeviceFeatures Features;
        Features.SpecializationConstants = DEVICE_FEATURE_STATE_ENABLED;
        return Features;
    }

    static DeviceFeatures GetDisabledFeatures()
    {
        DeviceFeatures Features;
        Features.SpecializationConstants = DEVICE_FEATURE_STATE_DISABLED;
        return Features;
    }

    PipelineStateDesc PSODesc{};
    DeviceFeatures    EnabledFeatures  = GetEnabledFeatures();
    DeviceFeatures    DisabledFeatures = GetDisabledFeatures();
};


TEST_F(ValidateSpecializationConstantsTest, NullPointerWithNonZeroCount)
{
    PSODesc.Name = "TestPSO";

    {
        TestingEnvironment::ErrorScope ExpectedErrors{"pSpecializationConstants is null"};
        EXPECT_THROW(
            ValidateSpecializationConstants(PSODesc, EnabledFeatures, 1, nullptr),
            std::runtime_error);
    }

    // Zero count with null pointer should not throw
    EXPECT_NO_THROW(
        ValidateSpecializationConstants(PSODesc, EnabledFeatures, 0, nullptr));
}


TEST_F(ValidateSpecializationConstantsTest, FeatureDisabled)
{
    PSODesc.Name = "TestPSO";

    const float Data = 1.0f;

    SpecializationConstant SpecConsts[] = {
        {"Constant0", SHADER_TYPE_VERTEX, sizeof(Data), &Data},
    };

    TestingEnvironment::ErrorScope ExpectedErrors{"SpecializationConstants device feature is not enabled"};
    EXPECT_THROW(
        ValidateSpecializationConstants(PSODesc, DisabledFeatures, 1, SpecConsts),
        std::runtime_error);
}


TEST_F(ValidateSpecializationConstantsTest, NullName)
{
    PSODesc.Name = "TestPSO";

    const float Data = 1.0f;

    SpecializationConstant SpecConsts[] = {
        {nullptr, SHADER_TYPE_VERTEX, sizeof(Data), &Data},
    };

    TestingEnvironment::ErrorScope ExpectedErrors{"Name must not be null"};
    EXPECT_THROW(
        ValidateSpecializationConstants(PSODesc, EnabledFeatures, 1, SpecConsts),
        std::runtime_error);
}


TEST_F(ValidateSpecializationConstantsTest, EmptyName)
{
    PSODesc.Name = "TestPSO";

    const float Data = 1.0f;

    SpecializationConstant SpecConsts[] = {
        {"", SHADER_TYPE_VERTEX, sizeof(Data), &Data},
    };

    TestingEnvironment::ErrorScope ExpectedErrors{"Name must not be empty"};
    EXPECT_THROW(
        ValidateSpecializationConstants(PSODesc, EnabledFeatures, 1, SpecConsts),
        std::runtime_error);
}


TEST_F(ValidateSpecializationConstantsTest, UnknownShaderStages)
{
    PSODesc.Name = "TestPSO";

    const float Data = 1.0f;

    SpecializationConstant SpecConsts[] = {
        {"Constant0", SHADER_TYPE_UNKNOWN, sizeof(Data), &Data},
    };

    TestingEnvironment::ErrorScope ExpectedErrors{"ShaderStages must not be SHADER_TYPE_UNKNOWN"};
    EXPECT_THROW(
        ValidateSpecializationConstants(PSODesc, EnabledFeatures, 1, SpecConsts),
        std::runtime_error);
}


TEST_F(ValidateSpecializationConstantsTest, ZeroSize)
{
    PSODesc.Name = "TestPSO";

    const float Data = 1.0f;

    SpecializationConstant SpecConsts[] = {
        {"Constant0", SHADER_TYPE_VERTEX, 0, &Data},
    };

    TestingEnvironment::ErrorScope ExpectedErrors{"Size must not be zero"};
    EXPECT_THROW(
        ValidateSpecializationConstants(PSODesc, EnabledFeatures, 1, SpecConsts),
        std::runtime_error);
}


TEST_F(ValidateSpecializationConstantsTest, NullData)
{
    PSODesc.Name = "TestPSO";

    SpecializationConstant SpecConsts[] = {
        {"Constant0", SHADER_TYPE_VERTEX, sizeof(float), nullptr},
    };

    TestingEnvironment::ErrorScope ExpectedErrors{"pData must not be null"};
    EXPECT_THROW(
        ValidateSpecializationConstants(PSODesc, EnabledFeatures, 1, SpecConsts),
        std::runtime_error);
}


TEST_F(ValidateSpecializationConstantsTest, DuplicateNameOverlappingStages)
{
    PSODesc.Name = "TestPSO";

    const float Data = 1.0f;

    SpecializationConstant SpecConsts[] = {
        {"Constant0", SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, sizeof(Data), &Data},
        {"Constant0", SHADER_TYPE_VERTEX | SHADER_TYPE_GEOMETRY, sizeof(Data), &Data},
    };

    TestingEnvironment::ErrorScope ExpectedErrors{"is defined in overlapping shader stages"};
    EXPECT_THROW(
        ValidateSpecializationConstants(PSODesc, EnabledFeatures, 2, SpecConsts),
        std::runtime_error);
}


TEST_F(ValidateSpecializationConstantsTest, DuplicateNameNonOverlappingStages)
{
    PSODesc.Name = "TestPSO";

    const float Data = 1.0f;

    SpecializationConstant SpecConsts[] = {
        {"Constant0", SHADER_TYPE_VERTEX, sizeof(Data), &Data},
        {"Constant0", SHADER_TYPE_PIXEL, sizeof(Data), &Data},
    };

    EXPECT_NO_THROW(
        ValidateSpecializationConstants(PSODesc, EnabledFeatures, 2, SpecConsts));
}


TEST_F(ValidateSpecializationConstantsTest, ValidConstants)
{
    PSODesc.Name = "TestPSO";

    const float FloatData = 1.0f;
    const int   IntData   = 42;

    SpecializationConstant SpecConsts[] = {
        {"FloatConst", SHADER_TYPE_VERTEX, sizeof(FloatData), &FloatData},
        {"IntConst", SHADER_TYPE_PIXEL, sizeof(IntData), &IntData},
    };

    EXPECT_NO_THROW(
        ValidateSpecializationConstants(PSODesc, EnabledFeatures, 2, SpecConsts));
}


TEST_F(ValidateSpecializationConstantsTest, ErrorAtSecondElement)
{
    PSODesc.Name = "TestPSO";

    const float Data = 1.0f;

    // First element is valid, second has null name
    SpecializationConstant SpecConsts[] = {
        {"Constant0", SHADER_TYPE_VERTEX, sizeof(Data), &Data},
        {nullptr, SHADER_TYPE_PIXEL, sizeof(Data), &Data},
    };

    TestingEnvironment::ErrorScope ExpectedErrors{"pSpecializationConstants[1].Name must not be null"};
    EXPECT_THROW(
        ValidateSpecializationConstants(PSODesc, EnabledFeatures, 2, SpecConsts),
        std::runtime_error);
}

} // namespace
