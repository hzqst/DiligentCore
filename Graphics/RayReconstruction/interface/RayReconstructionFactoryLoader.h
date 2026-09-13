/* Copyright 2026 Diligent Graphics LLC
 * Licensed under the Apache License, Version 2.0.
 */
#pragma once
#include "RayReconstructionFactory.h"
#if DILIGENT_RAY_RECONSTRUCTION_SHARED && PLATFORM_WIN32 && defined(_MSC_VER)
#    include "../../GraphicsEngine/interface/LoadEngineDll.h"
#    define DILIGENT_RAY_RECONSTRUCTION_EXPLICIT_LOAD 1
#endif
DILIGENT_BEGIN_NAMESPACE(Diligent)
typedef void (*CreateRayReconstructionFactoryType)(IRenderDevice*, IRayReconstructionFactory**);
#if DILIGENT_RAY_RECONSTRUCTION_EXPLICIT_LOAD
inline CreateRayReconstructionFactoryType DILIGENT_GLOBAL_FUNCTION(LoadRayReconstructionFactory)()
{
    return (CreateRayReconstructionFactoryType)LoadEngineDll("RayReconstruction", "CreateRayReconstructionFactory");
}
#else
void DILIGENT_GLOBAL_FUNCTION(CreateRayReconstructionFactory)(IRenderDevice* pDevice, IRayReconstructionFactory** ppFactory);
#endif
inline void DILIGENT_GLOBAL_FUNCTION(LoadAndCreateRayReconstructionFactory)(IRenderDevice* pDevice, IRayReconstructionFactory** ppFactory)
{
    if (ppFactory == NULL)
        return;
    *ppFactory = NULL;
#if DILIGENT_RAY_RECONSTRUCTION_EXPLICIT_LOAD
    CreateRayReconstructionFactoryType CreateFactory = DILIGENT_GLOBAL_FUNCTION(LoadRayReconstructionFactory)();
    if (CreateFactory != NULL)
        CreateFactory(pDevice, ppFactory);
#else
    DILIGENT_GLOBAL_FUNCTION(CreateRayReconstructionFactory)
    (pDevice, ppFactory);
#endif
}
DILIGENT_END_NAMESPACE
