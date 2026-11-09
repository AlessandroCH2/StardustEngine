# Builds the C# API assembly with the .NET SDK and drops it next to the engine executables.
add_custom_target(StardustScriptCore ALL
    COMMAND ${DOTNET_EXE} build ${CMAKE_CURRENT_SOURCE_DIR}/Stardust.ScriptCore.csproj
            -c $<IF:$<CONFIG:Debug>,Debug,Release>
            -o ${STARDUST_BIN_DIR}/$<CONFIG>
            --nologo -v:minimal
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building Stardust.ScriptCore (C#)"
    VERBATIM)
set_target_properties(StardustScriptCore PROPERTIES FOLDER "Scripting")
