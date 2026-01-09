# Vulkan Inline Constants（含 VkBuffer 模拟路径）实现解析

本文基于当前代码实现对 Vulkan 后端的 Inline Constants 机制做一次“从调用到落地”的重新梳理，重点覆盖：
- Push Constants（原生路径）的选择与提交
- VkBuffer（动态 UBO）模拟路径的资源/缓存/提交
- 关键数据结构与代码定位

> 术语说明：本文把 `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS` 资源统一称为 **inline constants**。

---

## 0. 结论与总体策略

在 Vulkan 后端，Diligent 的 inline constants 采用“两段式”策略：

1. **PRS（Pipeline Resource Signature）阶段统一处理**
   - 所有 inline constants 都会被分配 descriptor set / binding（`descriptorCount = 1`）
   - 同时为每个 inline-constant 资源在 **Signature 内创建一个共享的 `USAGE_DYNAMIC` UBO**（后续所有 SRB 共享）
   - 每个 SRB 的 resource cache 中还会为每个 inline-constant 资源分配一段 **CPU staging 内存**（保存 `SetInlineConstants()` 写入的值）

2. **PSO / PipelineLayout 阶段只“提升”一个资源为 push constants**
   - Vulkan pipeline layout 仅允许 **一段** push constant range
   - Diligent 会在所有 resource signatures 中找到 **第一个** inline-constant 资源作为 push constants
   - 其余 inline constants 继续走 **VkBuffer（动态 UBO）模拟**路径

---

## 1. 关键语义与限制（一定要先对齐）

### 1.1 `ArraySize` 的语义变化

对于 inline constants（`PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS`）：
- `PipelineResourceDesc::ArraySize` 表示 **4 字节常量的数量**（即 `NumConstants`）
- `PipelineResourceDesc::GetArraySize()` 会返回 **1**（descriptor 数组维度永远为 1）

这会影响：
- descriptor set layout 的 `descriptorCount`（固定为 1）
- SRB cache 的资源元素数量（也按 1 个 resource 处理）
- 但 inline constants 的数据大小仍是 `ArraySize * sizeof(Uint32)`

### 1.2 Flag 约束

- 仅 `SHADER_RESOURCE_TYPE_CONSTANT_BUFFER` 允许 `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS`
- `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS` **不能与任何其它 flag 组合**
  - 因此在 Vulkan 里 inline constants 不会与 `PIPELINE_RESOURCE_FLAG_NO_DYNAMIC_BUFFERS` 同时出现
  - 常量缓冲默认会走 `UniformBufferDynamic`（需要 dynamic offsets）

SPIR-V 映射规则也体现了这一点：push constants 会映射为常量缓冲并带 `INLINE_CONSTANTS` 标志（见 `Graphics/ShaderTools/src/SPIRVShaderResources.cpp`）。

---

## 2. Push Constants 的选择（PSO/PipelineLayout）

核心事实：**Vulkan pipeline layout 只允许一段 push constant range**。

因此 Diligent 的选择策略是：
- 遍历 pipeline 使用的所有 resource signatures（签名按 binding index 排序）
- 找到 **第一个**带 `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS` 的资源
- 用它生成唯一的 push constant range：
  - `size = ResDesc.ArraySize * sizeof(Uint32)`
  - `offset = 0`
  - `stageFlags = ShaderTypesToVkShaderStageFlags(ResDesc.ShaderStages)`

代码位置：
- `Graphics/GraphicsEngineVulkan/src/PipelineLayoutVk.cpp`
  - `PipelineLayoutVk::GetPushConstantInfo()`

注意：
- 创建 pipeline layout 时会校验 `size <= VkPhysicalDeviceLimits::maxPushConstantsSize`
- PSO 创建时会尝试把“选中资源”从 UBO 转换为 push constant（SPIR-V patch）
  - 若 shader 已经存在 push constant，则不能再转换（PSO 会报错）
  - 相关逻辑在 `Graphics/GraphicsEngineVulkan/src/PipelineStateVkImpl.cpp`

---

## 3. PRS：为所有 inline constants 建立“统一资源形态”

### 3.1 `InlineConstantBufferAttribsVk`

定义：
- `Graphics/GraphicsEngineVulkan/include/PipelineResourceSignatureVkImpl.hpp`

用途：
- 记录每个 inline-constant 资源的 descriptor 位置信息（set/binding/cache offset）
- 持有 **Signature 级共享**的 `BufferVkImpl`（用于模拟路径）

