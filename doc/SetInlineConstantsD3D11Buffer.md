# D3D11 InlineConstants Buffer管理与BindPoint分配机制

## 概述

在D3D11后端中，InlineConstants通过**动态常量缓冲区（Dynamic Constant Buffer）**模拟实现。与D3D12的Root Constants不同，D3D11需要创建实际的`ID3D11Buffer`对象，并通过`Map`/`Unmap`机制进行数据更新。

本文档详细分析了D3D11 InlineConstants的以下几个关键方面：
1. 核心数据结构
2. Buffer的创建与生命周期管理
3. BindPoint的分配机制
4. ResourceCache中的初始化
5. 数据更新流程

---

## 1. 核心数据结构

### 1.1 InlineConstantBufferAttribsD3D11

该结构定义在`PipelineResourceSignatureD3D11Impl.hpp`中，用于存储每个Inline Constant Buffer的属性：

```cpp
struct InlineConstantBufferAttribsD3D11
{
    D3D11ResourceBindPoints        BindPoints;     // 各Shader阶段的绑定点
    Uint32                         NumConstants;  // 32位常量的数量
    RefCntAutoPtr<BufferD3D11Impl> pBuffer;       // 内部创建的动态常量缓冲区
};
```

**字段说明：**
| 字段 | 类型 | 描述 |
|------|------|------|
| `BindPoints` | `D3D11ResourceBindPoints` | 存储各Shader阶段（VS/PS/GS/HS/DS/CS）的CBV绑定槽位 |
| `NumConstants` | `Uint32` | 32位常量的数量（即`ArraySize`参数，不是字节数） |
| `pBuffer` | `RefCntAutoPtr<BufferD3D11Impl>` | 内部创建的`USAGE_DYNAMIC`缓冲区对象 |

### 1.2 CachedCB中的Inline Constants支持

`ShaderResourceCacheD3D11::CachedCB`结构扩展了对Inline Constants的支持：

```cpp
struct CachedCB
{
    RefCntAutoPtr<BufferD3D11Impl> pBuff;             // 缓冲区对象
    Uint32 BaseOffset = 0;                            // 基础偏移（字节）
    Uint32 RangeSize  = 0;                            // 范围大小（字节）
    Uint32 DynamicOffset = 0;                         // 动态偏移（字节）
    void* pInlineConstantData = nullptr;              // CPU端暂存数据指针
    
    void SetInlineConstants(const void* pSrcConstants, 
                           Uint32 FirstConstant, 
                           Uint32 NumConstants);
};
```

`pInlineConstantData`指向ResourceCache中分配的CPU端暂存内存，用于在`SetInlineConstants`调用时暂存数据，在Draw/Dispatch时再批量上传到GPU。

---

## 2. Buffer创建与生命周期管理

### 2.1 创建时机

Buffer在`PipelineResourceSignatureD3D11Impl::CreateLayout()`中创建：

```
PipelineResourceSignatureD3D11Impl构造函数
    └─> CreateLayout(IsSerialized = false)
        └─> 遍历Resources
            └─> 如果资源带有 PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS
                └─> 创建动态常量缓冲区
```

### 2.2 创建过程详解

在`CreateLayout()`中的关键代码：

```cpp
if (ResDesc.Flags & PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS)
{
    InlineConstantBufferAttribsD3D11& InlineCBAttribs{m_InlineConstantBuffers[InlineConstantBufferIdx++]};
    InlineCBAttribs.BindPoints   = BindPoints;        // 保存分配的绑定点
    InlineCBAttribs.NumConstants = ResDesc.ArraySize; // ArraySize存储的是常量数量
    
    // 创建动态常量缓冲区
    std::string Name = m_Desc.Name + " - " + ResDesc.Name;
    BufferDesc CBDesc{
        Name.c_str(),
        ResDesc.ArraySize * sizeof(Uint32),  // 缓冲区大小 = 常量数 × 4字节
        BIND_UNIFORM_BUFFER,
        USAGE_DYNAMIC,                        // 动态用法，支持Map
        CPU_ACCESS_WRITE                      // CPU写访问
    };
    
    RefCntAutoPtr<IBuffer> pBuffer;
    m_pDevice->CreateBuffer(CBDesc, nullptr, &pBuffer);
    InlineCBAttribs.pBuffer = pBuffer.RawPtr<BufferD3D11Impl>();
}
```

