cmake_minimum_required(VERSION 3.25)

project(Lmao LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(LmaoWarnings)

add_subdirectory(third_party SYSTEM)

add_subdirectory(engine)
add_subdirectory(game)