关键字段（简化）：
- `ResIndex`：该资源在 signature 的资源数组中的索引（用于唯一定位）
- `DescrSet / BindingIndex`：descriptor set / binding
- `NumConstants`：inline constants 的 4 字节常量数量
- `SRBCacheOffset`：该资源在 SRB cache 中的偏移（用于快速取 staging 指针）
- `pBuffer`：Signature 持有的共享 `BufferVkImpl`

### 3.2 共享 Buffer 的创建：`CreateInlineConstantBuffer()`

inline constants 的 VkBuffer（模拟路径）并不是每个 SRB 一份，而是在 Signature 创建时统一创建，所有 SRB 共享：

- 创建逻辑在基类：
  - `Graphics/GraphicsEngine/include/PipelineResourceSignatureBase.hpp`
  - `CreateInlineConstantBuffer(const char* ResName, Uint32 NumConstants)`

Buffer 关键属性（当前实现）：
- `BIND_UNIFORM_BUFFER`
- `USAGE_DYNAMIC`
- `CPU_ACCESS_WRITE`
- `Size = NumConstants * sizeof(Uint32)`
- Name 会拼接：`"<SignatureName> - <ResName>"`

### 3.3 Vulkan 下 inline constants 默认使用动态 UBO descriptor

在 Vulkan 后端，常量缓冲默认 descriptor type 为：
- 未设置 `NO_DYNAMIC_BUFFERS`：`DescriptorType::UniformBufferDynamic`
- 设置 `NO_DYNAMIC_BUFFERS`：`DescriptorType::UniformBuffer`

而 inline constants 由于 flag 不能组合，等价于“永远走动态 UBO”：
- descriptor type 是 `UniformBufferDynamic`
- 绑定 descriptor sets 时需要提供 dynamic offsets

---

## 4. SRB Resource Cache：CPU staging + 绑定共享 Buffer

### 4.1 Cache 内存布局（非常关键）

`ShaderResourceCacheVk::InitializeSets()` 会一次性分配一块连续内存，布局如下：

```
| DescriptorSet[0..Ns-1] | Resource[0..N-1] | InlineConstantValues[0..TotalInlineConstants-1] |
```

代码位置：
- `Graphics/GraphicsEngineVulkan/include/ShaderResourceCacheVk.hpp`
- `Graphics/GraphicsEngineVulkan/src/ShaderResourceCacheVk.cpp`
  - `GetRequiredMemorySize()`
  - `InitializeSets()`
  - `InitializeResources()`

### 4.2 `Resource::pInlineConstantData`

`ShaderResourceCacheVk::Resource` 对 inline constants 增加了专用字段：
- `void* const pInlineConstantData`

该指针在 `InitializeResources()` 中初始化，指向 `InlineConstantValues` 尾部区域的一段切片（每个 inline-constant 资源有自己的一段 staging 存储）。

### 4.3 `InitSRBResourceCache()` 绑定共享 Buffer

在 `PipelineResourceSignatureVkImpl::InitSRBResourceCache()` 中：
1. 分配并初始化 resource cache（包括 inline constant staging）
2. 分配静态/可变 descriptor set（若存在）
3. 遍历 `m_InlineConstantBuffers`，把 Signature 中创建的共享 `BufferVkImpl` 绑定到 cache 对应资源处（`SetResource(...)`）

重要点：
- **必须在 descriptor set allocation 之后绑定**（代码中有注释），以保证 descriptor writes 能正确工作
- 这一步只是把 buffer 作为资源绑定进 cache；真正的数据更新发生在绘制/dispatch 前的 commit 阶段

---

## 5. 写入路径：`SetInlineConstants()` 只写 CPU staging

调用链：
- `ShaderVariableManagerVk::SetInlineConstants(...)`
  - `Graphics/GraphicsEngineVulkan/src/ShaderVariableManagerVk.cpp`
- → `ShaderResourceCacheVk::SetInlineConstants(...)`
  - `Graphics/GraphicsEngineVulkan/src/ShaderResourceCacheVk.cpp`

行为：
- 仅把数据 `memcpy` 到 `Resource::pInlineConstantData` 所指向的 staging 内存
- 不直接触碰 Vulkan buffer，也不触碰 descriptor set

---

## 6. 提交路径：draw/dispatch 前提交 push constants 或动态 UBO

### 6.1 为什么要先提交 inline constants，再绑定 descriptor sets

在 `DeviceContextVkImpl::PrepareForDraw()` 中可以看到明确注释：
- 先 `CommitInlineConstants`
- 再 `CommitDescriptorSets`

