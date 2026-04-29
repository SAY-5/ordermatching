cmake_minimum_required(VERSION 3.20)
project(ordermatching CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

add_library(ordermatching INTERFACE)
target_include_directories(ordermatching INTERFACE include)

add_executable(matchcli src/matchcli.cpp)
target_link_libraries(matchcli PRIVATE ordermatching)

enable_testing()
add_subdirectory(tests)
