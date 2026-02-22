# Specialization Constants Support Plan

## Goal
Add support for pipeline specialization constants in DiligentCore for issue #760.

Backend scope:
- Vulkan: supported via `VkSpecializationInfo`.
- WebGPU: supported via pipeline-overridable constants (`WGPUConstantEntry`).
- D3D11 / D3D12 / OpenGL / Metal: not supported.

This plan is intentionally split into three major stages.

---

## Stage 1 - API and feature plumbing in DiligentCore API

### Objective
Introduce a backend-agnostic API surface for specialization constants and add feature gating so unsupported backends fail fast.

### Work items
1. **Public API type additions**
   - Add `SpecializationConstant` in `Graphics/GraphicsEngine/interface/PipelineState.h`:
     - `const char* Name`
     - `SHADER_TYPE ShaderStages`
     - `Uint32 Size`
     - `const void* pData`
   - Extend `PipelineStateCreateInfo` with:
     - `const SpecializationConstant* pSpecializationConstants`
     - `Uint32 NumSpecializationConstants`
   - Add `SpecializationConstants` to `DeviceFeatures` in `Graphics/GraphicsEngine/interface/GraphicsTypes.h`.
   - Add `SpecializationConstants` to `ENUMERATE_DEVICE_FEATURES` and constructor initialization paths.
   - Update all `ASSERT_SIZEOF(DeviceFeatures, 47, ...)` call sites to the new size.
   - Wire feature enable/selection logic in `Graphics/GraphicsEngine/src/RenderDeviceBase.cpp` (`EnableDeviceFeatures`).
   
2. **API validation and behavior contract**
   - Add common validation in pipeline create-info validation path (`PipelineStateBase` validation):
     - pointer/count consistency (`Num == 0` <=> pointer may be null, `Num > 0` requires pointer non-null)
     - every entry must have valid `Name`, `ShaderStages`, `Size`, `pData`
     - reject duplicate `(Name, ShaderStages overlap)` in one PSO create info
     - reject usage when `DeviceFeatures.SpecializationConstants == DEVICE_FEATURE_STATE_DISABLED`
   - Keep behavior deterministic across all PSO types (graphics/compute/ray tracing/tile where applicable).

3. **Backend feature states**
   - Vulkan: set `Features.SpecializationConstants = ENABLED`.
   - WebGPU: set `Features.SpecializationConstants = ENABLED`.
   - D3D11 / D3D12 / OpenGL / Metal: set `DISABLED`.
   - Update backend feature-population files (not exhaustive):
     - `Graphics/GraphicsEngineVulkan/src/VulkanTypeConversions.cpp`
     - `Graphics/GraphicsEngineWebGPU/src/EngineFactoryWebGPU.cpp`
     - `Graphics/GraphicsEngineD3D11/src/EngineFactoryD3D11.cpp`
     - `Graphics/GraphicsEngineD3D12/src/EngineFactoryD3D12.cpp`
     - `Graphics/GraphicsEngineOpenGL/src/RenderDeviceGLImpl.cpp`
     - `Graphics/GraphicsEngineVulkan/src/EngineFactoryVk.cpp` (size/assert sync)

4. **Create-info lifetime, copy, and serialization**
   - Extend `PipelineStateCreateInfoX` wrapper in `Graphics/GraphicsEngine/interface/GraphicsTypesX.hpp` to deep-copy specialization constants (names + raw bytes) for async PSO creation safety.
   - Extend PSO serialization/deserialization for `PipelineStateCreateInfo`:
     - `Graphics/GraphicsEngine/src/PSOSerializer.cpp`
     - include per-entry `{Name, ShaderStages, Size, DataBytes}`.
   - Update serialization size asserts and tests.

5. **Tests for stage 1**
   - Update `Tests/DiligentCoreTest/src/GraphicsEngine/PSOSerializerTest.cpp` for round-trip with non-empty specialization constants.
   - Add/extend API validation tests (invalid pointer/count/name/size/data, unsupported backend path).

### Exit criteria
- API compiles for all backends.
- Device feature reports are correct by backend.
- Serialization round-trip preserves specialization constant payload.
- Unsupported backends reject non-empty specialization constants with clear errors.

---

## Stage 2 - Vulkan backend implementation

### Objective
Implement full Vulkan specialization constants path by mapping names to SPIR-V `SpecId` and binding values into `VkPipelineShaderStageCreateInfo`.

