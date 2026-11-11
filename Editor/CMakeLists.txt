file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_executable(StardustEditor ${EDITOR_SOURCES})
if(WIN32)
    target_sources(StardustEditor PRIVATE ${CMAKE_BINARY_DIR}/EngineIcon.rc)
endif()
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src FILES ${EDITOR_SOURCES})
target_include_directories(StardustEditor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(StardustEditor PRIVATE StardustEngine)
add_dependencies(StardustEditor StardustScriptCore)

# nethost.dll must sit next to the executable
add_custom_command(TARGET StardustEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NETHOST_DLL} $<TARGET_FILE_DIR:StardustEditor>)
set_target_properties(StardustEditor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

if(UNIX)
    set_target_properties(StardustEditor PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()

# Packages, shader library, shader compiler, icon and notices next to the executable (cmake/RuntimeFiles.cmake)
stardust_copy_runtime_files(StardustEditor)
