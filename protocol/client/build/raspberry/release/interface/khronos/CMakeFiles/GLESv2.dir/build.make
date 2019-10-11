# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.13

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/pi/Downloads/protocol/client

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/pi/Downloads/protocol/client/build/raspberry/release

# Include any dependencies generated for this target.
include interface/khronos/CMakeFiles/GLESv2.dir/depend.make

# Include the progress variables for this target.
include interface/khronos/CMakeFiles/GLESv2.dir/progress.make

# Include the compile flags for this target's objects.
include interface/khronos/CMakeFiles/GLESv2.dir/flags.make

interface/khronos/CMakeFiles/GLESv2.dir/glxx/glxx_client.c.o: interface/khronos/CMakeFiles/GLESv2.dir/flags.make
interface/khronos/CMakeFiles/GLESv2.dir/glxx/glxx_client.c.o: ../../../interface/khronos/glxx/glxx_client.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/pi/Downloads/protocol/client/build/raspberry/release/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object interface/khronos/CMakeFiles/GLESv2.dir/glxx/glxx_client.c.o"
	cd /home/pi/Downloads/protocol/client/build/raspberry/release/interface/khronos && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/GLESv2.dir/glxx/glxx_client.c.o   -c /home/pi/Downloads/protocol/client/interface/khronos/glxx/glxx_client.c

interface/khronos/CMakeFiles/GLESv2.dir/glxx/glxx_client.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/GLESv2.dir/glxx/glxx_client.c.i"
	cd /home/pi/Downloads/protocol/client/build/raspberry/release/interface/khronos && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/pi/Downloads/protocol/client/interface/khronos/glxx/glxx_client.c > CMakeFiles/GLESv2.dir/glxx/glxx_client.c.i

interface/khronos/CMakeFiles/GLESv2.dir/glxx/glxx_client.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/GLESv2.dir/glxx/glxx_client.c.s"
	cd /home/pi/Downloads/protocol/client/build/raspberry/release/interface/khronos && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/pi/Downloads/protocol/client/interface/khronos/glxx/glxx_client.c -o CMakeFiles/GLESv2.dir/glxx/glxx_client.c.s

# Object files for target GLESv2
GLESv2_OBJECTS = \
"CMakeFiles/GLESv2.dir/glxx/glxx_client.c.o"

# External object files for target GLESv2
GLESv2_EXTERNAL_OBJECTS =

../../lib/libGLESv2.so: interface/khronos/CMakeFiles/GLESv2.dir/glxx/glxx_client.c.o
../../lib/libGLESv2.so: interface/khronos/CMakeFiles/GLESv2.dir/build.make
../../lib/libGLESv2.so: ../../lib/libEGL.so
../../lib/libGLESv2.so: ../../lib/libkhrn_client.a
../../lib/libGLESv2.so: ../../lib/libbcm_host.so
../../lib/libGLESv2.so: ../../lib/libvchostif.a
../../lib/libGLESv2.so: ../../lib/libvchiq_arm.so
../../lib/libGLESv2.so: ../../lib/libvcos.so
../../lib/libGLESv2.so: interface/khronos/CMakeFiles/GLESv2.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/pi/Downloads/protocol/client/build/raspberry/release/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking C shared library ../../../../lib/libGLESv2.so"
	cd /home/pi/Downloads/protocol/client/build/raspberry/release/interface/khronos && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/GLESv2.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
interface/khronos/CMakeFiles/GLESv2.dir/build: ../../lib/libGLESv2.so

.PHONY : interface/khronos/CMakeFiles/GLESv2.dir/build

interface/khronos/CMakeFiles/GLESv2.dir/clean:
	cd /home/pi/Downloads/protocol/client/build/raspberry/release/interface/khronos && $(CMAKE_COMMAND) -P CMakeFiles/GLESv2.dir/cmake_clean.cmake
.PHONY : interface/khronos/CMakeFiles/GLESv2.dir/clean

interface/khronos/CMakeFiles/GLESv2.dir/depend:
	cd /home/pi/Downloads/protocol/client/build/raspberry/release && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/pi/Downloads/protocol/client /home/pi/Downloads/protocol/client/interface/khronos /home/pi/Downloads/protocol/client/build/raspberry/release /home/pi/Downloads/protocol/client/build/raspberry/release/interface/khronos /home/pi/Downloads/protocol/client/build/raspberry/release/interface/khronos/CMakeFiles/GLESv2.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : interface/khronos/CMakeFiles/GLESv2.dir/depend

