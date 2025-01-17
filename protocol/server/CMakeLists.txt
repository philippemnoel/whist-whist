#[[
################## WhistServer ##################
]]

add_executable(WhistServer
        client.c
        video.c
        audio.c
        parse_args.c
        main.c
        handle_client_message.c
        network.c
        dbus.c
        gpu_commands.c
        )

set_target_properties(WhistServer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/server/build${arch}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/server/build${arch})

copy_runtime_libs(WhistServer)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin"
OR ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    add_custom_command(
            TARGET WhistServer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/server/wserver
            ${CMAKE_BINARY_DIR})

endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    add_custom_command(
            TARGET WhistServer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/server/wserver.bat
            ${CMAKE_BINARY_DIR})
endif()

# Print out compile flags in verbose mode of CMake
message(VERBOSE "c base flags ${CMAKE_C_FLAGS}")
message(VERBOSE "c release flags ${CMAKE_C_FLAGS_RELEASE}")
message(VERBOSE "c debug flags ${CMAKE_C_FLAGS_DEBUG}")

# FFMPEG, Threading and Dynamic linking libs
foreach(LIB ${FFMPEG_LIBS_PATHS})
    message(VERBOSE "linking lib for WhistServer: ${LIB}")
    target_link_libraries(WhistServer ${LIB})
endforeach()

# Threading and Dynamic link for linux/macosx only
# if we add other target platforms we may need to come back to this
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Windows")
#    set(THREADS_PREFER_PTHREAD_FLAG ON)
#    find_package(Threads REQUIRED)
    target_link_libraries(WhistServer
#       Threads::Threads
        ${CMAKE_DL_LIBS}
        )
endif()


#[[
################## Linking platform independent libs ##################
]]

target_link_libraries(WhistServer
    ${PLATFORM_INDEPENDENT_SERVER_LIBS}
    )

#[[
################## LINUX ##################
]]
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    find_package(X11 REQUIRED
            Xfixes
            Xdamage
            Xext
            Xtst)
    include_directories(${X11_INCLUDE_DIR})
    if(NOT X11_LIBRARIES)
        message(FATAL_ERROR "Library X11_LIBRARIES was not found!")
    endif()
    if(NOT X11_Xfixes_LIB)
        message(FATAL_ERROR "Library X11_Xfixes_LIB was not found!")
    endif()
    if(NOT X11_Xdamage_LIB)
        message(FATAL_ERROR "Library X11_Xdamage_LIB was not found!")
    endif()
    if(NOT X11_Xext_LIB)
        message(FATAL_ERROR "Library X11_Xext_LIB was not found!")
    endif()

    pkg_check_modules(DBUS REQUIRED dbus-1)
    include_directories(${DBUS_INCLUDE_DIRS})
    message(STATUS "DBUS_INCLUDE_DIRS: ${DBUS_INCLUDE_DIRS}")
    if(NOT DBUS_FOUND)
        message(FATAL_ERROR "Library dbus-1 was not found!")
    endif()

    target_link_libraries(WhistServer
            ${X11_LIBRARIES}
            ${X11_Xfixes_LIB}
            ${X11_Xdamage_LIB}
            ${X11_Xext_LIB}
            ${X11_Xtst_LIB}
            ${DBUS_LIBRARIES}
            nppc_static
            nppicc_static
            culibos
            cudart
            GL
            asound
            m
            OpenSSL::Crypto
            )


endif()

#[[
################## WINDOWS ##################
]]
if(${WIN32})
    #Linked flags: LNK4099 is when the linker cannot find the .pdb for debugging symbols
    # this comes up when we used precompiled libraries which we do not have .pdb files for
    set(CMAKE_EXE_LINKER_FLAGS "/nodefaultlib /nologo /ignore:4099 ")

    #sets the MSVC runtime library as multithreaded and adds debug symbols if is a debug build
    #see https://cmake.org/cmake/help/v3.15/variable/CMAKE_MSVC_RUNTIME_LIBRARY.html#variable:CMAKE_MSVC_RUNTIME_LIBRARY
    set_property(TARGET WhistServer PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    if(CMAKE_CUDA_COMPILER)
        set_property(TARGET WhistServer PROPERTY CUDA_ARCHITECTURES OFF)
    endif()

    # Link to system libs
    target_link_libraries(WhistServer ${WINDOWS_SPECIFIC_SERVER_LIBS})
endif()
