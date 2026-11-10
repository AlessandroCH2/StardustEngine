file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_library(StardustEngine STATIC ${ENGINE_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src FILES ${ENGINE_SOURCES})

target_include_directories(StardustEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)

# Version.h: engine version plus the git commit, refreshed on every build
set(STARDUST_VERSION_HEADER ${CMAKE_BINARY_DIR}/generated/Version.h)
add_custom_target(StardustVersion
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DOUTPUT=${STARDUST_VERSION_HEADER} -DVERSION=${PROJECT_VERSION}
            -P ${CMAKE_SOURCE_DIR}/cmake/GenerateVersion.cmake
    BYPRODUCTS ${STARDUST_VERSION_HEADER}
    COMMENT "Stamping engine version"
    VERBATIM)
add_dependencies(StardustEngine StardustVersion)
target_link_libraries(StardustEngine PUBLIC ${CMAKE_DL_LIBS} glfw Vulkan::Vulkan vma glm::glm nlohmann_json::nlohmann_json imgui stb nethost Jolt assetlibs)
if(WIN32)
    # Media Foundation decodes MP4 / MOV / WMV / AVI for the Video Player
    target_link_libraries(StardustEngine PUBLIC mfplat mfreadwrite mfuuid shlwapi ole32)
endif()
target_compile_definitions(StardustEngine PUBLIC
    STARDUST_DXC_PATH="${Vulkan_dxc_EXECUTABLE}"
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_ENABLE_EXPERIMENTAL
    GLFW_INCLUDE_NONE
    STARDUST_ROOT_DIR="${CMAKE_SOURCE_DIR}")
if(MSVC)
    target_compile_options(StardustEngine PRIVATE /W4 /wd4100 /wd4201 /wd4127 /wd4324)
endif()

# ---------------------------------------------------------------- HLSL -> SPIR-V (Vulkan) with DXC
# Naming convention: Name.vs.hlsl / Name.ps.hlsl / Name.cs.hlsl. Output: bin/<cfg>/shaders/Name.<stage>.spv
set(SHADER_DIR ${CMAKE_SOURCE_DIR}/Assets/Shaders)
file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS ${SHADER_DIR}/*.vs.hlsl ${SHADER_DIR}/*.ps.hlsl ${SHADER_DIR}/*.cs.hlsl)
file(GLOB SHADER_INCLUDES CONFIGURE_DEPENDS ${SHADER_DIR}/*.hlsli)
set(SHADER_OUT ${STARDUST_BIN_DIR}/$<CONFIG>/shaders)
set(SPIRV_OUTPUTS)
foreach(src ${SHADER_SOURCES})
    get_filename_component(fname ${src} NAME)
    string(REGEX MATCH "\\.(vs|ps|cs)\\.hlsl$" _m ${fname})
    set(stage ${CMAKE_MATCH_1})
    string(REGEX REPLACE "\\.hlsl$" "" base ${fname})
    set(out ${SHADER_OUT}/${base}.spv)
    add_custom_command(OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT}
        COMMAND ${Vulkan_dxc_EXECUTABLE} -spirv -fspv-target-env=vulkan1.3 -fvk-use-dx-layout
                -HV 2021 -T ${stage}_6_6 -E main -O3 -Zpc -I ${SHADER_DIR} -D STARDUST_VULKAN=1
                -Fo ${out} ${src}
        DEPENDS ${src} ${SHADER_INCLUDES}
        COMMENT "HLSL -> SPIR-V ${fname}"
        VERBATIM)
    list(APPEND SPIRV_OUTPUTS ${out})
endforeach()
# Listed in the IDE only: stop Visual Studio's built-in FxCompile from compiling them as D3D shaders
set_source_files_properties(${SHADER_SOURCES} ${SHADER_INCLUDES} PROPERTIES VS_TOOL_OVERRIDE "None")
add_custom_target(StardustShaders ALL DEPENDS ${SPIRV_OUTPUTS} SOURCES ${SHADER_SOURCES} ${SHADER_INCLUDES})
set_target_properties(StardustShaders PROPERTIES FOLDER "Engine")
add_dependencies(StardustEngine StardustShaders)
