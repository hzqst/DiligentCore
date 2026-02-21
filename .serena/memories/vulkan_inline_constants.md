# Vulkan Inline Constants（重新分析，含 Buffer-Emulated 路径）

## 一句话结论
在 Vulkan 后端，Diligent 将 **所有** `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS` 资源在 PRS（Pipeline Resource Signature）层面统一当作“带 descriptor binding 的常量缓冲”，并为其创建 **Signature 级共享 `USAGE_DYNAMIC` UBO** 与 **SRB 级 CPU staging 存储**；而在 PSO/PipelineLayout 创建时仅挑选 **一个** inline-constant 资源走 **push constants**，其余资源走 **动态 UBO（VkBuffer + dynamic offset）模拟**。

## 关键限制/语义
- 仅 `SHADER_RESOURCE_TYPE_CONSTANT_BUFFER` 允许 `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS`。
- `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS` **不能与其他 flag 组合**（因此在 Vulkan 中不会显式开启 `NO_DYNAMIC_BUFFERS`）。
- `PipelineResourceDesc::ArraySize` 对 inline constants 表示“4 字节常量个数”；`GetArraySize()` 会返回 1（descriptor 数组维度）。
- SPIR-V 侧：`SPIRVShaderResourceAttribs::ResourceType::PushConstant` 会映射为 `SHADER_RESOURCE_TYPE_CONSTANT_BUFFER + INLINE_CONSTANTS`（`Graphics/ShaderTools/src/SPIRVShaderResources.cpp`）。

## Push constant 选择（PSO/PipelineLayout 阶段）
- Vulkan pipeline layout 只允许一段 push constant range。
- Diligent 策略：从 **按 binding index 排序**的签名中，找到**第一个**带 `INLINE_CONSTANTS` 的资源作为 push constant（`Graphics/GraphicsEngineVulkan/src/PipelineLayoutVk.cpp` 的 `PipelineLayoutVk::GetPushConstantInfo()`）。
- 大小：`ResDesc.ArraySize * sizeof(Uint32)`；stageFlags：`ResDesc.ShaderStages`；offset 固定 0。
- 若超过 `maxPushConstantsSize`，PSO 创建直接失败。
- PSO 创建时会尝试把选中的 UBO 转换为 push constant：
  - 若 shader 里不存在同名 push constant，但存在同名 uniform buffer，则会对 SPIR-V 做转换（`Graphics/GraphicsEngineVulkan/src/PipelineStateVkImpl.cpp` 相关逻辑）。
  - 若 shader 已经有 push constant，则不能再转换 UBO（会报错）。

## Buffer-Emulated 路径（PRS/SRB/运行时）
### 1) PRS 创建：为每个 inline-constant 资源创建共享 Buffer
- `PipelineResourceSignatureVkImpl::CreateSetLayouts()`：
  - 为所有 inline-constant 资源分配 descriptor set/binding（descriptorCount=1）。
  - 为每个 inline-constant 资源创建 **Signature 级共享** `BufferVkImpl`：
    - 由基类 `PipelineResourceSignatureBase::CreateInlineConstantBuffer()` 创建。
    - `BufferDesc{ size = NumConstants*4, BIND_UNIFORM_BUFFER, USAGE_DYNAMIC, CPU_ACCESS_WRITE }`。
  - 对 Vulkan 来说，常量缓冲默认使用 `DescriptorType::UniformBufferDynamic`（因为未设置 `NO_DYNAMIC_BUFFERS`）。

### 2) SRB cache 初始化：分配 CPU staging 存储并绑定共享 Buffer
- `PipelineResourceSignatureVkImpl::InitSRBResourceCache()`：
  - 通过 `ShaderResourceCacheVk::InitializeSets(..., TotalInlineConstants)` 一次性分配连续内存：
    `DescriptorSet[] + Resource[] + InlineConstantValues[]`。
  - `InitializeResources()` 为 inline-constant 资源把 `Resource::pInlineConstantData` 指向尾部 `InlineConstantValues` 中的切片。
  - 为每个 inline-constant 资源将 PRS 创建的共享 `BufferVkImpl` 绑定进 resource cache（`SetResource(...)`）。

### 3) 写入：`SetInlineConstants` 只更新 CPU staging
- `ShaderVariableManagerVk::SetInlineConstants()` → `ShaderResourceCacheVk::SetInlineConstants()`：
  - 仅 `memcpy` 到 `Resource::pInlineConstantData`（CPU 侧 staging）。

### 4) 提交：绘制/Dispatch 前提交 push constants 或动态 UBO
- `DeviceContextVkImpl::PrepareForDraw()`（以及类似路径）会先提交 inline constants，再提交 descriptor sets：
  - 原因：模拟路径使用 `USAGE_DYNAMIC` 缓冲，更新时会产生新的动态分配偏移，descriptor set 绑定时需要正确的 dynamic offsets。
- `DeviceContextVkImpl::CommitInlineConstants()`：
  - 根据 pipeline layout 的选择，给每个 signature 传入 `PushConstantResIndex`（命中则走 push constant，否则全走 buffer）。
- `PipelineResourceSignatureVkImpl::CommitInlineConstants()`：
  - 对命中的资源：`vkCmdPushConstants`。
  - 对其余资源：`MapBuffer(MAP_WRITE | DISCARD)` → `memcpy` → `UnmapBuffer` 更新共享 `BufferVkImpl`。
  - 随后在 `vkCmdBindDescriptorSets` 时通过 dynamic offsets 指向正确的动态分配。

## 重要数据结构
- `Graphics/GraphicsEngineVulkan/include/PipelineResourceSignatureVkImpl.hpp`：`InlineConstantBufferAttribsVk{ResIndex, DescrSet, BindingIndex, NumConstants, SRBCacheOffset, pBuffer}`。
- `Graphics/GraphicsEngineVulkan/include/ShaderResourceCacheVk.hpp`：`ShaderResourceCacheVk::Resource` 包含 `void* const pInlineConstantData`（仅 inline constants 使用）。
- `Graphics/GraphicsEngine/include/PipelineResourceSignatureBase.hpp`：`CreateInlineConstantBuffer()` 的通用 buffer 创建逻辑（所有后端复用）。

## 控制哪一个资源成为 push constant（实践建议）
- 由于选择规则是“第一个 inline-constant 资源”，要确保你想走 push constant 的那一个：
  1) 处在更靠前的 signature（binding index 更小/排序更前）；
  2) 在该 signature 的资源列表中更靠前。
- 若同一 pipeline layout 下需要多个频繁更新的小常量块，除第一个外都会退化为 dynamic UBO 模拟路径。

## 代码定位（入口）
- 选择 push constant：`Graphics/GraphicsEngineVulkan/src/PipelineLayoutVk.cpp` (`GetPushConstantInfo`)。
- SPIR-V 转换：`Graphics/GraphicsEngineVulkan/src/PipelineStateVkImpl.cpp`（Remap/Verify 阶段）。
- PRS 创建与 SRB init：`Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp`（`CreateSetLayouts`, `InitSRBResourceCache`, `CommitInlineConstants`）。
- 写入 staging：`Graphics/GraphicsEngineVulkan/src/ShaderVariableManagerVk.cpp` 与 `Graphics/GraphicsEngineVulkan/src/ShaderResourceCacheVk.cpp`。
- 绘制时提交：`Graphics/GraphicsEngineVulkan/src/DeviceContextVkImpl.cpp`（`PrepareForDraw`, `CommitInlineConstants`）。