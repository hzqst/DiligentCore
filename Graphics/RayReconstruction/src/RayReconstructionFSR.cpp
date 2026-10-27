/* Copyright 2026 Diligent Graphics LLC
 * Licensed under the Apache License, Version 2.0.
 */
#include "RayReconstructionProvider.hpp"
namespace Diligent
{
std::unique_ptr<RayReconstructionProvider> CreateRRFSRProvider(IRenderDevice*)
{
    // Reserved for a future FSR implementation. Do not advertise an unsupported variant.
    return nullptr;
}
} // namespace Diligent
