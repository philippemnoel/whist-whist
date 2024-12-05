# #[[
################## WhistProtocolTest ##################
#]]

# Create the executable
set(PROTOCOL_TEST_BINARY WhistProtocolTest)

# Client-only files, since macOS is only supported client-sides
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    add_executable(${PROTOCOL_TEST_BINARY}
        # Only run this with `make WhistProtocolTest`
        EXCLUDE_FROM_ALL

        protocol_test.cpp
        codec_test.cpp
        lodepng_test.cpp

        # Files needed for Whist Lib unit tests
        ../whist/utils/aes.c
        ../whist/utils/png.c
        ../whist/utils/avpacket_buffer.c
        ../whist/network/ringbuffer.c
        ../whist/network/network_algorithm.c

        # Files needed for Cursor unit tests
        whist_cursor_test.cpp
        ../whist/cursor/cursor.cpp
        ../whist/cursor/cursor_internal.c

        # Files needed for Client unit tests
        ../client/audio.cpp
        ../client/client_utils.c
        ../client/whist_client.cpp
        ../client/network.cpp
        ../client/handle_frontend_events.cpp
        ../client/sdl_utils.c
        ../client/handle_server_message.cpp
        ../client/video.cpp
        ../client/sync_packets.cpp
        ../client/renderer.c
        ../client/frontend/frontend.c
        ../client/frontend/sdl/audio.cpp
        ../client/frontend/sdl/events.cpp
        ../client/frontend/sdl/lifecycle.cpp
        ../client/frontend/sdl/sdl.c
        ../client/frontend/virtual/virtual.c
        ../client/frontend/virtual/impl.c
        ../client/frontend/virtual/interface.cpp
        ../client/frontend/sdl/video.cpp
        ../client/frontend/sdl/window.cpp
        ../client/frontend/sdl/stdin_parser.c

        # Files needed for LodePNG unit tests
        ../whist/utils/lodepng.cpp

        # Debug console
        ../whist/debug/debug_console.cpp
    )
# Server & Client files, for Windows/Linux (supported on both client and server)
else()
    if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        set(EXTRA_TESTS capture_test.cpp)
    endif()

    add_executable(${PROTOCOL_TEST_BINARY}
        ${EXTRA_TESTS}
        protocol_test.cpp
        codec_test.cpp
        lodepng_test.cpp

        # Files needed for Server unit tests
        ../server/client.c
        ../server/video.c
        ../server/audio.c
        ../server/handle_client_message.c
        ../server/network.c
        ../server/network.c
        ../server/dbus.c

        # Files needed for Whist Lib unit tests
        ../whist/utils/aes.c
        ../whist/utils/png.c
        ../whist/utils/avpacket_buffer.c
        ../whist/network/ringbuffer.c
        ../whist/network/network_algorithm.c
        ../whist/video/capture/filecapture.c
        ../whist/video/ltr.c
        ../whist/file/file_synchronizer.c

        # Files needed for Cursor unit tests
        whist_cursor_test.cpp
        ../whist/cursor/cursor.cpp
        ../whist/cursor/cursor_internal.c

        # Files needed for Client unit tests
        ../client/audio.cpp
        ../client/client_utils.c
        ../client/whist_client.cpp
        ../client/network.cpp
        ../client/handle_frontend_events.cpp
        ../client/sdl_utils.c
        ../client/handle_server_message.cpp
        ../client/video.cpp
        ../client/sync_packets.cpp
        ../client/renderer.c
        ../client/frontend/frontend.c
        ../client/frontend/sdl/audio.cpp
        ../client/frontend/sdl/events.cpp
        ../client/frontend/sdl/lifecycle.cpp
        ../client/frontend/sdl/sdl.c
        ../client/frontend/virtual/virtual.c
        ../client/frontend/virtual/impl.c
        ../client/frontend/virtual/interface.cpp
        ../client/frontend/sdl/video.cpp
        ../client/frontend/sdl/window.cpp
        ../client/frontend/sdl/stdin_parser.c
        $<$<PLATFORM_ID:Windows>:../client/frontend/sdl/d3d11.cpp>

        # Files needed for LodePNG unit tests
        ../whist/utils/lodepng.cpp

        # Debug console
        ../whist/debug/debug_console.cpp
)
endif()

# Protocol test file in source form for the embed-file test.
embed_file(protocol_test.cpp embed_test_object)
# Client embedded files needed by sdl_utils.c.
embed_file(../client/images/error_message_500x50.png insufficient_bandwidth_500x50)
embed_file(../client/images/error_message_750x75.png insufficient_bandwidth_750x75)
embed_file(../client/images/error_message_1000x100.png insufficient_bandwidth_1000x100)
embed_file(../client/images/error_message_1500x150.png insufficient_bandwidth_1500x150)
target_sources(${PROTOCOL_TEST_BINARY} PRIVATE ${EMBED_FILE_LIST})

# Link the GoogleTest library
target_link_libraries(${PROTOCOL_TEST_BINARY} gtest)
target_link_libraries(${PROTOCOL_TEST_BINARY} gmock)

