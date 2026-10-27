/* Copyright 2026 Diligent Graphics LLC
 * Licensed under the Apache License, Version 2.0.
 */
#include "RayReconstructionFactoryLoader.h"

int TestRayReconstructionCInterface(IRayReconstruction* RR, IRayReconstructionFactory* Factory)
{
    Uint32                       Count = 0;
    float                        X = 0, Y = 0;
    const RayReconstructionDesc* Desc = IRayReconstruction_GetDesc(RR);
    IRayReconstruction_GetJitterOffset(RR, 0, &X, &Y);
    IRayReconstructionFactory_EnumerateVariants(Factory, &Count, NULL);
    return Desc != NULL && Desc->OutputWidth != 0 && Count != 0 && X >= -0.5f && X <= 0.5f && Y >= -0.5f && Y <= 0.5f;
}
