cmake_minimum_required(VERSION 3.10)
project(Raytracer)

# ...existing code...
set(CMAKE_CXX_STANDARD 17)

# Enable Release by default if not set
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Fast compiler flags for GCC/Clang
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -O3
        -DNDEBUG
        -march=native
        -ffast-math
        -funroll-loops
        -fno-exceptions
        -fno-rtti
    )
    # Linker optimizations
    add_link_options(
        -Wl,-O1
        -Wl,--as-needed
    )
endif()

# Enable LTO/IPO
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# ...existing code...
find_package(glfw3 3.3 REQUIRED)

add_executable(Raytracer
    src/main.cc
    src/glad.c
)

target_include_directories(Raytracer PRIVATE include)
target_link_libraries(Raytracer PRIVATE glfw)

if(APPLE)
    target_link_libraries(Raytracer PRIVATE "-framework OpenGL")
elseif(UNIX)
    find_package(OpenGL REQUIRED)
    target_link_libraries(Raytracer PRIVATE OpenGL::GL)
endif()