cmake_minimum_required(VERSION 3.11)

# A single NGX loader/state owner even when SR and RR are loaded as separate DLLs.
FetchContent_Declare(DLSS-Headers
    GIT_REPOSITORY https://github.com/hzqst/DLSS-Headers.git
    GIT_TAG 05bc207f59405548b4b49e76341502eb35680a33
)
FetchContent_MakeAvailable(DLSS-Headers)

add_library(Diligent-NGX-shared SHARED src/NGXRuntime.cpp src/NGX.def)
target_include_directories(Diligent-NGX-shared PUBLIC include PRIVATE ../../ThirdParty/Vulkan-Headers/include)
target_compile_definitions(Diligent-NGX-shared PRIVATE DILIGENT_NGX_EXPORTS)
target_link_libraries(Diligent-NGX-shared PUBLIC DLSS-Headers PRIVATE DLSS-NGX Diligent-BuildSettings Diligent-Common)
set_dll_output_name(Diligent-NGX-shared DiligentNGX)
set_common_target_properties(Diligent-NGX-shared)
set_target_properties(Diligent-NGX-shared PROPERTIES FOLDER DiligentCore/Graphics)

function(copy_diligent_ngx_runtime TARGET_NAME)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Diligent-NGX-shared>" "$<TARGET_FILE_DIR:${TARGET_NAME}>"
        VERBATIM)
endfunction()

if(DILIGENT_INSTALL_CORE)
    install_core_lib(Diligent-NGX-shared)
    if(SUPER_RESOLUTION_SUPPORTED)
        install(FILES "${DLSS_BIN_DIR}/nvngx_dlss.dll" DESTINATION "${CMAKE_INSTALL_BINDIR}/${DILIGENT_CORE_DIR}/$<CONFIG>")
    endif()
    if(RAY_RECONSTRUCTION_SUPPORTED)
        install(FILES "${DLSS_BIN_DIR}/nvngx_dlssd.dll" DESTINATION "${CMAKE_INSTALL_BINDIR}/${DILIGENT_CORE_DIR}/$<CONFIG>")
    endif()
endif()
