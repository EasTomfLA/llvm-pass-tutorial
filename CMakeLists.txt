cmake_minimum_required(VERSION 3.4)
project(llvm-pass-tutorial)

# 项目根目录
get_filename_component(PROJECT_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR} ABSOLUTE)
message(STATUS "项目根目录: ${PROJECT_ROOT_DIR}")

# 设置 NDK 根目录
set(NDK_BASE_DIR ${PROJECT_ROOT_DIR}/../android-ndk-r25c-linux)
message(STATUS "NDK 基础目录: ${NDK_BASE_DIR}")

# 检查 NDK 基础目录是否存在
if(NOT EXISTS ${NDK_BASE_DIR})
    message(FATAL_ERROR "NDK 基础目录不存在: ${NDK_BASE_DIR}")
endif()

# 假设 android-ndk 放在项目根目录或可以用相对路径引用
set(NDK_PATH ${NDK_BASE_DIR}/android-ndk-r25c)
set(LLVM_TOOLCHAIN_PATH ${NDK_PATH}/toolchains/llvm/prebuilt/linux-x86_64)

# llvm14
set(CMAKE_C_COMPILER ${LLVM_TOOLCHAIN_PATH}/bin/clang)
set(CMAKE_CXX_COMPILER ${LLVM_TOOLCHAIN_PATH}//bin/clang++)
set(ENV{LLVM_HOME} ${LLVM_TOOLCHAIN_PATH})

# we need LLVM_HOME in order not automatically set LLVM_DIR
if(NOT DEFINED ENV{LLVM_HOME})
    message(FATAL_ERROR "$LLVM_HOME is not defined")
endif()

if(NOT DEFINED ENV{LLVM_DIR})
    set(ENV{LLVM_DIR} $ENV{LLVM_HOME}/lib64/cmake/llvm)
endif()

find_package(LLVM REQUIRED CONFIG)
add_definitions(${LLVM_DEFINITIONS})

link_directories(${LLVM_LIBRARY_DIRS})

if (${LLVM_VERSION_MAJOR} VERSION_GREATER_EQUAL 10)
    set(CMAKE_CXX_STANDARD 14)
endif ()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
set(CMAKE_SKIP_RPATH ON)

# add_subdirectory(skeleton)  # Use your pass name here.
# add_subdirectory(ollvm)  # ollvm
add_subdirectory(Hikari)  # Hikari
# add_subdirectory(Armariris)  # Armariris
