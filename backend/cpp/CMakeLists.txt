cmake_minimum_required(VERSION 3.10)

# Define project
project(HelloWorld)

# Create executable
add_executable(helloworld main.cpp)

set(CMAKE_EXE_LINKER_FLAGS "-static -static-libstdc++ -static-libgcc")
