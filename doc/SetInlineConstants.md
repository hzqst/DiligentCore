# SetInlineConstants Usage Guide

This document is written for **Diligent API users**. It explains how to define and use inline constants via `IShaderResourceVariable::SetInlineConstants()` and what to watch out for when using them across backends.

Backend-specific submission flow (CPU -> staging -> GPU) is documented here:
- Vulkan: `doc/SetInlineConstantsVulkan.md`
- D3D11: `doc/SetInlineConstantsD3D11.md`

---

## 1. What are inline constants?

Inline constants provide a low-friction way to pass a **small amount of frequently-updated 32-bit data** from CPU to shaders without managing dedicated constant buffer objects for every update.

Typical use cases:
- Per-draw/per-dispatch parameters (IDs, small structs, flags)
- Data that changes very often and should be cheap to update

Non-goals / not recommended:
- Large data blocks
- Infrequently updated data that fits well into regular constant buffers

---

## 2. How to declare inline constants (Pipeline Resource Signature)

Inline constants are declared as a constant-buffer resource (`SHADER_RESOURCE_TYPE_CONSTANT_BUFFER`) with `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS`.

```cpp
PipelineResourceDesc Resources[] =
{
    // Note: ArraySize is the number of 32-bit values (not bytes).
    {SHADER_TYPE_VERTEX, "Constants", 16, SHADER_RESOURCE_TYPE_CONSTANT_BUFFER,
     SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC, PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS},
};

PipelineResourceSignatureDesc PRSDesc;
PRSDesc.Resources    = Resources;
PRSDesc.NumResources = _countof(Resources);

RefCntAutoPtr<IPipelineResourceSignature> pPRS;
pDevice->CreatePipelineResourceSignature(PRSDesc, &pPRS);
```

Important semantics:
- `PipelineResourceDesc::ArraySize` is the **count of 4-byte (32-bit) constants**.
- For inline constants, `PipelineResourceDesc::GetArraySize()` returns `1` (descriptor array size is always 1), but the data payload size is still `ArraySize * sizeof(Uint32)`.

---

## 3. Shader-side expectations

From the shader’s point of view, inline constants map to a constant-buffer-like resource with the same name. Keep your shader declaration consistent with the size/layout implied by `ArraySize`.

Practical tips:
- Treat the payload as an array of 32-bit words for cross-backend consistency.
- Keep the layout stable; avoid backend-dependent packing assumptions.

### Vulkan (SPIR-V) special case: push-constant blocks

If a Vulkan shader’s SPIR-V contains a push-constant block (storage class `PushConstant`, often a `PushConstantsBlock`), Diligent reflects it as a constant-buffer-like resource with `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS` (i.e., it is treated as inline constants by default).

In practice, this means you should declare a matching inline-constant resource in your PRS and update it via `SetInlineConstants()` just like any other inline constants variable.

Implementation reference:
- `Graphics/ShaderTools/src/SPIRVShaderResources.cpp` (`ResourceType::PushConstant` -> `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS`)

---

## 4. Writing values at runtime (`SetInlineConstants`)

At runtime, you update inline constants through a shader resource variable:

```cpp
auto* pVar = pSRB->GetVariableByName(SHADER_TYPE_VERTEX, "Constants");

Uint32 Data[16] = {};
pVar->SetInlineConstants(Data, /*FirstConstant*/ 0, /*NumConstants*/ 16);
```

### 4.1 STATIC inline constants example (initialize via PRS)

If the inline-constants resource is declared as `STATIC`, initialize it through the pipeline resource signature and create the SRB with `InitStaticResources=true`:

```cpp
Uint32 InitData[16] = {};
pPRS->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")
    ->SetInlineConstants(InitData, /*FirstConstant*/ 0, /*NumConstants*/ 16);

RefCntAutoPtr<IShaderResourceBinding> pSRB;
pPRS->CreateShaderResourceBinding(&pSRB, /*InitStaticResources*/ true);
```

Note: updates to the signature’s static cache after SRB creation do not affect existing SRBs; use the SRB variable for per-frame/per-draw updates.

### 4.2 The most common mistake: units are 32-bit constants

- `FirstConstant` and `NumConstants` are in **32-bit constant indices**, not bytes.
- `pConstants` points to the source array of **32-bit values**.

Example: update constants 4..7 (four 32-bit values):

```cpp
Uint32 Data4[4] = {/*c4..c7*/};
pVar->SetInlineConstants(Data4, /*FirstConstant*/ 4, /*NumConstants*/ 4);
```

---

## 5. STATIC vs MUTABLE/DYNAMIC (avoid "I updated it but nothing changed")

Inline constants follow the same PRS/SRB caching rules as other resources.

### STATIC variables

- `pPRS->GetStaticVariableByName(...)->SetInlineConstants(...)` updates the **signature’s static cache**.
- The static cache is copied into an SRB only during SRB creation when `InitStaticResources=true`.
- Updating the signature’s static cache after an SRB is created does **not** retroactively update existing SRBs.

### MUTABLE / DYNAMIC variables

- `pSRB->GetVariableByName(...)->SetInlineConstants(...)` updates the **SRB’s own cache**.
- Use this for per-frame/per-draw updates.

Recommended rule of thumb:
- Initialization-only values: `STATIC`
- Frequently updated values: `DYNAMIC` (or `MUTABLE`)

---

## 6. Limits and validity rules

- Resource type: only `SHADER_RESOURCE_TYPE_CONSTANT_BUFFER` can use `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS`.
- Flag exclusivity: `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS` cannot be combined with other resource flags.
- Size: `ArraySize <= MAX_INLINE_CONSTANTS` (currently 64, i.e. 256 bytes).
- Vulkan PSO creation: if the inline constants promoted to push constants exceed `VkPhysicalDeviceLimits::maxPushConstantsSize`, or the shader already defines incompatible push constants, PSO creation may fail.

---

## 7. Vulkan-specific note: only one push-constant block per pipeline layout

Vulkan allows only a single push constant range in a pipeline layout. Diligent may therefore promote only one inline-constant resource to the push-constant path; other inline constants fall back to a buffer-based path.

If Vulkan performance matters:
- Put the most performance-critical inline constants first (signature binding order + resource order decide which one gets promoted).
- Ensure the promoted block size does not exceed `VkPhysicalDeviceLimits::maxPushConstantsSize`, or PSO creation will fail.

Details: `doc/SetInlineConstantsVulkan.md`.

---

## 8. Skipping redundant work: `DRAW_FLAG_INLINE_CONSTANTS_INTACT`

If you know that inline constants used by the draw/dispatch have not changed since the last submission, pass `DRAW_FLAG_INLINE_CONSTANTS_INTACT` to let backends skip the commit work.

This is particularly beneficial for backends that emulate inline constants through dynamic buffers (e.g., D3D11 and Vulkan emulation path).

---

## 9. Troubleshooting checklist

- Wrong values: confirm that `FirstConstant`/`NumConstants` are **32-bit indices**, not bytes.
- Out of range / validation failure: confirm `ArraySize <= MAX_INLINE_CONSTANTS`.
