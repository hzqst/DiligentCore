# Specialization Constants (WebGPU) Detailed Implementation Plan

## Scope
- Implement WebGPU backend support for `PipelineStateCreateInfo::{NumSpecializationConstants, pSpecializationConstants}`.
- Cover both compute and graphics PSO creation paths.
- Keep behavior aligned with current API contract and existing Vulkan implementation where practical.
- Include sync and async PSO creation parity.

## Current Baseline (from code)
- Stage 1 API plumbing is already in tree (`PipelineState.h`, `PipelineStateBase.cpp`, `GraphicsTypesX.hpp`, serializer/hash/tests).
- Vulkan backend path is implemented in `Graphics/GraphicsEngineVulkan/src/PipelineStateVkImpl.cpp` (`BuildSpecializationData(...)`).
- WebGPU currently reports `Features.SpecializationConstants = DEVICE_FEATURE_STATE_DISABLED` in `Graphics/GraphicsEngineWebGPU/src/EngineFactoryWebGPU.cpp`.
- WebGPU backend already uses `WGPUConstantEntry` in a non-PSO helper (`Graphics/GraphicsEngineWebGPU/src/GenerateMipsHelperWebGPU.cpp`), so compute-stage constant entry wiring is known-good in this codebase.

## Design Goals
1. Name/stage matching from user `SpecializationConstant` input to WGSL `override` constants.
2. Reflection-driven type/size validation before pipeline creation.
3. Correct `WGPUConstantEntry` wiring for:
   - `WGPUComputePipelineDescriptor.compute`
   - `WGPURenderPipelineDescriptor.vertex`
   - `WGPUFragmentState` (fragment stage)
4. No lifetime bugs in asynchronous PSO path.
5. Targeted tests for reflection, positive rendering/compute behavior, and invalid input behavior.

## Non-goals
- No public API change (Stage 1 already added API surface).
- No WebGPU ray tracing/tile specialization handling.
- No backend-wide redesign of shader reflection storage beyond what is needed for override constants.

---

## Candidate Files and Symbols

### 1) WebGPU feature state
- **File:** `Graphics/GraphicsEngineWebGPU/src/EngineFactoryWebGPU.cpp`
- **Symbol:** `DeviceFeatures GetSupportedFeatures(WGPUAdapter wgpuAdapter, WGPUDevice wgpuDevice = nullptr)`
- **Candidate change:**
  - Flip `Features.SpecializationConstants` from `DEVICE_FEATURE_STATE_DISABLED` to `DEVICE_FEATURE_STATE_ENABLED` after backend path is in place.

### 2) WGSL reflection extension (override constants)
- **Files:**
  - `Graphics/ShaderTools/include/WGSLShaderResources.hpp`
  - `Graphics/ShaderTools/src/WGSLShaderResources.cpp`
- **Existing symbols to extend:**
  - `class WGSLShaderResources`
  - `WGSLShaderResources::WGSLShaderResources(...)`
  - `WGSLShaderResources::Initialize(...)`
  - `WGSLShaderResources::~WGSLShaderResources()`
  - `WGSLShaderResources::DumpResources()`
- **Candidate new symbols:**
  - `struct WGSLSpecializationConstantAttribs` (name, scalar basic type, byte size)
  - `Uint32 GetNumSpecConstants() const`
  - `const WGSLSpecializationConstantAttribs& GetSpecConstant(Uint32 n) const`
- **Candidate change details:**
  - Parse WGSL override constants for the selected entry point.
  - Store reflection metadata needed by PSO creation: constant name, scalar type, size.
  - Keep memory ownership consistent with existing WGSL reflection patterns (resource name pool + compact metadata storage).

### 3) WebGPU PSO creation wiring
- **Files:**
  - `Graphics/GraphicsEngineWebGPU/include/PipelineStateWebGPUImpl.hpp`
  - `Graphics/GraphicsEngineWebGPU/src/PipelineStateWebGPUImpl.cpp`