原因：
- 模拟路径使用 `USAGE_DYNAMIC` 缓冲，`MapBuffer(..., DISCARD)` 会产生新的动态分配
- 随后的 `vkCmdBindDescriptorSets` 必须用 **正确的 dynamic offsets** 指向本次提交的数据

代码位置：
- `Graphics/GraphicsEngineVulkan/src/DeviceContextVkImpl.cpp`
  - `PrepareForDraw()`
  - `CommitInlineConstants(...)`

### 6.2 `DeviceContextVkImpl::CommitInlineConstants`

该函数会根据 pipeline layout 的 push constant 选择结果（`PipelineLayoutVk::PushConstantInfo`）：
- 对于**包含被选中 push constant 资源**的 signature：传入 `PushConstantResIndex = PCInfo.ResourceIndex`
- 对于其他 signature：传入 `PushConstantResIndex = ~0u`（表示本签名所有 inline constants 都走 buffer 模拟路径）

### 6.3 `PipelineResourceSignatureVkImpl::CommitInlineConstants`

代码位置：
- `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp`
  - `PipelineResourceSignatureVkImpl::CommitInlineConstants(...)`

它会遍历本签名的所有 `m_InlineConstantBuffers`：

1) 读取数据：
- 从 `ShaderResourceCacheVk` 里用 `DescrSet + SRBCacheOffset` 取到 `pInlineConstantData`
- `DataSize = NumConstants * sizeof(Uint32)`

2) 若命中 push constant 资源：
- `vkCmdPushConstants`（封装为 `CmdBuffer.PushConstants(...)`）

3) 否则走 VkBuffer 模拟路径：
- `MapBuffer(USAGE_DYNAMIC, MAP_WRITE | MAP_FLAG_DISCARD)`
- `memcpy(mapped, pInlineConstantData, DataSize)`
- `UnmapBuffer(...)`

随后在 descriptor sets 绑定阶段，dynamic offsets 会指向这次 map 所对应的动态分配。

---

## 7. 共享 Buffer（Signature 级） vs staging（SRB 级）：所有权与并发含义

当前设计的核心对齐点（也与 D3D11 的思路一致）：
- **GPU 侧 UBO（用于模拟）**：Signature 级共享（节省对象数量与内存）
- **CPU 侧 staging 数据**：SRB 级独立（每个 SRB 保存自己的一份 inline constant 值）

简化示意：

```
PipelineResourceSignature (shared)
  InlineConstantBuffers[]
    pBuffer (USAGE_DYNAMIC UBO)  <--- shared by all SRBs

SRB A (per-SRB)
  ResourceCache
    pInlineConstantData (A's staging)

SRB B (per-SRB)
  ResourceCache
    pInlineConstantData (B's staging)
```

---

## 8. 如何“控制”哪个 inline constants 走 push constants（实践建议）

由于选择规则是“第一个 inline-constant 资源”：
- 想走 push constants 的那一块常量，应该尽量：
  1) 位于更靠前的 signature（binding index 更小/排序更前）
  2) 且在该 signature 的资源列表里更靠前

同时注意：
- 只有一个资源能走 push constants，其余资源会走动态 UBO 模拟路径
- 若 shader 已经定义了 push constants，则 PSO 可能无法再把 UBO 转换为 push constants

---

## 9. 关键代码定位（索引表）

| 主题 | 代码位置 |
|------|----------|
| push constant 选择 | `Graphics/GraphicsEngineVulkan/src/PipelineLayoutVk.cpp` (`GetPushConstantInfo`) |
| PSO 资源处理/可能的 SPIR-V 转换 | `Graphics/GraphicsEngineVulkan/src/PipelineStateVkImpl.cpp` |
| PRS 建 set layout、创建共享 UBO | `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp` (`CreateSetLayouts`) |
| SRB cache 初始化 + 绑定共享 UBO | `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp` (`InitSRBResourceCache`) |
| 写入 staging | `Graphics/GraphicsEngineVulkan/src/ShaderVariableManagerVk.cpp` + `Graphics/GraphicsEngineVulkan/src/ShaderResourceCacheVk.cpp` |
| draw/dispatch 前提交 | `Graphics/GraphicsEngineVulkan/src/DeviceContextVkImpl.cpp` (`PrepareForDraw`, `CommitInlineConstants`) |
| 提交实现（push constants 或 Map/Unmap） | `Graphics/GraphicsEngineVulkan/src/PipelineResourceSignatureVkImpl.cpp` (`CommitInlineConstants`) |

