include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# ---------------------------------------------------------------- Vulkan SDK (headers, loader, DXC shader compiler)
find_package(Vulkan REQUIRED COMPONENTS dxc)
if(NOT Vulkan_dxc_EXECUTABLE)
    find_program(Vulkan_dxc_EXECUTABLE dxc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
endif()
message(STATUS "DXC: ${Vulkan_dxc_EXECUTABLE}")

# ---------------------------------------------------------------- GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE)

FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# Source-only dependencies: SOURCE_SUBDIR points at a missing folder so they are only downloaded.
FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.1.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _no_cmake)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8-docking
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _no_cmake)
FetchContent_Declare(imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG 105e4fde45e4e74aae8973d98750820ef7aca7e2
    SOURCE_SUBDIR _no_cmake)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _no_cmake)

FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG v1.14
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _no_cmake)
FetchContent_Declare(tinyobj
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG v2.0.0rc13
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _no_cmake)

# ---------------------------------------------------------------- Jolt Physics
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)  # match our /MD runtime
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
FetchContent_Declare(jolt
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.2.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR Build)

FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.21
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _no_cmake)

FetchContent_Declare(ufbx
    GIT_REPOSITORY https://github.com/ufbx/ufbx.git
    GIT_TAG v0.21.5
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _no_cmake)
FetchContent_MakeAvailable(glfw glm json vma imgui imguizmo stb cgltf tinyobj jolt miniaudio ufbx)
add_library(ufbx STATIC ${ufbx_SOURCE_DIR}/ufbx.c)
target_include_directories(ufbx PUBLIC ${ufbx_SOURCE_DIR})
set_target_properties(Jolt PROPERTIES FOLDER "ThirdParty")

add_library(assetlibs INTERFACE)
target_link_libraries(assetlibs INTERFACE ufbx)
target_include_directories(assetlibs INTERFACE ${cgltf_SOURCE_DIR} ${tinyobj_SOURCE_DIR} ${miniaudio_SOURCE_DIR})

add_library(vma INTERFACE)
target_include_directories(vma INTERFACE ${vma_SOURCE_DIR}/include)

# ImGui + ImGuizmo with GLFW + Vulkan backends (the D3D12 backend is added together with the D3D12 RHI)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp
    ${imguizmo_SOURCE_DIR}/src)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

set_target_properties(glfw imgui PROPERTIES FOLDER "ThirdParty")

# ---------------------------------------------------------------- nethost (from the installed .NET SDK)
find_program(DOTNET_EXE dotnet REQUIRED)
get_filename_component(DOTNET_REAL_EXE "${DOTNET_EXE}" REALPATH)
get_filename_component(DOTNET_ROOT "${DOTNET_REAL_EXE}" DIRECTORY)
if(WIN32)
 set(STARDUST_DOTNET_RID win-x64)
else()
 set(STARDUST_DOTNET_RID linux-x64)
endif()
file(GLOB NETHOST_CANDIDATES LIST_DIRECTORIES true
    "${DOTNET_ROOT}/packs/Microsoft.NETCore.App.Host.${STARDUST_DOTNET_RID}/8.*/runtimes/${STARDUST_DOTNET_RID}/native")
if(NOT NETHOST_CANDIDATES)
    message(FATAL_ERROR "nethost not found: install the .NET 8 SDK")
endif()
list(SORT NETHOST_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
list(GET NETHOST_CANDIDATES 0 NETHOST_DIR)
message(STATUS "nethost: ${NETHOST_DIR}")

add_library(nethost SHARED IMPORTED GLOBAL)
set_target_properties(nethost PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${NETHOST_DIR}")
if(WIN32)
 set_target_properties(nethost PROPERTIES IMPORTED_LOCATION "${NETHOST_DIR}/nethost.dll" IMPORTED_IMPLIB "${NETHOST_DIR}/nethost.lib")
 set(NETHOST_DLL "${NETHOST_DIR}/nethost.dll" CACHE INTERNAL "")
else()
 set_target_properties(nethost PROPERTIES IMPORTED_LOCATION "${NETHOST_DIR}/libnethost.so")
 set(NETHOST_DLL "${NETHOST_DIR}/libnethost.so" CACHE INTERNAL "")
endif()