### Work items
1. **SPIR-V reflection extension (required)**
   - Extend `SPIRVShaderResources` to expose specialization constants metadata:
     - constant name (`OpName`)
     - `SpecId`
     - scalar type / byte size metadata for validation
   - Candidate files:
     - `Graphics/ShaderTools/include/SPIRVShaderResources.hpp`
     - `Graphics/ShaderTools/src/SPIRVShaderResources.cpp`
   - Use SPIRV-Cross specialization constant reflection (`get_specialization_constants` + name lookup).

2. **Name -> SpecId matching policy**
   - Match `SpecializationConstant::Name` to reflected specialization constant names from SPIR-V.
   - Scope matching by shader module and stage mask (`ShaderStages`).
   - Validate uniqueness and type/size compatibility.
   - Define clear error messages for:
     - name not found in target shader module
     - duplicate bindings
     - size/type mismatch

3. **Populate Vulkan structs during PSO creation**
   - In Vulkan pipeline state creation flow, build per-shader-module specialization data:
     - `std::vector<VkSpecializationMapEntry>`
     - contiguous data blob (`std::vector<Uint8>`)
     - `VkSpecializationInfo`
   - Attach `StageCI.pSpecializationInfo` for each `VkPipelineShaderStageCreateInfo`.
   - Ensure memory lifetime is valid until Vulkan `vkCreate*Pipelines` call returns.
   - Primary integration file:
     - `Graphics/GraphicsEngineVulkan/src/PipelineStateVkImpl.cpp`

4. **Pipeline coverage**
   - Cover all Vulkan PSO paths that use shader stage create infos (graphics/compute/ray tracing as applicable).

5. **Tests for stage 2**
   - Add Vulkan API tests with known specialization constants:
     - graphics path and compute path minimum
     - positive test: output changes when constants change
     - negative test: unknown name / wrong size
   - Reuse existing test infrastructure patterns from inline-constants and Vulkan-specific test suites.

### Exit criteria
- Vulkan pipelines consume specialization constants correctly.
- `Name` to `SpecId` mapping is reflection-driven (via `OpName`) and validated.
- Vulkan tests pass for both valid and invalid cases.

---

## Stage 3 - WebGPU backend implementation

### Objective
Implement specialization constants in WebGPU by populating pipeline overridable constants during pipeline creation.

### Work items
1. **Pipeline descriptor wiring**
   - Populate WebGPU stage constant arrays using `WGPUConstantEntry`:
     - compute pipeline: `wgpuComputePipelineDesc.compute.constants/constantCount`
     - render pipeline: vertex/fragment stage constants (API-version dependent fields in current Dawn headers)
   - Integration file:
     - `Graphics/GraphicsEngineWebGPU/src/PipelineStateWebGPUImpl.cpp`

2. **Name and type validation policy**
   - Match `SpecializationConstant::Name` against shader stage overridable constant names.
   - Validate supported payload types and size conversion into WebGPU numeric value (`double` in `WGPUConstantEntry`).
   - Decide and document bool/int/uint/float conversion rules and range checks.

3. **WGSL reflection support (if required by current code path)**
   - If existing shader metadata does not expose override constants, extend WGSL reflection container to include them.
   - Candidate files:
     - `Graphics/ShaderTools/include/WGSLShaderResources.hpp`
     - `Graphics/ShaderTools/src/WGSLShaderResources.cpp`

4. **Async pipeline path parity**
   - Ensure both sync and async pipeline creation paths consume the same specialization constant data.

5. **Tests for stage 3**
   - Add WebGPU tests with WGSL `override` constants:
     - positive path: constants affect shader behavior
     - negative path: invalid name / incompatible size or type
   - Verify feature report (`DeviceFeatures.SpecializationConstants`) and unsupported backend behavior remain unchanged.

### Exit criteria
- WebGPU pipeline overridable constants are correctly bound from API inputs.
- Validation behavior matches Vulkan/API contract where applicable.
- WebGPU tests pass in both sync and async creation paths.

---

## Cross-stage verification checklist
- Build all enabled backends after each stage.
- Run serializer/unit tests after Stage 1.
- Run Vulkan/WebGPU API tests after Stage 2/3.
- Confirm no regressions in existing inline-constants tests.
- Confirm render state cache / PSO archive paths remain deterministic with specialization constants present.

## Suggested implementation order
1. Complete Stage 1 fully (unblocks feature gating and stable API contract).
2. Implement Stage 2 (Vulkan), including reflection extension.
3. Implement Stage 3 (WebGPU), reusing Stage 1 validation rules and test patterns.
