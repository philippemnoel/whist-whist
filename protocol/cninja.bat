@echo off

REM Script to build the Fractal protocol on Windows

REM cd to parent directory of this script
cd %~dp0

mkdir build
cmake -S . -B build/ -G Ninja -D CMAKE_BUILD_TYPE=Debug
cd build
ninja