### 2.3 Buffer共享设计

**重要设计决策**：所有从同一Signature创建的SRB共享同一个内部Buffer对象。

```
PipelineResourceSignature
    └─> m_InlineConstantBuffers[i].pBuffer  ─┬─> SRB1的ResourceCache引用同一Buffer
                                              ├─> SRB2的ResourceCache引用同一Buffer
                                              └─> SRB3的ResourceCache引用同一Buffer
```

这种设计的权衡：
- **优点**：减少内存消耗
- **缺点**：每帧都需要更新Buffer（因为所有SRB共享）

另一种可能的设计是每个SRB有独立Buffer，可以在常量未变化时跳过更新，但会增加内存占用。

### 2.4 生命周期

```
Signature创建 ──> Buffer创建（CreateLayout）
                      │
                      ├──> SRB1创建：引用Buffer
                      ├──> SRB2创建：引用Buffer
                      │
Signature销毁 ──> Buffer销毁（RefCntAutoPtr自动释放）
```

---

## 3. BindPoint分配机制

### 3.1 分配算法

BindPoint分配在`CreateLayout()`中通过`AllocBindPoints`辅助函数完成：

```cpp
const auto AllocBindPoints = [](D3D11ShaderResourceCounters& ResCounters,
                                D3D11ResourceBindPoints&     BindPoints,
                                SHADER_TYPE                  ShaderStages,
                                Uint32                       ArraySize,
                                D3D11_RESOURCE_RANGE         Range)
{
    while (ShaderStages != SHADER_TYPE_UNKNOWN)
    {
        const Int32 ShaderInd = ExtractFirstShaderStageIndex(ShaderStages);
        // 为当前Shader阶段分配绑定点
        BindPoints[ShaderInd] = ResCounters[Range][ShaderInd];
        // 递增计数器
        ResCounters[Range][ShaderInd] += ArraySize;
    }
};
```

### 3.2 Inline Constants的特殊处理

对于Inline Constants，`ArraySize`表示常量数量，但资源只占用**一个**CBV槽位：

```cpp
// 获取实际的数组大小（对于Inline Constants返回1）
const Uint32 ArraySize = ResDesc.GetArraySize();  
// GetArraySize()内部判断：如果是InlineConstants则返回1，否则返回ArraySize

AllocBindPoints(m_ResourceCounters, BindPoints, ResDesc.ShaderStages, ArraySize, Range);
```

### 3.3 BindPoint数据结构

`D3D11ResourceBindPoints`存储了6个Shader阶段的绑定槽位：

```cpp
struct D3D11ResourceBindPoints
{
    // NumShaderTypes = 6 (VS, PS, GS, HS, DS, CS)
    std::array<Uint8, NumShaderTypes> Bindings;
    
    Uint8 operator[](Int32 ShaderInd) const { return Bindings[ShaderInd]; }
    SHADER_TYPE GetActiveStages() const;  // 返回哪些Shader阶段激活
    bool IsStageActive(Int32 ShaderInd) const;
};
```

### 3.4 分配示例

假设Signature定义如下资源：

```cpp
PipelineResourceDesc Resources[] = {
    {"CameraCB",    SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL, 1, CB, STATIC, 0},
    {"TransformCB", SHADER_TYPE_VERTEX, 16, CB, STATIC, INLINE_CONSTANTS},  // Inline
    {"MaterialCB",  SHADER_TYPE_PIXEL,  8,  CB, DYNAMIC, INLINE_CONSTANTS}, // Inline
};
```

分配后的BindPoint：

