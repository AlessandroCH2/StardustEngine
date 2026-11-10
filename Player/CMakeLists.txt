file(GLOB_RECURSE PLAYER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)

add_executable(StardustPlayer ${PLAYER_SOURCES})
if(WIN32)
    target_sources(StardustPlayer PRIVATE ${CMAKE_BINARY_DIR}/EngineIcon.rc)
    # A windowed game: no console unless it is started with --log (see main.cpp). main() stays the entry point.
    set_target_properties(StardustPlayer PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(StardustPlayer PRIVATE /ENTRY:mainCRTStartup)
endif()
target_link_libraries(StardustPlayer PRIVATE StardustEngine)
add_dependencies(StardustPlayer StardustScriptCore)

add_custom_command(TARGET StardustPlayer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NETHOST_DLL} $<TARGET_FILE_DIR:StardustPlayer>)
set_target_properties(StardustPlayer PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

if(UNIX)
 set_target_properties(StardustPlayer PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
 add_custom_command(TARGET StardustPlayer POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/engineIcon.png $<TARGET_FILE_DIR:StardustPlayer>)
endif()
