# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.17

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Disable VCS-based implicit rules.
% : %,v


# Disable VCS-based implicit rules.
% : RCS/%


# Disable VCS-based implicit rules.
% : RCS/%,v


# Disable VCS-based implicit rules.
% : SCCS/s.%


# Disable VCS-based implicit rules.
% : s.%


.SUFFIXES: .hpux_make_needs_suffix_list


# Produce verbose output by default.
VERBOSE = 1

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

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
CMAKE_COMMAND = /usr/local/Cellar/cmake/3.17.2/bin/cmake

# The command to remove a file.
RM = /usr/local/Cellar/cmake/3.17.2/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /Users/Philippe/Downloads/protocol

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /Users/Philippe/Downloads/protocol/build

# Include any dependencies generated for this target.
include fractal/core/CMakeFiles/fractalCore.dir/depend.make

# Include the progress variables for this target.
include fractal/core/CMakeFiles/fractalCore.dir/progress.make

# Include the compile flags for this target's objects.
include fractal/core/CMakeFiles/fractalCore.dir/flags.make

fractal/core/CMakeFiles/fractalCore.dir/fractal.c.o: fractal/core/CMakeFiles/fractalCore.dir/flags.make
fractal/core/CMakeFiles/fractalCore.dir/fractal.c.o: ../fractal/core/fractal.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/Users/Philippe/Downloads/protocol/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object fractal/core/CMakeFiles/fractalCore.dir/fractal.c.o"
	cd /Users/Philippe/Downloads/protocol/build/fractal/core && /usr/local/opt/llvm/bin/clang-10 $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/fractalCore.dir/fractal.c.o   -c /Users/Philippe/Downloads/protocol/fractal/core/fractal.c

fractal/core/CMakeFiles/fractalCore.dir/fractal.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/fractalCore.dir/fractal.c.i"
	cd /Users/Philippe/Downloads/protocol/build/fractal/core && /usr/local/opt/llvm/bin/clang-10 $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /Users/Philippe/Downloads/protocol/fractal/core/fractal.c > CMakeFiles/fractalCore.dir/fractal.c.i

fractal/core/CMakeFiles/fractalCore.dir/fractal.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/fractalCore.dir/fractal.c.s"
	cd /Users/Philippe/Downloads/protocol/build/fractal/core && /usr/local/opt/llvm/bin/clang-10 $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /Users/Philippe/Downloads/protocol/fractal/core/fractal.c -o CMakeFiles/fractalCore.dir/fractal.c.s

# Object files for target fractalCore
fractalCore_OBJECTS = \
"CMakeFiles/fractalCore.dir/fractal.c.o"

# External object files for target fractalCore
fractalCore_EXTERNAL_OBJECTS =

fractal/core/libfractalCore.a: fractal/core/CMakeFiles/fractalCore.dir/fractal.c.o
fractal/core/libfractalCore.a: fractal/core/CMakeFiles/fractalCore.dir/build.make
fractal/core/libfractalCore.a: fractal/core/CMakeFiles/fractalCore.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/Users/Philippe/Downloads/protocol/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking C static library libfractalCore.a"
	cd /Users/Philippe/Downloads/protocol/build/fractal/core && $(CMAKE_COMMAND) -P CMakeFiles/fractalCore.dir/cmake_clean_target.cmake
	cd /Users/Philippe/Downloads/protocol/build/fractal/core && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/fractalCore.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
fractal/core/CMakeFiles/fractalCore.dir/build: fractal/core/libfractalCore.a

.PHONY : fractal/core/CMakeFiles/fractalCore.dir/build

fractal/core/CMakeFiles/fractalCore.dir/clean:
	cd /Users/Philippe/Downloads/protocol/build/fractal/core && $(CMAKE_COMMAND) -P CMakeFiles/fractalCore.dir/cmake_clean.cmake
.PHONY : fractal/core/CMakeFiles/fractalCore.dir/clean

fractal/core/CMakeFiles/fractalCore.dir/depend:
	cd /Users/Philippe/Downloads/protocol/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /Users/Philippe/Downloads/protocol /Users/Philippe/Downloads/protocol/fractal/core /Users/Philippe/Downloads/protocol/build /Users/Philippe/Downloads/protocol/build/fractal/core /Users/Philippe/Downloads/protocol/build/fractal/core/CMakeFiles/fractalCore.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : fractal/core/CMakeFiles/fractalCore.dir/depend

