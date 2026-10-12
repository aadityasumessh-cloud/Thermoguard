cmake_minimum_required(VERSION 3.14)
project(plant_control_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Optional sanitizers: -DSANITIZE=thread | address,undefined
set(SANITIZE "" CACHE STRING "Sanitizers to enable, e.g. thread or address,undefined")
if(SANITIZE)
  add_compile_options(-fsanitize=${SANITIZE} -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=${SANITIZE})
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)

add_library(plant_core STATIC
  src/net.cpp
  src/command_server.cpp
  src/scheduler.cpp
  src/control_system.cpp
  src/commands.cpp)
target_include_directories(plant_core PUBLIC include)
target_link_libraries(plant_core PUBLIC Threads::Threads)

add_executable(plant_controller src/plant_controller.cpp)
target_link_libraries(plant_controller PRIVATE plant_core)

add_executable(plantctl src/plantctl.cpp)
target_link_libraries(plantctl PRIVATE plant_core)

include(CTest)
if(BUILD_TESTING)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(plant_tests
    tests/test_bounded_queue.cpp
    tests/test_pid_and_plant.cpp
    tests/test_alarm.cpp
    tests/test_scheduler.cpp
    tests/test_network.cpp
    tests/test_control_system.cpp
    tests/test_commands.cpp)
  target_link_libraries(plant_tests PRIVATE plant_core GTest::gtest_main)

  include(GoogleTest)
  gtest_discover_tests(plant_tests DISCOVERY_TIMEOUT 60)
endif()
