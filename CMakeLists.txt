cmake_minimum_required(VERSION 3.0)
project(ttylm)
file(GLOB sources CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/*.c)
add_executable(ttylm ${sources})
