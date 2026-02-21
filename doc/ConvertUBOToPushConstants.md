# ConvertUBOToPushConstants

## Overview

`ConvertUBOToPushConstants()` transforms a Uniform Buffer Object (UBO) in SPIR-V into a Vulkan push constant block. In DiligentCore it is primarily used by the Vulkan backend when a pipeline layout requires a push constant resource, but a shader was compiled with the same data as a UBO.

## API

Declaration: `Graphics/ShaderTools/include/SPIRVTools.hpp`

```cpp
std::vector<uint32_t> ConvertUBOToPushConstants(const std::vector<uint32_t>& SPIRV,
                                                const std::string&           BlockName);
```

- Input: SPIR-V word stream.
- `BlockName`: exact string to match in `OpName`.
- Returns: patched SPIR-V, or an empty vector on failure.

## How the target block is found

The conversion pass scans `OpName` instructions and collects all IDs whose name equals `BlockName`. It then selects the first ID that identifies a UBO in one of two ways:

1. `OpVariable` named `BlockName`
   - variable type is `OpTypePointer Uniform <struct>`
   - pointee struct has `Block` decoration and does **not** have `BufferBlock`

2. `OpTypeStruct` named `BlockName`
   - finds a global `OpVariable` with `OpTypePointer Uniform` to that struct
   - applies the same `Block` / `BufferBlock` check

This allows patching by either the variable instance name (e.g. `cb`) or the struct/block type name (e.g. `CB1`), depending on the compiler.

## What the pass changes

1. Creates or reuses a `OpTypePointer PushConstant <pointee>` for the same struct type.
2. Ensures the pointer type definition appears before its first use in `types_values()` (SSA rule “define before use”), moving the type instruction only when needed.
3. Updates the target `OpVariable`:
   - result type: `OpTypePointer PushConstant ...`
   - storage class operand: `PushConstant`
4. Propagates pointer result types derived from the variable so that all pointer-typed values remain storage-class consistent.
   - handled opcodes include:
     `OpAccessChain`, `OpPtrAccessChain`, `OpInBoundsAccessChain`, `OpInBoundsPtrAccessChain`,
     `OpCopyObject`, `OpPhi`, `OpSelect`, `OpBitcast`, `OpUndef`, `OpConstantNull`
   - unknown pointer-producing patterns stop propagation on that chain and emit a warning.
5. Removes `Binding` and `DescriptorSet` decorations from the converted variable (other layout/block decorations are preserved).

Because the pass mutates types and can move instructions in `types_values()`, it explicitly invalidates type/def-use/decoration analyses before continuing.

## Function calls (OpFunctionCall) and inlining

Propagating storage class through `OpFunctionCall` is not generally safe. To avoid this, `ConvertUBOToPushConstants()` runs these optimizer passes before the custom conversion pass:

- `InlineExhaustivePass` (eliminate function calls)
- `AggressiveDCEPass`
- `EliminateDeadFunctionsPass`

If function calls cannot be fully inlined, the remaining `OpFunctionCall` chains are not rewritten.

## Validation

In `DILIGENT_DEVELOPMENT` builds, the optimizer runs the SPIR-V validator (`options.set_run_validator(true)`), which helps catch missing propagation or SSA issues early.

## Limitations and gotchas

- Requires `OpName`: run this before any “strip reflection/debug names” step that removes names.
- Only targets UBOs (`Uniform` + `Block`); it will not convert SSBOs (`BufferBlock`) or unrelated resources.
- Vulkan supports at most one push constant block per pipeline layout; the Vulkan backend rejects shaders that already contain push constants before attempting conversion.
- Inlining/DCE can increase preprocessing cost and may change IDs; use only when needed.

## Where it is used in DiligentCore

Vulkan PSO creation may patch shaders when a pipeline layout selects a push-constant resource but the shader contains it as a UBO:
`Graphics/GraphicsEngineVulkan/src/PipelineStateVkImpl.cpp`

## Tests

- `Tests/DiligentCoreAPITest/src/Vulkan/ConvertUBOToPushConstantsTestVk.cpp`
  - patches by struct type name (`CB1`)
  - patches by variable name (`cb`)
  - covers GLSLang (GLSL/HLSL) and DXC (HLSL)
