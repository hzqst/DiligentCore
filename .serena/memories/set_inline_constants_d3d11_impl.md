# SetInlineConstants（D3D11）实现要点（与当前代码一致）

## 总览
- D3D11 无 Root Constants；DiligentCore 在 D3D11 后端用 **动态 Constant Buffer（`USAGE_DYNAMIC + CPU_ACCESS_WRITE`）** 来模拟 inline constants。
- `PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS` 只能用于 `SHADER_RESOURCE_TYPE_CONSTANT_BUFFER`。
- 对 inline constants：`PipelineResourceDesc::ArraySize` 表示 **32-bit 常量个数**；但该资源在 D3D11 里只占用 **1 个 CBV 槽位**（`PipelineResourceDesc::GetArraySize()` 在带 `INLINE_CONSTANTS` 时返回 `1`）。

## Buffer 创建（Signature 级别，共享）
- `PipelineResourceSignatureD3D11Impl::CreateLayout()`（`Graphics/GraphicsEngineD3D11/src/PipelineResourceSignatureD3D11Impl.cpp`）：
  - 统计并分配 `InlineConstantBufferAttribsD3D11[]`。
  - 为资源分配 `BindPoints`（按 shader stage + range 计数器分配）。
  - 对每个 inline-constant 资源：
    - `InlineCBAttribs.BindPoints = BindPoints`；`InlineCBAttribs.NumConstants = ResDesc.ArraySize`。
    - `InlineCBAttribs.pBuffer = CreateInlineConstantBuffer(ResDesc.Name, ResDesc.ArraySize)`。
- `CreateInlineConstantBuffer()` 来自通用基类 helper：`PipelineResourceSignatureBase::CreateInlineConstantBuffer()`（`Graphics/GraphicsEngine/include/PipelineResourceSignatureBase.hpp`）。
  - 创建 `BIND_UNIFORM_BUFFER`、`USAGE_DYNAMIC`、`CPU_ACCESS_WRITE` 的 buffer，大小 `NumConstants * sizeof(Uint32)`，名称为 `"SignatureName - ResName"`。
- 关键语义：**同一个 Signature 创建出来的所有 SRB 共享同一个 D3D11 Buffer 对象**（代码中有明确注释）。

## SRB/Cache 侧：CPU staging 与跨 shader stage 共享
- `ShaderResourceCacheD3D11::Initialize(...)`（`Graphics/GraphicsEngineD3D11/src/ShaderResourceCacheD3D11.cpp`）：
  - 计算 `TotalInlineConstants`，在 cache 的一整块资源内存末尾追加一段 **CPU staging 内存**（`TotalInlineConstants * sizeof(Uint32)`）。
  - 对每个 inline constant buffer：调用 `InitInlineConstantBuffer(BindPoints, pBuffer, NumConstants, pInlineConstantData)`。
- `ShaderResourceCacheD3D11::InitInlineConstantBuffer(...)`：
  - 对 `BindPoints` 覆盖到的所有活跃 shader stage：
    - `CachedCB.pBuff = pBuffer`（指向 signature 共享 buffer）。
    - `CachedCB.RangeSize = NumConstants * sizeof(Uint32)`。
    - `CachedCB.pInlineConstantData = pInlineConstantData`（指向该 cache 的 staging）。
    - 同时保存 `ID3D11Buffer*` 指针。
  - 因此：**同一 inline-constant 资源在不同 shader stage 的 cache 条目共享同一段 staging 指针**（以及相同属性）。

## SetInlineConstants 写入路径（只写 CPU staging）
- `ShaderVariableManagerD3D11::ConstBuffBindInfo::SetConstants(...)`（`Graphics/GraphicsEngineD3D11/src/ShaderVariableManagerD3D11.cpp`）：
  - 做开发版参数验证后调用 `m_ResourceCache.SetInlineConstants(Attr.BindPoints, ...)`。
- `ShaderResourceCacheD3D11::SetInlineConstants(...)`（`Graphics/GraphicsEngineD3D11/include/ShaderResourceCacheD3D11.hpp`）：
  - 取 `BindPoints` 的第一个活跃 shader stage 的 binding（因为 staging 在所有 stage 间共享）。
  - 调用 `CachedCB::SetInlineConstants()` → `memcpy` 写入 `pInlineConstantData + FirstConstant*4`。

## GPU 提交（Map/Unmap 发生在 Bind/Draw 前）
- `DeviceContextD3D11Impl::BindShaderResources(...)`（`Graphics/GraphicsEngineD3D11/src/DeviceContextD3D11Impl.cpp`）：
  - 若该 signature 对应 SRB 具有 inline constants：
    - 当 `SRBStale` 为真，或未指定 `DRAW_FLAG_INLINE_CONSTANTS_INTACT` 时，调用
      `PipelineResourceSignatureD3D11Impl::UpdateInlineConstantBuffers(*pResourceCache, m_pd3d11DeviceContext)`。
- `PipelineResourceSignatureD3D11Impl::UpdateInlineConstantBuffers(...)`（`Graphics/GraphicsEngineD3D11/src/PipelineResourceSignatureD3D11Impl.cpp`）：
  - 遍历 signature 的 `m_InlineConstantBuffers[]`：
    - 从 `ResourceCache` 取到对应 `CachedCB` 和 `ID3D11Buffer*`。
    - `Map(D3D11_MAP_WRITE_DISCARD)` → `memcpy(MappedData.pData, pInlineConstantData, NumConstants*4)` → `Unmap`。

## 使用建议（与实现对齐）
- `SetInlineConstants()` 只写 CPU staging；真正的 GPU 更新在提交阶段 Map/Unmap。
- 若确定 inline constants 在本次 draw/dispatch 前未变化，可使用 `DRAW_FLAG_INLINE_CONSTANTS_INTACT` 跳过 `UpdateInlineConstantBuffers()`。