| 资源 | VS BindPoint | PS BindPoint |
|------|--------------|--------------|
| CameraCB | 0 | 0 |
| TransformCB | 1 | - |
| MaterialCB | - | 1 |

---

## 4. ResourceCache中的初始化

### 4.1 内存布局

`ShaderResourceCacheD3D11`的内存布局如下：

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  CachedCB 数组   │  ID3D11Buffer* 数组  │  ... SRV/Sampler/UAV ...  │        │
│  (VS/PS/GS...)   │  (VS/PS/GS...)       │                           │        │
├─────────────────────────────────────────────────────────────────────────────┤
│                               Inline Constant Data Storage                  │
│  ┌──────────────┬──────────────┬──────────────┐                             │
│  │ InlineCB[0]  │ InlineCB[1]  │ InlineCB[2]  │ ...                         │
│  │ (NumConst*4B)│ (NumConst*4B)│ (NumConst*4B)│                             │
│  └──────────────┴──────────────┴──────────────┘                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 4.2 Initialize流程

`ShaderResourceCacheD3D11::Initialize()`的关键步骤：

```cpp
void ShaderResourceCacheD3D11::Initialize(
    const D3D11ShaderResourceCounters&      ResCount,
    IMemoryAllocator&                       MemAllocator,
    const std::array<Uint16, NumShaderTypes>* pDynamicCBSlotsMask,
    const InlineConstantBufferAttribsD3D11* pInlineCBs,
    Uint32                                  NumInlineCBs)
{
    // 1. 计算Inline Constants总大小
    Uint32 TotalInlineConstants = 0;
    ProcessInlineCBs([&TotalInlineConstants](const InlineConstantBufferAttribsD3D11& attr) {
        TotalInlineConstants += attr.NumConstants;
    });
    
    // 2. 分配内存（包括Inline Constant Data Storage）
    BufferSize = MemOffset + TotalInlineConstants * sizeof(Uint32);
    m_pResourceData = ALLOCATE(MemAllocator, "...", Uint8, BufferSize);
    
    // 3. 初始化Inline Constant Buffers
    Uint32* pInlineCBData = (Uint32*)(m_pResourceData.get() + MemOffset);
    ProcessInlineCBs([&pInlineCBData, this](const InlineConstantBufferAttribsD3D11& attr) {
        InitInlineConstantBuffer(attr.BindPoints, attr.pBuffer, attr.NumConstants, pInlineCBData);
        pInlineCBData += attr.NumConstants;  // 移动到下一个Inline CB的存储位置
    });
}
```

### 4.3 InitInlineConstantBuffer

该函数将Buffer和CPU暂存指针绑定到所有活跃Shader阶段的CachedCB槽位：

```cpp
void ShaderResourceCacheD3D11::InitInlineConstantBuffer(
    const D3D11ResourceBindPoints& BindPoints,
    RefCntAutoPtr<BufferD3D11Impl> pBuffer,
    Uint32                         NumConstants,
    void*                          pInlineConstantData)
{
    ID3D11Buffer* pd3d11Buffer = pBuffer->GetD3D11Buffer();
    
    // 遍历所有活跃的Shader阶段
    for (SHADER_TYPE ActiveStages = BindPoints.GetActiveStages(); 
         ActiveStages != SHADER_TYPE_UNKNOWN;)
    {
        const Uint32 ShaderInd = ExtractFirstShaderStageIndex(ActiveStages);
        const Uint32 Binding   = BindPoints[ShaderInd];
        
        auto  ResArrays = GetResourceArrays<D3D11_RESOURCE_RANGE_CBV>(ShaderInd);
        auto& CachedRes = ResArrays.first[Binding];
        auto& pd3d11Res = ResArrays.second[Binding];
        
        // 设置缓冲区和暂存指针（所有阶段共享同一暂存内存）
        CachedRes.pBuff               = pBuffer;
        CachedRes.RangeSize           = NumConstants * sizeof(Uint32);
        CachedRes.pInlineConstantData = pInlineConstantData;
        pd3d11Res                     = pd3d11Buffer;
    }
}
```

