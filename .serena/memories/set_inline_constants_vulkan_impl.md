# SetInlineConstants（Vulkan 后端实现要点）

> 该记忆用于快速回答“Vulkan Inline Constants 当前到底怎么实现”的问题；更细节见 `vulkan_inline_constants.md` 与 `doc/SetInlineConstantsVkBuffer.md`。

## 当前实现的核心策略
- PRS 阶段“统一资源形态”：所有 `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS` 资源都被当作常量缓冲来分配 descriptor set/binding，并为每个资源创建一个 **Signature 级共享**的 `USAGE_DYNAMIC` UBO（通过 `PipelineResourceSignatureBase::CreateInlineConstantBuffer()`）。
- PSO/PipelineLayout 阶段只选择一个 push constant：Vulkan pipeline layout 只能有一段 push constant range，因此 Diligent 从 pipeline 使用的所有 resource signatures 中找到 **第一个** inline-constant 资源作为 push constants，其余资源保持为 UBO 模拟。
- SRB 阶段只写 CPU staging：`SetInlineConstants()` 只把数据写到 `ShaderResourceCacheVk::Resource::pInlineConstantData` 指向的 staging 内存。
- Draw/Dispatch 前提交：在 `vkCmdBindDescriptorSets` 之前先提交 inline constants：
  - 若命中被选中的资源：`vkCmdPushConstants`
  - 否则：`MapBuffer(DISCARD)` + `memcpy` + `UnmapBuffer` 更新共享 UBO；随后 descriptor sets 绑定会使用正确的 dynamic offsets。

## 为什么必须“先提交 inline constants，再绑定 descriptor sets”
- Vulkan 模拟路径使用 `USAGE_DYNAMIC` 统一缓冲，`Map(..., DISCARD)` 会产生新的动态分配偏移；若先绑定 descriptor sets，会导致 dynamic offsets 指向旧分配。

## 关键代码入口（最常用定位）
- push constant 选择：`Graphics/GraphicsEngineVulkan/src/PipelineLayoutVk.cpp` (`PipelineLayoutVk::GetPushConstantInfo`)。
- 提交时序：`Graphics/GraphicsEngineVulkan/src/DeviceContextVkImpl.cpp`（`PrepareForDraw` 先 `CommitInlineConstants`，再 `CommitDescriptorSets`）。
- 提交实现：`Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp`（`CommitInlineConstants`）。
- 写 staging：`Graphics/GraphicsEngineVulkan/src/ShaderVariableManagerVk.cpp` → `Graphics/GraphicsEngineVulkan/src/ShaderResourceCacheVk.cpp` (`SetInlineConstants`)。
- cache staging 分配：`Graphics/GraphicsEngineVulkan/src/ShaderResourceCacheVk.cpp`（`InitializeSets/InitializeResources`，内存尾部是 inline-constant 值数组）。