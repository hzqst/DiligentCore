# ConvertUBOToPushConstants（UBO 转 Push Constants）

## 用途（DiligentCore 内部）
- Vulkan pipeline layout 只能有一段 push constants，但业务/工具链可能把同一份数据编译成 UBO。
- 在创建 Vulkan PSO 时，如果 pipeline layout 选中了某个资源名作为 push constants，而 shader 里该资源仍是 UBO，则会对 SPIR-V 做一次补丁，把该 UBO 转成 push constants。
  - 入口：`Graphics/GraphicsEngineVulkan/src/PipelineStateVkImpl.cpp`（创建 PSO 时打补丁并重建 `SPIRVShaderResources`）。

## 实现位置
- API：`Graphics/ShaderTools/include/SPIRVTools.hpp` → `ConvertUBOToPushConstants()`。
- 实现：`Graphics/ShaderTools/src/ConvertUBOToPushConstant.cpp`。
  - 先注册 SPIRV-Tools 的 `InlineExhaustivePass` + `AggressiveDCEPass` + `EliminateDeadFunctionsPass`，再运行自定义 out-of-tree pass `ConvertUBOToPushConstantPass`。
  - `DILIGENT_DEVELOPMENT` 下开启 validator（`options.set_run_validator(true)`）。

## 目标 UBO 的定位逻辑（BlockName 匹配）
- 依赖 `OpName`：扫描所有 `OpName`，收集名字等于 `BlockName` 的 ID。
- 支持两种命名方式（不同编译器/前端会不同）：
  1) `OpVariable` 直接叫 `BlockName`（变量实例名，例如 `cb`）
  2) `OpTypeStruct` 叫 `BlockName`（结构体/块类型名，例如 `CB1`），再去找指向它的全局 `OpVariable`
- 判定为“可转换 UBO”的必要条件：
  - `OpTypePointer` storage class 为 `Uniform`
  - pointee struct 有 `Block` decoration 且没有 `BufferBlock`（避免把 SSBO 当 UBO 转）

## 具体改写内容
- 创建/复用 `OpTypePointer PushConstant <pointee>`（用 `TypeManager::FindPointerToType`）。
- 处理 SSA“先定义后使用”：如果新 pointer type 出现在 `types_values()` 末尾且会被更早的指令使用（特别是全局 `OpVariable`/`OpConstantNull` 等也在 `types_values()`），会把 type 指令移动到 use 之前（只在确实“在后面”时移动，避免扰动已有类型顺序）。
- 更新目标 `OpVariable`：
  - result type 改成 `PushConstant` 指针类型
  - `OpVariable` 的 storage class operand 改为 `PushConstant`
- 显式失效分析缓存：类型/def-use/decoration（因为会改类型并可能移动 types_values 指令，随后还要基于 def-use 继续传播）。
- 递归传播“派生指针”的 result type（把 Uniform 指针链改成 PushConstant 指针链），覆盖常见 opcode：
  - `OpAccessChain/OpPtrAccessChain/OpInBounds*`、`OpCopyObject`、`OpPhi`、`OpSelect`、`OpBitcast`、`OpUndef`、`OpConstantNull`
  - `OpFunctionCall` 不做传播（语义不安全）；因此 wrapper 通过 inlining 尽量消除 call。
  - 遇到未知的 pointer-producing 模式会停止该链的传播并打印 warning。
- 删除目标变量上的 `Binding` / `DescriptorSet` decoration（push constants 不需要也不允许这些）；保留 `Block`、`Offset`、`MatrixStride` 等布局相关 decoration。

## 重要限制/踩坑
- 必须在“剥离反射/剥离名字（OpName）”之前运行；否则会报 “no OpName found” 并返回空。
- 如果 inlining 后仍存在 `OpFunctionCall` 且指针流跨越函数边界，传播可能不完整；开发版 validator 能尽早暴露。
- 仅处理 UBO（Uniform + Block），不会把已有 push constants 反转成 UBO。

## 测试定位
- `Tests/DiligentCoreAPITest/src/Vulkan/ConvertUBOToPushConstantsTestVk.cpp`
  - 覆盖以 struct 名 `CB1` 和变量名 `cb` 打补丁的路径
  - 覆盖 GLSLang（GLSL/HLSL）与 DXC（HLSL）
