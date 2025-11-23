cmake_minimum_required(VERSION 3.10)
project(Raytracer)

set(CMAKE_CXX_STANDARD 17)

find_package(glfw3 3.3 REQUIRED)

# Normal
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

# Guided
add_executable(Raytracer2
    src/mainBVH.cc
    src/glad.c
)

target_include_directories(Raytracer2 PRIVATE include)
target_link_libraries(Raytracer2 PRIVATE glfw)

if(APPLE)
    target_link_libraries(Raytracer2 PRIVATE "-framework OpenGL")
elseif(UNIX)
    find_package(OpenGL REQUIRED)
    target_link_libraries(Raytracer2 PRIVATE OpenGL::GL)
endif()

# Test
add_executable(Raytracer3
    src/mainOwnBVH.cc
    src/glad.c
)

target_include_directories(Raytracer3 PRIVATE include)
target_link_libraries(Raytracer3 PRIVATE glfw)

if(APPLE)
    target_link_libraries(Raytracer3 PRIVATE "-framework OpenGL")
elseif(UNIX)
    find_package(OpenGL REQUIRED)
    target_link_libraries(Raytracer3 PRIVATE OpenGL::GL)
endif()
