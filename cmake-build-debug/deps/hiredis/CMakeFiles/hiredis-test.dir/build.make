# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.23

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

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake.exe

# The command to remove a file.
RM = /usr/bin/cmake.exe -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /cygdrive/d/workspace/github/fucking-redis

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /cygdrive/d/workspace/github/fucking-redis/cmake-build-debug

# Include any dependencies generated for this target.
include deps/hiredis/CMakeFiles/hiredis-test.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include deps/hiredis/CMakeFiles/hiredis-test.dir/compiler_depend.make

# Include the progress variables for this target.
include deps/hiredis/CMakeFiles/hiredis-test.dir/progress.make

# Include the compile flags for this target's objects.
include deps/hiredis/CMakeFiles/hiredis-test.dir/flags.make

deps/hiredis/CMakeFiles/hiredis-test.dir/test.c.o: deps/hiredis/CMakeFiles/hiredis-test.dir/flags.make
deps/hiredis/CMakeFiles/hiredis-test.dir/test.c.o: ../deps/hiredis/test.c
deps/hiredis/CMakeFiles/hiredis-test.dir/test.c.o: deps/hiredis/CMakeFiles/hiredis-test.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/cygdrive/d/workspace/github/fucking-redis/cmake-build-debug/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object deps/hiredis/CMakeFiles/hiredis-test.dir/test.c.o"
	cd /cygdrive/d/workspace/github/fucking-redis/cmake-build-debug/deps/hiredis && /usr/bin/gcc.exe $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -MD -MT deps/hiredis/CMakeFiles/hiredis-test.dir/test.c.o -MF CMakeFiles/hiredis-test.dir/test.c.o.d -o CMakeFiles/hiredis-test.dir/test.c.o -c /cygdrive/d/workspace/github/fucking-redis/deps/hiredis/test.c

deps/hiredis/CMakeFiles/hiredis-test.dir/test.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/hiredis-test.dir/test.c.i"
	cd /cygdrive/d/workspace/github/fucking-redis/cmake-build-debug/deps/hiredis && /usr/bin/gcc.exe $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /cygdrive/d/workspace/github/fucking-redis/deps/hiredis/test.c > CMakeFiles/hiredis-test.dir/test.c.i

deps/hiredis/CMakeFiles/hiredis-test.dir/test.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/hiredis-test.dir/test.c.s"
	cd /cygdrive/d/workspace/github/fucking-redis/cmake-build-debug/deps/hiredis && /usr/bin/gcc.exe $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /cygdrive/d/workspace/github/fucking-redis/deps/hiredis/test.c -o CMakeFiles/hiredis-test.dir/test.c.s

# Object files for target hiredis-test
hiredis__test_OBJECTS = \
"CMakeFiles/hiredis-test.dir/test.c.o"

# External object files for target hiredis-test
hiredis__test_EXTERNAL_OBJECTS =

../src/hiredis-test.exe: deps/hiredis/CMakeFiles/hiredis-test.dir/test.c.o
../src/hiredis-test.exe: deps/hiredis/CMakeFiles/hiredis-test.dir/build.make
../src/hiredis-test.exe: deps/hiredis/libhiredis.dll.a
../src/hiredis-test.exe: deps/hiredis/CMakeFiles/hiredis-test.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/cygdrive/d/workspace/github/fucking-redis/cmake-build-debug/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking C executable ../../../src/hiredis-test.exe"
	cd /cygdrive/d/workspace/github/fucking-redis/cmake-build-debug/deps/hiredis && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/hiredis-test.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
deps/hiredis/CMakeFiles/hiredis-test.dir/build: ../src/hiredis-test.exe
.PHONY : deps/hiredis/CMakeFiles/hiredis-test.dir/build

deps/hiredis/CMakeFiles/hiredis-test.dir/clean:
	cd /cygdrive/d/workspace/github/fucking-redis/cmake-build-debug/deps/hiredis && $(CMAKE_COMMAND) -P CMakeFiles/hiredis-test.dir/cmake_clean.cmake
.PHONY : deps/hiredis/CMakeFiles/hiredis-test.dir/clean

deps/hiredis/CMakeFiles/hiredis-test.dir/depend:
	cd /cygdrive/d/workspace/github/fucking-redis/cmake-build-debug && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /cygdrive/d/workspace/github/fucking-redis /cygdrive/d/workspace/github/fucking-redis/deps/hiredis /cygdrive/d/workspace/github/fucking-redis/cmake-build-debug /cygdrive/d/workspace/github/fucking-redis/cmake-build-debug/deps/hiredis /cygdrive/d/workspace/github/fucking-redis/cmake-build-debug/deps/hiredis/CMakeFiles/hiredis-test.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : deps/hiredis/CMakeFiles/hiredis-test.dir/depend