**关键点**：所有Shader阶段共享同一个`pInlineConstantData`指针，这意味着：
- 调用`SetInlineConstants`只需更新一次数据
- 所有阶段会自动获得相同的常量值

---

## 5. 数据更新流程

### 5.1 完整数据流

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        用户代码                                              │
├─────────────────────────────────────────────────────────────────────────────┤
│  pVariable->SetInlineConstants(pData, FirstConstant, NumConstants)          │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                    ShaderVariableManagerD3D11                                │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │ ConstBuffBindInfo::SetConstants():                                     │  │
│  │   1. 验证参数（DEV build only）                                        │  │
│  │   2. 调用 m_ResourceCache.SetInlineConstants(...)                     │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                     ShaderResourceCacheD3D11                                 │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │ SetInlineConstants():                                                  │  │
│  │   1. 根据BindPoints获取第一个活跃Shader阶段的Binding                   │  │
│  │   2. 获取对应的CachedCB                                               │  │
│  │   3. 调用 CachedCB::SetInlineConstants()                              │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│                                                                              │
│  CachedCB::SetInlineConstants():                                            │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │   memcpy(pInlineConstantData + FirstConstant*4, pSrc, NumConstants*4) │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│                                     ↓ 数据暂存于CPU内存                      │
└──────────────────────────────────┬──────────────────────────────────────────┘
                                   │
                                   ▼ (在Draw/Dispatch时)
┌─────────────────────────────────────────────────────────────────────────────┐
│               PipelineResourceSignatureD3D11Impl                             │
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │ UpdateInlineConstantBuffers():                                         │  │
│  │   for each inline constant buffer:                                     │  │
│  │     pd3d11Ctx->Map(pd3d11CB, D3D11_MAP_WRITE_DISCARD)                 │  │
│  │     memcpy(MappedData.pData, pInlineConstantData, Size)               │  │
│  │     pd3d11Ctx->Unmap(pd3d11CB)                                        │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
│                                     ↓ 数据上传至GPU                          │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 5.2 SetInlineConstants实现

```cpp
// ShaderResourceCacheD3D11.hpp
__forceinline void ShaderResourceCacheD3D11::SetInlineConstants(
    const D3D11ResourceBindPoints& BindPoints,
    const void*                    pConstants,
    Uint32                         FirstConstant,
    Uint32                         NumConstants)
{
    // 由于所有Shader阶段共享同一暂存内存，只需更新一次
    SHADER_TYPE ActiveStages = BindPoints.GetActiveStages();
    const Uint32 ShaderInd0 = ExtractFirstShaderStageIndex(ActiveStages);
    const Uint32 Binding0   = BindPoints[ShaderInd0];
    
    const auto ResArrays0 = GetResourceArrays<D3D11_RESOURCE_RANGE_CBV>(ShaderInd0);
    ResArrays0.first[Binding0].SetInlineConstants(pConstants, FirstConstant, NumConstants);
}
```

### 5.3 UpdateInlineConstantBuffers实现

```cpp
void PipelineResourceSignatureD3D11Impl::UpdateInlineConstantBuffers(
    const ShaderResourceCacheD3D11& ResourceCache, 
    ID3D11DeviceContext* pd3d11Ctx) const
{
    for (Uint32 i = 0; i < m_NumInlineConstantBuffers; ++i)
    {
        const InlineConstantBufferAttribsD3D11& InlineCBAttr = m_InlineConstantBuffers[i];
        
        // 获取D3D11 Buffer和暂存数据
        ID3D11Buffer* pd3d11CB = nullptr;
        const auto& InlineCB = ResourceCache.GetResource<D3D11_RESOURCE_RANGE_CBV>(
            InlineCBAttr.BindPoints, &pd3d11CB);
        
        // Map + memcpy + Unmap
        D3D11_MAPPED_SUBRESOURCE MappedData{};
        if (SUCCEEDED(pd3d11Ctx->Map(pd3d11CB, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedData)))
        {
            memcpy(MappedData.pData, InlineCB.pInlineConstantData, 
                   InlineCBAttr.NumConstants * sizeof(Uint32));
            pd3d11Ctx->Unmap(pd3d11CB, 0);
        }
    }
}
```

