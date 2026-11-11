# Files the editor and the player read at run time, copied next to the executable after every build, so a build
# folder (build/bin/Release, build/bin/Debug...) works on its own and no path to the source tree is compiled in.
#   Packages/         official packages (the core package and the optional ones the Package Manager installs)
#   Assets/Shaders/   the shader library: copied into projects and used to compile custom shaders
#   tools/dxc/        the shader compiler for custom shaders (Windows; the Linux package adds its own)
#   docs/             third-party notices that exported games ship with
#   engineIcon.png    window and editor icon
function(stardust_copy_runtime_files target)
    set(dir $<TARGET_FILE_DIR:${target}>)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/engineIcon.png ${dir}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Packages ${dir}/Packages
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Assets/Shaders ${dir}/Assets/Shaders
        COMMAND ${CMAKE_COMMAND} -E make_directory ${dir}/docs
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/docs/THIRD_PARTY_PL_MPEG.txt ${CMAKE_SOURCE_DIR}/docs/THIRD_PARTY_UFBX.txt ${dir}/docs
        VERBATIM)
    if(WIN32 AND Vulkan_dxc_EXECUTABLE)
        get_filename_component(dxc_dir ${Vulkan_dxc_EXECUTABLE} DIRECTORY)
        set(dxc_files ${Vulkan_dxc_EXECUTABLE})
        foreach(dll dxcompiler.dll dxil.dll)
            if(EXISTS ${dxc_dir}/${dll})
                list(APPEND dxc_files ${dxc_dir}/${dll})
            endif()
        endforeach()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ${dir}/tools/dxc/bin
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${dxc_files} ${dir}/tools/dxc/bin
            VERBATIM)
    endif()
endfunction()