- **Existing symbols to modify:**
  - `PipelineStateWebGPUImpl::InitializePipeline(const GraphicsPipelineStateCreateInfo& CreateInfo)`
  - `PipelineStateWebGPUImpl::InitializePipeline(const ComputePipelineStateCreateInfo& CreateInfo)`
  - `PipelineStateWebGPUImpl::InitializeWebGPURenderPipeline(...)`
  - `PipelineStateWebGPUImpl::InitializeWebGPUComputePipeline(...)`
  - `struct PipelineStateWebGPUImpl::AsyncPipelineBuilder`
- **Candidate new symbols:**
  - `struct ShaderStageSpecializationDataWebGPU` (per-stage `WGPUConstantEntry` storage)
  - `BuildSpecializationDataWebGPU(...)` helper in anonymous namespace or private static method
  - `ConvertSpecConstantToDouble(...)` helper
- **Candidate change details:**
  - Build per-stage constant entries from:
    - user input (`SpecializationConstant`)
    - shader reflection (`WGSLShaderResources` override metadata)
  - Assign stage descriptors:
    - `wgpuComputePipelineDesc.compute.constants / constantCount`
    - `wgpuRenderPipelineDesc.vertex.constants / constantCount` (or API-version equivalent)
    - `wgpuFragmentState.constants / constantCount` (or API-version equivalent)
  - Ensure data lifetime for async path by storing specialization payload in `AsyncPipelineBuilder` (not in temporary create-info memory).

### 4) Tests
- **Reflection unit tests**
  - `Tests/DiligentCoreTest/src/ShaderTools/WGSLShaderResourcesTest.cpp`
  - (optional assets) `Tests/DiligentCoreTest/assets/shaders/WGSL/*`
- **API tests**
  - `Tests/DiligentCoreAPITest/src/SpecializationConstantsTest.cpp`
  - `Tests/DiligentCoreAPITest/src/ObjectCreationFailure/PSOCreationFailureTest.cpp`
  - (optional async stress) `Tests/DiligentCoreAPITest/src/AsyncShaderCompilationTest.cpp`

---

## Matching and Validation Policy (WebGPU)

### Proposed policy
1. For each shader stage in PSO, iterate reflected WGSL override constants.
2. For each reflected constant:
   - Find first user `SpecializationConstant` with:
     - `Name` equal
     - `ShaderStages` overlapping current stage
3. If user entry is not found:
   - Skip (shader default override value remains active).
4. If user entry is found:
   - Validate data size is sufficient for reflected type (`user.Size >= reflected.Size`).
   - Convert raw value to `double` using reflected scalar type.
   - Emit one `WGPUConstantEntry`.

### Error cases (backend-level)
- Reflected constant matched by name but insufficient `Size`.
- Reflected constant type unsupported by conversion helper.
- Malformed reflection data (unexpected scalar type/size).
- Descriptor field mismatch due WebGPU C API version differences.

### Behavior alignment notes
- Unmatched user constants are skipped (same behavior as current Vulkan path).
- API-level validation (`null Name`, `Size == 0`, duplicate overlapping stages, feature disabled, etc.) stays in `PipelineStateBase.cpp`.

---

## Detailed Implementation Steps

### Step 0: WebGPU descriptor compatibility guard
- Confirm exact stage constant fields available in current WebGPU headers for render stages.
- If field names differ across API versions, add a small compatibility helper in `PipelineStateWebGPUImpl.cpp` so descriptor wiring is centralized.

### Step 1: Add WGSL override constant reflection
- Extend `WGSLShaderResources` metadata:
  - add specialization constant count/getter APIs
  - add per-constant attributes (name, `SHADER_CODE_BASIC_TYPE`, byte size)
- In `WGSLShaderResources::WGSLShaderResources(...)`:
  - after parsing entry point and resources, collect override constants for that entry point.
  - map WGSL scalar type to `SHADER_CODE_BASIC_TYPE` and fixed byte size.
- Keep name lifetime stable via existing string pool strategy.
- Update destructor and dump output to include new metadata where useful.

### Step 2: Build per-stage specialization payload for WebGPU PSO
- In `PipelineStateWebGPUImpl.cpp`, add helper:
  - input: `TShaderStages`, `NumSpecializationConstants`, `pSpecializationConstants`, `PipelineStateDesc`
  - output: vector indexed by stage in `ShaderStages`, each with `WGPUConstantEntry` array
