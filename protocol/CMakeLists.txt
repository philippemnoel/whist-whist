#[[
############################### Whist Protocol ###############################
This is the root CmakeLists.txt for the protocol repository. We define various
project level settings here as well as include the subdirectories of the
repository. We also look for the protocol libraries here (FFmpeg, SDL, etc.).

To compile on Windows with MSVC, set cmake_minimum_required(VERSION 3.15)
and uncomment cmake_policy(SET CMP0091 NEW).

Additional VERBOSE message output is a feature of Cmake v3.15 and later. If
you use an earlier version, the verbose messages will be printed regardless
of whether verbose output is set to ON or OFF.
]]

cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0091 NEW)
include(CheckLanguage)
include(FetchContent)
include(CMakeDependentOption)

project(WhistProtocol LANGUAGES C CXX)

# Generate compile_commands.json, which can make IDE's intellisense more accurate
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    check_language(CUDA)
    # CMAKE_CUDA_COMPILER will be set only if check_language(CUDA) succeeded
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        add_definitions(-DWHIST_CUDA_ENABLED)
    endif()
endif()

# Special work-around for getting include_directories(SYSTEM path) to work on MSVC
# Found from https://www.reddit.com/r/cpp/comments/elewps/msvc_isystem/
# Waiting on https://gitlab.kitware.com/cmake/cmake/-/issues/17904 to be merged into master cmake branch
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS
    "$<$<COMPILE_LANG_AND_ID:C,MSVC>:/external:W0;/experimental:external>")

if(MSVC)
    set(CMAKE_INCLUDE_SYSTEM_FLAG_C "/external:I ")
endif()

