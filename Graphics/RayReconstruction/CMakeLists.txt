cmake_minimum_required(VERSION 3.11)
project(Diligent-RayReconstruction CXX)

add_library(Diligent-RayReconstructionInterface INTERFACE)
target_link_libraries(Diligent-RayReconstructionInterface INTERFACE Diligent-GraphicsEngineInterface)
target_include_directories(Diligent-RayReconstructionInterface INTERFACE interface)
target_compile_definitions(Diligent-RayReconstructionInterface INTERFACE RAY_RECONSTRUCTION_SUPPORTED=1)

set(SOURCE
    src/RayReconstructionBase.cpp
    src/RayReconstructionDLSS.cpp
    src/RayReconstructionFactory.cpp
    src/RayReconstructionFSR.cpp
)
set(INCLUDE
    include/RayReconstructionBase.hpp
    include/RayReconstructionDLSS.hpp
    include/RayReconstructionProvider.hpp
)
set(INTERFACE
    interface/RayReconstruction.h
    interface/RayReconstructionFactory.h
    interface/RayReconstructionFactoryLoader.h
)
if(D3D12_SUPPORTED)
    list(APPEND SOURCE src/DLSSProviderD3D12.cpp)
endif()
if(VULKAN_SUPPORTED)
    list(APPEND SOURCE src/DLSSProviderVk.cpp)
endif()
source_group("src" FILES ${SOURCE})
source_group("include" FILES ${INCLUDE})
source_group("interface" FILES ${INTERFACE})
add_library(Diligent-RayReconstruction-static STATIC ${SOURCE} ${INCLUDE} ${INTERFACE} readme.md)
target_link_libraries(Diligent-RayReconstruction-static PUBLIC Diligent-RayReconstructionInterface
    PRIVATE Diligent-NGX-shared Diligent-BuildSettings Diligent-Common Diligent-GraphicsAccessories)
target_include_directories(Diligent-RayReconstruction-static PRIVATE include ../GraphicsEngine/include
    ../GraphicsEngineNextGenBase/include ../GraphicsEngineD3DBase/include
    ../GraphicsEngineD3D12/include ../GraphicsEngineVulkan/include ../ShaderTools/include)
if(D3D12_SUPPORTED)
    target_compile_definitions(Diligent-RayReconstruction-static PRIVATE DILIGENT_RR_D3D12=1)
    target_link_libraries(Diligent-RayReconstruction-static PRIVATE Diligent-GraphicsEngineD3D12-static)
endif()
if(VULKAN_SUPPORTED)
    target_compile_definitions(Diligent-RayReconstruction-static PRIVATE DILIGENT_RR_VULKAN=1)
    target_link_libraries(Diligent-RayReconstruction-static PRIVATE Diligent-GraphicsEngineVk-static Vulkan::Headers)
endif()

add_library(Diligent-RayReconstruction-shared SHARED src/DLLMain.cpp src/RayReconstruction.def)
target_link_libraries(Diligent-RayReconstruction-shared PUBLIC Diligent-RayReconstructionInterface PRIVATE Diligent-BuildSettings)
target_link_whole_archive(Diligent-RayReconstruction-shared Diligent-RayReconstruction-static)
target_compile_definitions(Diligent-RayReconstruction-shared INTERFACE DILIGENT_RAY_RECONSTRUCTION_SHARED=1)
set_dll_output_name(Diligent-RayReconstruction-shared RayReconstruction)
set_common_target_properties(Diligent-RayReconstruction-static)
set_common_target_properties(Diligent-RayReconstruction-shared)
set_target_properties(Diligent-RayReconstruction-static Diligent-RayReconstruction-shared PROPERTIES FOLDER DiligentCore/Graphics)
copy_diligent_ngx_runtime(Diligent-RayReconstruction-shared)
copy_dlssd_dlls(Diligent-RayReconstruction-shared)
if(DILIGENT_INSTALL_CORE)
    install_core_lib(Diligent-RayReconstruction-static)
    install_core_lib(Diligent-RayReconstruction-shared)
endif()