# Link the FFmpeg libraries
foreach(LIB ${FFMPEG_LIBS_PATHS})
    message(VERBOSE "linking lib for ${PROTOCOL_TEST_BINARY}: ${LIB}")
    target_link_libraries(${PROTOCOL_TEST_BINARY} ${LIB})
endforeach()

# Link the threading and dynamic link library if we are on macOS/Linx
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    target_link_libraries(${PROTOCOL_TEST_BINARY} ${CMAKE_DL_LIBS})
endif()

# Link the remaining platform-independent libraries
target_link_libraries(${PROTOCOL_TEST_BINARY}
    ${PLATFORM_INDEPENDENT_SERVER_LIBS}
    ${PLATFORM_INDEPENDENT_LIBS}
)

# Link macOS-specific files and libraries
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_sources(${PROTOCOL_TEST_BINARY} PRIVATE
        ../client/frontend/sdl/native_macos.mm
    )
    target_link_libraries(${PROTOCOL_TEST_BINARY} ${MAC_SPECIFIC_CLIENT_LIBS})
endif()

# Link more Linux-specific libraries
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    target_sources(${PROTOCOL_TEST_BINARY} PRIVATE
        ../client/frontend/sdl/native_x11.c
    )
    find_package(X11 REQUIRED
        Xfixes
        Xdamage
        Xext
        Xtst
    )
    find_package(ALSA REQUIRED)
    include_directories(${X11_INCLUDE_DIR} ${ALSA_INCLUD_DIR})
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

    target_link_libraries(${PROTOCOL_TEST_BINARY}
        ${X11_LIBRARIES}
        ${X11_Xfixes_LIB}
        ${X11_Xdamage_LIB}
        ${X11_Xext_LIB}
        ${X11_Xtst_LIB}
        ${ALSA_LIBRARIES}
        ${DBUS_LIBRARIES}
        GL
        asound
        m
        OpenSSL::Crypto
        atomic
    )
endif()

# Finally, link Windows-specific libraries
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set(CMAKE_EXE_LINKER_FLAGS "/nodefaultlib /nologo /ignore:4099  ")

    target_sources(${PROTOCOL_TEST_BINARY} PRIVATE
        ../client/frontend/sdl/native_windows.cpp
        ../client/frontend/sdl/windows_notification_helper.cpp
    )

    set_property(TARGET ${PROTOCOL_TEST_BINARY} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

    if(CMAKE_CUDA_COMPILER)
        set_property(TARGET ${PROTOCOL_TEST_BINARY} PROPERTY CUDA_ARCHITECTURES OFF)
    endif()

    # Link to system libs
    target_link_libraries(${PROTOCOL_TEST_BINARY} ${WINDOWS_SPECIFIC_CLIENT_LIBS})
endif()

copy_runtime_libs(${PROTOCOL_TEST_BINARY})

# Copy over test-related assets from s3
# To change anything in the test assets, make a new tar file with the
# next version number and upload it to the S3 bucket, then change the
# version number here.  (This ensures that tests on previous branches
# always continue to be usable.)
set(PROTOCOL_TEST_ASSETS_VERSION 2)
message(STATUS "Downloading test assets from AWS S3")
execute_process(
    COMMAND aws s3 cp s3://whist-test-assets/protocol-test-assets-v${PROTOCOL_TEST_ASSETS_VERSION}.tar.gz -
    COMMAND tar -C ${CMAKE_BINARY_DIR}/test -xzf -)
message(STATUS "Downloading test assets from AWS S3 - Completed")

# Run the tests
add_custom_target(RunProtocolTests DEPENDS ${PROTOCOL_TEST_BINARY})


if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin" OR ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    add_custom_command(
            TARGET ${PROTOCOL_TEST_BINARY} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/test/wtest
            ${CMAKE_BINARY_DIR})
    add_custom_command(
            TARGET RunProtocolTests POST_BUILD
            COMMAND ./wtest
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  add_custom_command(
          TARGET RunProtocolTests POST_BUILD
          COMMAND ./${PROTOCOL_TEST_BINARY})
endif()

# Generate code coverage
if(GENERATE_COVERAGE)
    add_custom_command(
        TARGET RunProtocolTests POST_BUILD
        COMMAND lcov --capture --directory ${CMAKE_BINARY_DIR} --output-file ${CMAKE_BINARY_DIR}/coverage.info
        COMMAND lcov --remove ${CMAKE_BINARY_DIR}/coverage.info -o ${CMAKE_BINARY_DIR}/coverage.info '${CMAKE_BINARY_DIR}/*' '/Applications/*' '/usr/include/c++/9/*' '/Library/*' '${CMAKE_SOURCE_DIR}/test' # Remove non-Whist files
        COMMAND lcov --list ${CMAKE_BINARY_DIR}/coverage.info # print verbose coverage information
    )
endif()

# Upload code coverage to Codecov
if(UPLOAD_COVERAGE)
    add_custom_command(
        TARGET RunProtocolTests POST_BUILD
        COMMAND ${CMAKE_BINARY_DIR}/codecov --sha $ENV{GITHUB_SHA} --slug whisthq/whist -t $ENV{CODECOV_TOKEN} -c -F protocol
    )
endif()

add_custom_target(test DEPENDS RunProtocolTests)

add_subdirectory(programs)