option(SANITIZE "Enable sanitizer during build (address, thread, undefined)." OFF)
option(CLIENT_SHARED_LIB "Build the client as a shared library" OFF)
option(USE_CCACHE "Use ccache to speed up builds" OFF)
option(USE_MIMALLOC "Link the client to mimalloc" ON)
if(USE_MIMALLOC AND ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    add_compile_definitions(USING_MLOCK=true)
else()
    add_compile_definitions(USING_MLOCK=false)
endif()

if(USE_CCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER "ccache")
    set(CMAKE_CXX_COMPILER_LAUNCHER "ccache")
    set(CMAKE_CUDA_COMPILER_LAUNCHER "ccache")
    # Unclear whether we need these two, as I think we just treat these as C/CXX
    set(CMAKE_OBJC_COMPILER_LAUNCHER "ccache")
    set(CMAKE_OBJCXX_COMPILER_LAUNCHER "ccache")
endif()

option(NOGPU "Disable GPU acceleration" OFF)
if(NOGPU)
    add_compile_definitions(NOGPU=true)
endif()

# Add our compiler flags
include(cmake/compiler-options.cmake)

# Add rpath to look for shared libraries on linux
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(CMAKE_SKIP_BUILD_RPATH  FALSE)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    set(CMAKE_INSTALL_RPATH $ORIGIN)
endif()

# Add rpath to look for shared libraries on mac
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(CMAKE_SKIP_BUILD_RPATH  FALSE)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    set(CMAKE_INSTALL_RPATH @executable_path)
endif()

set(META_PROJECT_NAME        "Protocol")
set(META_PROJECT_DESCRIPTION "CMake Project for the Whist client protocol clients on mac, linux and windows
                              and servers on windows and linux.")
set(META_AUTHOR_ORGANIZATION "Whist")
set(META_AUTHOR_DOMAIN       "https://github.com/whisthq/whist")

set(CMAKE_C_STANDARD 11)
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows" AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.20.4)
    # On such systems, only 23 enables /std:c++latest, which is what we need
    set(CMAKE_CXX_STANDARD 23)
else()
    set(CMAKE_CXX_STANDARD 20)
endif()

# verbose cmake outputs, useful for debugging CMake issues such as linking
set(CMAKE_VERBOSE_MAKEFILE OFF)

# Set up clang-format
# clang-format can be called with make clang-format
include(cmake/clang-format.cmake)
# A small module which sets the default build type to debug if a .git is present and release otherwise.
include(cmake/default-build.cmake)

option(CHECK_CI "Run checks for the CI environment" OFF)
if(CHECK_CI)
    message(STATUS "Automatically enabling checks for the CI environment")
    cmake_dependent_option(RUN_CPPCHECK "Run cppcheck" ON "NOT ${CMAKE_SYSTEM_NAME} MATCHES Windows" OFF)
    cmake_dependent_option(GENERATE_COVERAGE "Generate code coverage with lcov" ON "NOT ${CMAKE_SYSTEM_NAME} MATCHES Windows" OFF)
    cmake_dependent_option(UPLOAD_COVERAGE "Upload code coverage to Codecov" ON "${GENERATE_COVERAGE}" OFF)
else()
    cmake_dependent_option(RUN_CPPCHECK "Run cppcheck" OFF "NOT ${CMAKE_SYSTEM_NAME} MATCHES Windows" OFF)
    cmake_dependent_option(GENERATE_COVERAGE "Generate code coverage with lcov" OFF "NOT ${CMAKE_SYSTEM_NAME} MATCHES Windows" OFF)
    cmake_dependent_option(UPLOAD_COVERAGE "Upload code coverage to Codecov" OFF "${GENERATE_COVERAGE}" OFF)
endif()

# We configure coverage here, since we don't want this flag to be passed to gtest.
if(GENERATE_COVERAGE)
    message(STATUS "Enabling code coverage generation.")
    set(CMAKE_C_FLAGS --coverage)
    set(CMAKE_CXX_FLAGS --coverage)
endif()

if(UPLOAD_COVERAGE)
    message(STATUS "Enabling code coverage uploading.")
    message(STATUS "Downloading Codecov to ${CMAKE_BINARY_DIR}/codecov")

    # Download Codecov
    if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        execute_process(COMMAND curl -s https://uploader.codecov.io/latest/macos/codecov -o ${CMAKE_BINARY_DIR}/codecov)
    else()
        execute_process(COMMAND curl -s https://uploader.codecov.io/latest/linux/codecov -o ${CMAKE_BINARY_DIR}/codecov)
    endif()

    # Make Codecov executable
    execute_process(COMMAND chmod +x ${CMAKE_BINARY_DIR}/codecov)

    message(STATUS "Downloading Codecov - done")
endif()

if(RUN_CPPCHECK)
    find_program(CPPCHECK cppcheck HINTS $ENV{PROGRAMFILES}/cppcheck)
    if(CPPCHECK)
        message(STATUS "Detecting cppcheck - found: ${CPPCHECK}")
    else()
        message(STATUS "Detecting cppcheck - not found")
    endif()

    if(CPPCHECK)
        message(STATUS "Detecting cppcheck status - active")
        set(CMAKE_C_CPPCHECK "${CPPCHECK}"
                "--template=\"cppcheck: {file}:{line} ({severity}) {message}\""
                "--enable=performance,portability"
                "--verbose"
                "--std=c11"
                "-j 8" # Run in 8 threads
                "--error-exitcode=1" # Propagate cppcheck errors to cmake
                "--suppress=shiftTooManyBitsSigned"
                "--suppress=*:${CMAKE_SOURCE_DIR}/whist/clipboard/clipboard_osx.m" # suppress all warnings for clipboard_osx.m since cppcheck isn't supported on Objective-C
                "--suppress=*:${CMAKE_SOURCE_DIR}/whist/utils/lodepng.*" # suppress all lodepng warnings, since this is an external library that we should not touch
                "--suppress=*:${CMAKE_SOURCE_DIR}/test/lodepng_test.*" # suppress all lodepng warnings, since this is an external library that we should not touch
                "--inline-suppr" # Allow cppcheck-suppress comments in code
                )
    else()
        message(STATUS "Detecting cppcheck status - inactive")
    endif()
endif()

# export compile commands database - not needed if not running clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
message(STATUS "Exporting compile commands for clang-tidy - done")

set(arch 64)
set(CMAKE_INSTALL_LIBDIR lib)

# on macOS, detect if we are building arm64 or x86_64
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(MACOS_TARGET_ARCHITECTURE AUTO CACHE STRING "The target architecture for macOS builds (arm64/x86_64/auto)")
    if(${MACOS_TARGET_ARCHITECTURE} MATCHES "auto|AUTO")
        # Detect from the current system
        # should return "arm64" or "x86_64"
        execute_process(COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE MACOS_ARCHITECTURE)
    else()
        string(TOLOWER "${MACOS_TARGET_ARCHITECTURE}" MACOS_ARCHITECTURE)
    endif()
    set(CMAKE_OSX_ARCHITECTURES "${MACOS_ARCHITECTURE}")
    message(STATUS "Detecting macOS Architecture: ${MACOS_ARCHITECTURE}")
endif()

# Get Bash on all OSs
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    # Find git
    find_program(GIT git REQUIRED)
    # Find git's path
    get_filename_component(GIT_DIR ${GIT} DIRECTORY)
    # Look for gitbash based on git's path
    # NO_DEFAULT_PATH to avoid finding WSL bash, or any other bash
    find_program(BASH bash PATHS "${GIT_DIR}/../bin" NO_DEFAULT_PATH)

    if(NOT BASH)
        if(EXISTS "C:\\Windows\\System32\\wsl.exe")
            set(WSL "C:\\Windows\\System32\\wsl.exe")
            set(BASH "/bin/bash")
        else()
            message(FATAL_ERROR "Neither Git Bash nor WSL Bash were found! For WSL bash, make sure to create a symlink from C:\\Windows\\SysWOW65\\wsl.exe to C:\\Windows\\System32\\wsl.exe if you are on a 32-bit CMake install.")
        endif()
    endif()
else()
    set(BASH "/bin/bash")
endif()

# Download AWS S3 binaries unless indicated otherwise
option(DOWNLOAD_BINARIES "download binaries from S3 via AWS CLI" ON)
if(DOWNLOAD_BINARIES)
    message(STATUS "Downloading Whist Protocol binaries from AWS S3")
    if(WSL)
        set(download_args ${CMAKE_SYSTEM_NAME} "`wslpath ${CMAKE_BINARY_DIR}`" ${CLIENT_SHARED_LIB})
        execute_process(COMMAND ${WSL} ${BASH} "`wslpath ${CMAKE_CURRENT_SOURCE_DIR}/download-binaries.sh`" ${download_args}
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        RESULT_VARIABLE download_result)
    else()
        set(download_args ${CMAKE_SYSTEM_NAME} ${CMAKE_BINARY_DIR} ${MACOS_ARCHITECTURE} ${CLIENT_SHARED_LIB})
        execute_process(COMMAND ${BASH} ${CMAKE_CURRENT_SOURCE_DIR}/download-binaries.sh ${download_args}
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        RESULT_VARIABLE download_result)
    endif()
    if(NOT ${download_result} EQUAL "0")
        message(FATAL_ERROR "Failed to download binaries from AWS S3: exitcode: ${download_result}")
    endif()
endif()

# FFmpeg: Use the FFmpeg libs in /lib/${arch}/ffmpeg
set(FFMPEG_DIR_PATH "${CMAKE_BINARY_DIR}/lib/${arch}/ffmpeg/${CMAKE_SYSTEM_NAME}")

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(FFMPEG_LIBS libswresample.4.dylib;libavcodec.59.dylib;libavdevice.59.dylib;libavfilter.8.dylib;
                    libavformat.59.dylib;libswscale.6.dylib;libavutil.57.dylib)
else()
    set(FFMPEG_LIBS swresample;avcodec;avdevice;avfilter;avformat;swscale;avutil)
endif()

set(FFMPEG_LIBS_PATHS "")
foreach(LIB ${FFMPEG_LIBS})
    message(STATUS "Looking for FFmpeg lib: ${LIB}")
    set(LIB_VAR "LIB_${LIB}")
    find_library(${LIB_VAR}
            NAMES ${LIB}
            PATHS ${FFMPEG_DIR_PATH}
            NO_DEFAULT_PATH
            NO_CMAKE_ENVIRONMENT PATH
            NO_CMAKE_PATH
            NO_SYSTEM_ENVIRONMENT_PATH
            NO_CMAKE_SYSTEM_PATH
            NO_CMAKE_FIND_ROOT_PATH)
    message(VERBOSE "Found FFmpeg lib: ${LIB_VAR}")
    list(APPEND FFMPEG_LIBS_PATHS ${${LIB_VAR}})
endforeach()
message(VERBOSE "FFMPEG PATHS ${FFMPEG_LIBS_PATHS}")
list(APPEND SHARED_LIBS_PATHS ${FFMPEG_DIR_PATH})


#[[
################## Libraries we supply ##################
]]

# the NO_* paramaters tell cmake to only look for our included libs and not system wide ones.
# For linux we use the system openssl/libcrypto. For mac and windows we bundle our own because is not included with the os
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(OPENSSL_DIR_PATH "${CMAKE_BINARY_DIR}/lib/${arch}/openssl/${CMAKE_SYSTEM_NAME}")
    find_library(LIB_OPENSSL NAMES crypto libcrypto PATHS ${OPENSSL_DIR_PATH}
            NO_DEFAULT_PATH
            NO_CMAKE_ENVIRONMENT PATH
            NO_CMAKE_PATH
            NO_SYSTEM_ENVIRONMENT_PATH
            NO_CMAKE_SYSTEM_PATH
            NO_CMAKE_FIND_ROOT_PATH)

    if(NOT LIB_OPENSSL)
        message(FATAL_ERROR "Library openssl was not found! ${OPENSSL_DIR_PATH}")
    endif()

    # We build as a static library on macOS and a shared library on
    # Windows, so only copy to the output directory on Windows.
    if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
        list(APPEND SHARED_LIBS_PATHS ${OPENSSL_DIR_PATH})
    endif()
else()
    find_package(OpenSSL REQUIRED)
endif()

set(SENTRY_DIR_PATH "${CMAKE_BINARY_DIR}/lib/${arch}/sentry/${CMAKE_SYSTEM_NAME}")
find_library(LIB_SENTRY NAMES sentry PATHS "${SENTRY_DIR_PATH}"
    NO_DEFAULT_PATH
    NO_CMAKE_ENVIRONMENT_PATH
    NO_CMAKE_PATH
    NO_SYSTEM_ENVIRONMENT_PATH
    NO_CMAKE_SYSTEM_PATH
    NO_CMAKE_FIND_ROOT_PATH)
if(NOT LIB_SENTRY)
    message(FATAL_ERROR "Library Sentry was not found! ${SENTRY_DIR_PATH}")
endif()
list(APPEND SHARED_LIBS_PATHS ${SENTRY_DIR_PATH})

set(SDL2_DIR_PATH "${CMAKE_BINARY_DIR}/lib/${arch}/SDL2/${CMAKE_SYSTEM_NAME}")
find_library(STATIC_SDL2 NAMES SDL2 SDL2-static PATHS "${SDL2_DIR_PATH}"
        NO_DEFAULT_PATH
        NO_CMAKE_ENVIRONMENT_PATH
        NO_CMAKE_PATH
        NO_SYSTEM_ENVIRONMENT_PATH
        NO_CMAKE_SYSTEM_PATH
        NO_CMAKE_FIND_ROOT_PATH)
if(NOT STATIC_SDL2)
    message(FATAL_ERROR "Library STATIC_SDL2 was not found! ${SDL2_DIR_PATH}")
endif()

function(copy_runtime_libs TARGET)
    foreach(DIR ${SHARED_LIBS_PATHS})
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${DIR}
            $<TARGET_FILE_DIR:${TARGET}>)
    endforeach()
endfunction()

fetchcontent_declare(
    googletest
    GIT_REPOSITORY "https://github.com/google/googletest.git"
    GIT_TAG "release-1.11.0"
)
# From GoogleTests' official README: prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

#Set macOS SDK
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    # Use the latest sysroot
    set(CMAKE_OSX_SYSROOT /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk)
    # Use an older deployment target so all systems will be happy
    if(MACOS_ARCHITECTURE MATCHES "arm64")
      # We have to keep this 10.13 instead of 11.0 because of the deprecated
      # notifications API usage.
      set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13")
    else()
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13")
    endif()
endif()

fetchcontent_makeavailable(googletest)

# Include embed-file function for use in subdirectories.
include(cmake/embed-file.cmake)

# Tell CMake where to look for includes
# Use SYSTEM flag to not get warnings from their files
include_directories(${CMAKE_SOURCE_DIR}) # For #include <whist/**> semantics
include_directories(SYSTEM include)
include_directories(SYSTEM ${CMAKE_BINARY_DIR}/include)
include_directories(SYSTEM ${CMAKE_BINARY_DIR}/include/ffmpeg)

# Here is where we tell cmake to look in subdirectories for further CMakeLists.txt
add_subdirectory(whist)

# Set variables to be used by the CMakeLists in the subdirectories
set(PLATFORM_INDEPENDENT_LIBS   whistCore
    whistAudio
    whistVideo
    whistClipboard
    whistCursor
    whistFile
    whistNetwork
    whistLogging
    whistUtils
    whistDebug
    whistFEC
    ${STATIC_SDL2}
    ${LIB_SENTRY}
)
set(MAC_SPECIFIC_CLIENT_LIBS
    "-framework OpenGL"
    "-framework VideoToolbox"
    "-framework CoreMedia"
    "-framework CoreAudio"
    "-framework CoreHaptics"
    "-framework AudioToolbox"
    "-framework IOKit"
    "-framework ForceFeedback"
    "-framework Carbon"
    "-framework AppKit"
    "-framework Metal"
    "-framework QuartzCore"
    "-framework GameController"
    "-framework Foundation"
    objc
    iconv
    ${LIB_OPENSSL}
)

# MSVC C/C++ Standard Library linking, based on Debug/Release
set(WINDOWS_CORE_LIBS
    $<IF:$<CONFIG:Debug>,libucrtd,libucrt>
    $<IF:$<CONFIG:Debug>,libcmtd,libcmt>
    $<IF:$<CONFIG:Debug>,libcpmtd,libcpmt>
    $<IF:$<CONFIG:Debug>,vcruntimed,vcruntime>
)

# Windows Libs Shared by both server and client go here
set(WINDOWS_CORE_LIBS
    ${WINDOWS_CORE_LIBS}
    ${LIB_OPENSSL}
    oldnames # Needed by gtest for things like dup2/creat/etc
    kernel32
    gdi32
    winmm
    imm32
    shell32
    advapi32
    ole32
    oleaut32
    opengl32
    user32
    uuid
    version
    ws2_32
    shlwapi
    crypt32
    d3d11
    dxgi
    dxguid
    shcore
    DbgHelp
    setupapi
    runtimeobject
    pathcch
)

# Client Specific windows libs go here
set(WINDOWS_SPECIFIC_CLIENT_LIBS
    ${WINDOWS_CORE_LIBS}
)

add_subdirectory(client)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin") # Server does not make or compile on MacOS
    set(SERV "WhistServer")
    set(PLATFORM_INDEPENDENT_SERVER_LIBS
        whistCore
        whistAudio
        whistVideo
        whistFile
        whistClipboard
        whistCursor
        whistInput
        whistNetwork
        whistLogging
        whistFEC
        whistDebug
        whistUtils
        ${STATIC_SDL2}
        ${LIB_SENTRY}
    )
    # Server Specific windows libs go here
    set(WINDOWS_SPECIFIC_SERVER_LIBS
        ${WINDOWS_CORE_LIBS}
        wldap32
        normaliz
        winspool
        comdlg32
        odbc32
        odbccp32
        avrt
        d3dcompiler
    )
    add_subdirectory(server)
endif()
# Include the unit tests code
add_subdirectory(test)

# Clang-tidy target
if(WSL)
    message(STATUS "Native bash not found (only WSL), clang-tidy will not be an option")
else()
    add_custom_target(clang-tidy
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMAND ${BASH} ${CMAKE_SOURCE_DIR}/run-clang-tidy.sh -c
        DEPENDS WhistClient ${SERV})
endif()

# Documentation target
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    add_custom_target(docs
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMAND mkdir -p ${CMAKE_BINARY_DIR}/docs
        COMMAND cp ${CMAKE_SOURCE_DIR}/docs/Doxyfile ${CMAKE_BINARY_DIR}/docs
        COMMAND ${CMAKE_COMMAND} -E echo "OUTPUT_DIRECTORY = ${CMAKE_BINARY_DIR}/docs" >> ${CMAKE_BINARY_DIR}/docs/Doxyfile
        COMMAND doxygen ${CMAKE_BINARY_DIR}/docs/Doxyfile
        )
endif()
