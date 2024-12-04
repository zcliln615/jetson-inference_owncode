# require CMake 2.8 or greater
cmake_minimum_required(VERSION 2.8)

# declare my project
project(posent)

# import jetson-inference and jetson-utils packages.
find_package(jetson-utils)
find_package(jetson-inference)
find_package(VPI 2.0)

# CUDA is required
find_package(CUDA)

# add directory for libnvbuf-utils to program
link_directories(/usr/lib/aarch64-linux-gnu/tegra)

# compile the my-peosenet program
cuda_add_executable(posenet posenet.cpp)

# link my-posenet to jetson-inference library
target_link_libraries(posenet jetson-inference)