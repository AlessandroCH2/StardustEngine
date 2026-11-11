cmake_minimum_required(VERSION 3.24)
project(StardustEngine VERSION 0.6.2 LANGUAGES C CXX)
if(WIN32)
    enable_language(RC)
    configure_file(Engine/Resources/EngineIcon.rc.in ${CMAKE_BINARY_DIR}/EngineIcon.rc @ONLY)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# All executables and DLLs land in one folder so the editor finds nethost.dll and the C# assemblies.
set(STARDUST_BIN_DIR ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${STARDUST_BIN_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${STARDUST_BIN_DIR})
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${STARDUST_BIN_DIR}/${cfg})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG} ${STARDUST_BIN_DIR}/${cfg})
endforeach()

if(MSVC)
    add_compile_options(/MP /utf-8 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# __FILE__ (asserts, Vulkan errors) becomes relative to the repository: no path of the build machine ends up in the executables
if(MSVC)
    file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}/" STARDUST_SOURCE_PREFIX)
    add_compile_options("/d1trimfile:${STARDUST_SOURCE_PREFIX}")
else()
    add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}/=")
endif()
include(cmake/Dependencies.cmake)

include(cmake/RuntimeFiles.cmake)
add_subdirectory(Engine)
option(STARDUST_BUILD_EDITOR "Build the editor" ON)
if(STARDUST_BUILD_EDITOR)
    add_subdirectory(Editor)
endif()
add_subdirectory(Player)
add_subdirectory(ScriptCore)

add_executable(StardustPacker Tools/AssetPacker.cpp)

# Test programs: off by default, turn on with -DSTARDUST_BUILD_TESTS=ON
option(STARDUST_BUILD_TESTS "Build the test programs" OFF)
if(STARDUST_BUILD_TESTS)
if(WIN32)
add_executable(StardustRuntimeTests EXCLUDE_FROM_ALL Tests/RuntimeTests.cpp)
target_link_libraries(StardustRuntimeTests PRIVATE StardustEngine)
endif()
add_executable(StardustGpuFeatureTests EXCLUDE_FROM_ALL Tests/GpuFeatureTests.cpp)
target_link_libraries(StardustGpuFeatureTests PRIVATE StardustEngine)
target_sources(StardustGpuFeatureTests PRIVATE Editor/src/EditorTheme.cpp Editor/src/AssetUI.cpp)
target_include_directories(StardustGpuFeatureTests PRIVATE Editor/src)
add_dependencies(StardustGpuFeatureTests StardustScriptCore)
set(TEST_PROPERTY_SHADER ${CMAKE_BINARY_DIR}/TestShaders/CustomProperties.ps.spv)
add_custom_command(OUTPUT ${TEST_PROPERTY_SHADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/TestShaders
    COMMAND ${Vulkan_dxc_EXECUTABLE} -spirv -fspv-target-env=vulkan1.3 -fvk-use-dx-layout -HV 2021 -T ps_6_6 -E main -O3 -Zpc -I ${CMAKE_SOURCE_DIR}/Assets/Shaders -D STARDUST_VULKAN=1 -Fo ${TEST_PROPERTY_SHADER} ${CMAKE_SOURCE_DIR}/Tests/Fixtures/CustomProperties.ps.hlsl
    DEPENDS Tests/Fixtures/CustomProperties.ps.hlsl Assets/Shaders/MeshCommon.hlsli Assets/Shaders/Common.hlsli Assets/Shaders/Bindless.hlsli VERBATIM)
add_custom_target(StardustTestPropertyShader DEPENDS ${TEST_PROPERTY_SHADER})
add_dependencies(StardustGpuFeatureTests StardustTestPropertyShader)
endif()

target_link_libraries(StardustPacker PRIVATE StardustEngine)

set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT StardustEditor)