- Conversion helper should decode user raw bytes by reflected scalar type:
  - `bool` -> `0.0` or `1.0`
  - `int32` -> `double`
  - `uint32` -> `double`
  - `float32` -> `double`
  - `float16` -> `double` (if implemented in this step), else explicit error until supported

### Step 3: Wire descriptor constants for compute and graphics
- Compute path (`InitializeWebGPUComputePipeline`):
  - assign `compute.constants` and `compute.constantCount`.
- Graphics path (`InitializeWebGPURenderPipeline`):
  - assign vertex stage constants on `wgpuRenderPipelineDesc.vertex`.
  - assign fragment stage constants on `wgpuFragmentState`.
- Keep entries alive through create call by owning vectors in function scope for sync and in async builder storage for async.

### Step 4: Async path parity and lifetime fix
- Extend `AsyncPipelineBuilder` with specialization payload storage.
- Build specialization payload in `InitializePipeline(CreateInfo)` while create info is still valid.
- Pass payload to sync create immediately, or move payload into async builder for deferred create in `GetStatus()`.
- Avoid reading user `pSpecializationConstants` from transient memory after `InitializePipeline(...)` returns.

### Step 5: Enable WebGPU feature bit
- Change `EngineFactoryWebGPU.cpp` feature reporting:
  - `Features.SpecializationConstants = DEVICE_FEATURE_STATE_ENABLED`.
- Do this with backend implementation in the same change window to avoid feature-contract mismatch.

### Step 6: Tests

#### 6.1 ShaderTools reflection tests
- Add WGSL override reflection test(s) in `WGSLShaderResourcesTest.cpp`.
- Verify:
  - reflected constant count
  - name/type/size metadata for each override constant
  - entry-point specific behavior

#### 6.2 APITest positive behavior
- Update `SpecializationConstantsTest.cpp` to include WebGPU path:
  - use WGSL shaders with `override` declarations for WebGPU device
  - keep existing Vulkan GLSL path unchanged
- Cover:
  - compute path output changes with constants
  - graphics path output changes with constants
  - cross-stage same-name matching (VS+PS)

#### 6.3 APITest negative behavior
- Extend `PSOCreationFailureTest.cpp` with WebGPU-relevant negative cases:
  - insufficient size for matched override constant
  - invalid type conversion cases (if backend enforces strict type support)
  - unmatched constant name skip behavior (if policy remains silent skip)

#### 6.4 Async behavior regression
- Add a focused async specialization PSO test (new test or extension):
  - create PSO with `PSO_CREATE_FLAG_ASYNCHRONOUS`
  - ensure specialization payload still applied after delayed completion
  - ensure no lifetime issues with stack-allocated input constant data

---

## Candidate Commit Slices

1. **Reflection layer**
   - WGSL specialization metadata + unit tests.
2. **WebGPU PSO wiring**
   - Build/match/convert constants + descriptor assignment + async payload ownership.
3. **Feature bit + API tests**
   - Enable feature flag in WebGPU factory and land APITest coverage.

---

## Verification Checklist

### Build
- Build WebGPU-enabled targets and both test binaries.

### Unit/API tests
- `DiligentCoreTest`:
  - WGSL reflection tests including new specialization cases.
- `DiligentCoreAPITest --mode=wgpu`:
  - `SpecializationConstants.*`
  - specialization-related `PSOCreationFailureTest` cases.
- Regression sanity:
  - `DiligentCoreAPITest --mode=vk --gtest_filter="SpecializationConstants.*"`

### Runtime behavior
- WebGPU device reports `Features.SpecializationConstants == ENABLED`.
- Pipelines with valid specialization constants create successfully and affect output.
- Invalid inputs fail with clear backend error messages.
- Async pipeline creation preserves specialization data correctly.

---

## Exit Criteria
- WebGPU backend fully consumes `SpecializationConstant` input for compute and graphics PSOs.
- Reflection-driven name/type/size matching exists for WGSL override constants.
- Sync and async pipeline creation paths both apply specialization constants correctly.
- Tests cover reflection, positive behavior, and critical failure paths.