---

## 6. 静态变量的拷贝

当SRB从Signature创建时，静态Inline Constants需要从Signature的静态缓存拷贝到SRB的缓存：

```cpp
void PipelineResourceSignatureD3D11Impl::CopyStaticResources(
    ShaderResourceCacheD3D11& DstResourceCache) const
{
    for (静态资源...)
    {
        if (ResDesc.Flags & PIPELINE_RESOURCE_FLAG_INLINE_CONSTANTS)
        {
            // 拷贝Inline Constants数据
            DstResourceCache.CopyInlineConstants(SrcResourceCache, ResAttr.BindPoints, 
                                                  ResDesc.ArraySize);
        }
        // ...
    }
}
```

```cpp
inline void ShaderResourceCacheD3D11::CopyInlineConstants(
    const ShaderResourceCacheD3D11& SrcCache, 
    const D3D11ResourceBindPoints& BindPoints, 
    Uint32 NumConstants)
{
    // 由于所有阶段共享同一暂存内存，只需拷贝一次
    const Int32 ShaderInd0 = ExtractFirstShaderStageIndex(BindPoints.GetActiveStages());
    const Uint32 Binding0  = BindPoints[ShaderInd0];
    
    const auto SrcResArrays0 = SrcCache.GetConstResourceArrays<D3D11_RESOURCE_RANGE_CBV>(ShaderInd0);
    const auto DstResArrays0 = GetResourceArrays<D3D11_RESOURCE_RANGE_CBV>(ShaderInd0);
    
    memcpy(DstResArrays0.first[Binding0].pInlineConstantData,
           SrcResArrays0.first[Binding0].pInlineConstantData,
           NumConstants * sizeof(Uint32));
}
```

---

## 7. 性能考虑

### 7.1 与D3D12/Vulkan的对比

| 方面 | D3D11 | D3D12 | Vulkan (Push Constants) |
|------|-------|-------|-------------------------|
| 实现方式 | Dynamic CB + Map | Root Constants | vkCmdPushConstants |
| 内存 | 需要Buffer对象 | 内联在Root Signature中 | 内联在Command Buffer中 |
| 更新开销 | Map/Unmap开销 | 直接写入 | 直接写入 |
| 性能 | 中等 | 高 | 高 |

### 7.2 优化建议

1. **使用`DRAW_FLAG_DYNAMIC_RESOURCE_BUFFERS_INTACT`**
   - 如果Inline Constants未改变，可以跳过`UpdateInlineConstantBuffers`调用

2. **批量更新**
   - 尽量一次性设置所有常量，避免多次调用`SetInlineConstants`

3. **适当的大小**
   - Inline Constants适合小数据（<= 128字节）
   - 大数据应使用常规Constant Buffer

---

## 8. 参考代码位置

| 组件 | 文件路径 |
|------|----------|
| `InlineConstantBufferAttribsD3D11` | `Graphics/GraphicsEngineD3D11/include/PipelineResourceSignatureD3D11Impl.hpp` |
| `CachedCB::SetInlineConstants` | `Graphics/GraphicsEngineD3D11/include/ShaderResourceCacheD3D11.hpp` |
| `CreateLayout()` | `Graphics/GraphicsEngineD3D11/src/PipelineResourceSignatureD3D11Impl.cpp` |
| `Initialize()` | `Graphics/GraphicsEngineD3D11/src/ShaderResourceCacheD3D11.cpp` |
| `UpdateInlineConstantBuffers()` | `Graphics/GraphicsEngineD3D11/src/PipelineResourceSignatureD3D11Impl.cpp` |
| `ConstBuffBindInfo::SetConstants()` | `Graphics/GraphicsEngineD3D11/src/ShaderVariableManagerD3D11.cpp` |

