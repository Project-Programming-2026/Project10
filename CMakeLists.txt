
cmake_minimum_required(VERSION 3.20)
project(Project10 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 同梱ヘッダーだけを使用し、構成時のダウンロードを行わない。
add_library(project10_json INTERFACE)
target_include_directories(project10_json SYSTEM INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)
add_library(nlohmann_json::nlohmann_json ALIAS project10_json)

add_library(project10_core
  src/io/config.cpp
  src/io/csv.cpp
  src/simulation/players.cpp
  src/simulation/latent_parameters.cpp
)
target_include_directories(project10_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(project10_core PRIVATE nlohmann_json::nlohmann_json)
if(MSVC)
  target_compile_options(project10_core PRIVATE /W4 /utf-8)
else()
  target_compile_options(project10_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(project10 src/main.cpp)
target_link_libraries(project10 PRIVATE project10_core)
if(MSVC)
  target_compile_options(project10 PRIVATE /utf-8)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(simulation_tests tests/simulation_tests.cpp)
  target_link_libraries(simulation_tests PRIVATE project10_core)
  if(MSVC)
    target_compile_options(simulation_tests PRIVATE /utf-8)
  endif()
  add_test(NAME simulation_unit COMMAND simulation_tests)

  foreach(case generation reproducibility invalid_input invalid_config duplicate_keys missing_config overwrite)
    add_test(NAME cli_${case}
      COMMAND "${CMAKE_COMMAND}"
        "-DPROGRAM=$<TARGET_FILE:project10>"
        "-DSAMPLE_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/configs/run_0000/config.json"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/test-work"
        "-DCASE=${case}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/generation.cmake"
    )
  endforeach()
endif()
