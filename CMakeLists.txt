
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
  src/io/process.cpp
  src/simulation/players.cpp
  src/simulation/latent_parameters.cpp
  src/simulation/probit.cpp
  src/simulation/matches.cpp
  src/ratings/glicko2.cpp
  src/ratings/ranked_glicko2.cpp
  src/simulation/run.cpp
  src/evaluation/MSE.cpp
  src/evaluation/online.cpp
  src/evaluation/reference.cpp
  src/ratings/trueskill.cpp
  src/evaluation/run.cpp
  src/evaluation/reuse.cpp
  src/ratings/glicko.cpp
  src/ratings/elo.cpp
  src/ratings/advantage_elo.cpp
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
add_executable(project10_evaluate src/evaluate_main.cpp)
target_link_libraries(project10_evaluate PRIVATE project10_core)
add_executable(project10_trueskill src/trueskill_main.cpp)
target_link_libraries(project10_trueskill PRIVATE project10_core)
add_executable(project10_glicko src/glicko_main.cpp)
target_link_libraries(project10_glicko PRIVATE project10_core)
add_library(project10_plotting src/evaluation/plot.cpp)
target_link_libraries(project10_plotting PUBLIC project10_core)
add_executable(project10_plot src/plot_main.cpp)
target_link_libraries(project10_plot PRIVATE project10_plotting)
add_executable(project10_predict src/predict_main.cpp)
target_link_libraries(project10_predict PRIVATE project10_core)
add_executable(project10_run src/run_main.cpp)
target_link_libraries(project10_run PRIVATE project10_plotting)
add_dependencies(project10_run project10_predict project10_glicko project10_trueskill)
if(MSVC)
  target_compile_options(project10_predict PRIVATE /utf-8)
  target_compile_options(project10_run PRIVATE /utf-8)
  target_compile_options(project10 PRIVATE /utf-8)
  target_compile_options(project10_evaluate PRIVATE /utf-8)
  target_compile_options(project10_trueskill PRIVATE /utf-8)
  target_compile_options(project10_glicko PRIVATE /utf-8)
  target_compile_options(project10_plotting PRIVATE /W4 /utf-8)
  target_compile_options(project10_plot PRIVATE /utf-8)
else()
  target_compile_options(project10_plotting PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
  find_program(PROJECT10_GNUPLOT NAMES gnuplot gnuplot-nox)
  add_executable(comparison_tests tests/comparison_tests.cpp)
  target_link_libraries(comparison_tests PRIVATE project10_core)
  if(MSVC)
    target_compile_options(comparison_tests PRIVATE /utf-8)
  endif()
  add_test(NAME comparison_unit COMMAND comparison_tests "${CMAKE_CURRENT_BINARY_DIR}/test-work/comparison-unit")
  foreach(mode contract render)
    add_test(NAME comparison_${mode}
      COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:project10_run>"
        "-DBIN=$<TARGET_FILE_DIR:project10_run>" "-DEXE=${CMAKE_EXECUTABLE_SUFFIX}"
        "-DPROBE=$<TARGET_FILE:batch_probe>" "-DGNUPLOT=${PROJECT10_GNUPLOT}"
        "-DMODE=${mode}" "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/test-work"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/comparison.cmake")
  endforeach()
  set_tests_properties(comparison_render PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP: gnuplot")
  add_executable(six_methods_tests tests/six_methods_tests.cpp)
  target_link_libraries(six_methods_tests PRIVATE project10_core)
  if(MSVC)
    target_compile_options(six_methods_tests PRIVATE /utf-8)
  endif()
  add_test(NAME six_methods_unit COMMAND six_methods_tests "${CMAKE_CURRENT_BINARY_DIR}/test-work/six-unit")
  foreach(mode contract render)
    add_test(NAME six_methods_${mode}
      COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:project10_run>"
        "-DBIN=$<TARGET_FILE_DIR:project10_run>" "-DEXE=${CMAKE_EXECUTABLE_SUFFIX}"
        "-DPROBE=$<TARGET_FILE:batch_probe>" "-DGNUPLOT=${PROJECT10_GNUPLOT}"
        "-DMODE=${mode}" "-DSAMPLE_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/configs/run_0008/config.json"
        "-DBASELINE=${CMAKE_CURRENT_SOURCE_DIR}/samples/glicko_baseline"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/test-work"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/six_methods.cmake")
  endforeach()
  set_tests_properties(six_methods_render PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP: gnuplot")
  add_executable(batch_probe tests/batch_probe.cpp)
  target_link_libraries(batch_probe PRIVATE project10_core)
  if(MSVC)
    target_compile_options(batch_probe PRIVATE /utf-8)
  endif()
  add_executable(trueskill_tests tests/trueskill_tests.cpp)
  target_link_libraries(trueskill_tests PRIVATE project10_core)
  if(MSVC)
    target_compile_options(trueskill_tests PRIVATE /utf-8)
  endif()
  add_test(NAME trueskill_unit COMMAND trueskill_tests "${CMAKE_CURRENT_BINARY_DIR}/test-work/trueskill-unit")
  add_test(NAME cli_trueskill
    COMMAND "${CMAKE_COMMAND}"
      "-DPROGRAM=$<TARGET_FILE:project10_trueskill>"
      "-DGENERATOR=$<TARGET_FILE:project10>"
      "-DEVALUATOR=$<TARGET_FILE:project10_evaluate>"
      "-DSAMPLE_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/configs/run_0000/config.json"
      "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/test-work"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/trueskill.cmake"
  )
  add_executable(run_services_tests tests/run_services_tests.cpp)
  target_link_libraries(run_services_tests PRIVATE project10_plotting nlohmann_json::nlohmann_json)
  if(MSVC)
    target_compile_options(run_services_tests PRIVATE /utf-8)
  endif()
  add_test(NAME run_services COMMAND run_services_tests
    "${CMAKE_CURRENT_BINARY_DIR}/test-work"
    "${CMAKE_CURRENT_SOURCE_DIR}/configs/run_0000/config.json"
  )
  add_executable(plot_tests tests/plot_tests.cpp)
  target_link_libraries(plot_tests PRIVATE project10_core)
  add_executable(plot_renderer_stub tests/plot_renderer_stub.cpp)
  if(MSVC)
    target_compile_options(plot_tests PRIVATE /utf-8)
    target_compile_options(plot_renderer_stub PRIVATE /utf-8)
  endif()
  add_test(NAME plot_csv_unit COMMAND plot_tests "${CMAKE_CURRENT_BINARY_DIR}/test-work/plot-csv")
  # 描画プログラムは実行時だけ必要。構成時の取得・必須化は行わない。
  foreach(mode contract render links)
    add_test(NAME reuse_${mode}
      COMMAND "${CMAKE_COMMAND}" "-DPROGRAM=$<TARGET_FILE:project10_run>"
        "-DRENDERER=$<TARGET_FILE:batch_probe>" "-DEXE=${CMAKE_EXECUTABLE_SUFFIX}"
        "-DGNUPLOT=${PROJECT10_GNUPLOT}" "-DMODE=${mode}"
        "-DSAMPLE_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/configs/run_0000/config.json"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/test-work"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/reuse.cmake"
    )
  endforeach()
  set_tests_properties(reuse_render PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP: gnuplot")
  set_tests_properties(reuse_links PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP: symbolic links")
  foreach(mode contract render)
    add_test(NAME batch_${mode}
      COMMAND "${CMAKE_COMMAND}"
        "-DPROGRAM=$<TARGET_FILE:project10_run>" "-DBIN=$<TARGET_FILE_DIR:project10_run>"
        "-DPROBE=$<TARGET_FILE:batch_probe>" "-DEXE=${CMAKE_EXECUTABLE_SUFFIX}"
        "-DGNUPLOT=${PROJECT10_GNUPLOT}" "-DMODE=${mode}"
        "-DSAMPLE_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/configs/run_0000/config.json"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/test-work"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/batch.cmake"
    )
  endforeach()
  set_tests_properties(batch_render PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP: gnuplot")
  foreach(mode IN ITEMS contract render)
    if(mode STREQUAL "contract")
      set(plot_backend "$<TARGET_FILE:plot_renderer_stub>")
    else()
      set(plot_backend "${PROJECT10_GNUPLOT}")
    endif()
    add_test(NAME plot_${mode}
      COMMAND "${CMAKE_COMMAND}"
        "-DPROGRAM=$<TARGET_FILE:project10_plot>"
        "-DGNUPLOT=${plot_backend}"
        "-DSAMPLE_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/configs/run_0000/config.json"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/test-work"
        "-DMODE=${mode}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/plot.cmake"
    )
  endforeach()
  set_tests_properties(plot_render PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP: gnuplot")
  add_executable(evaluation_tests tests/evaluation_tests.cpp)
  target_link_libraries(evaluation_tests PRIVATE project10_core)
  if(MSVC)
    target_compile_options(evaluation_tests PRIVATE /utf-8)
  endif()
  add_test(NAME evaluation_unit COMMAND evaluation_tests "${CMAKE_CURRENT_BINARY_DIR}/test-work")
  add_test(NAME cli_evaluation
    COMMAND "${CMAKE_COMMAND}"
      "-DPROGRAM=$<TARGET_FILE:project10_evaluate>"
      "-DSAMPLE_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/configs/run_0000/config.json"
      "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/test-work"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/evaluation.cmake"
  )
  add_executable(simulation_tests tests/simulation_tests.cpp)
  target_link_libraries(simulation_tests PRIVATE project10_core)
  if(MSVC)
    target_compile_options(simulation_tests PRIVATE /utf-8)
  endif()
  add_test(NAME simulation_unit COMMAND simulation_tests)

  add_executable(glicko_tests tests/glicko_tests.cpp)
  target_link_libraries(glicko_tests PRIVATE project10_core)
  if(MSVC)
    target_compile_options(glicko_tests PRIVATE /utf-8)
  endif()
  add_test(NAME glicko_unit COMMAND glicko_tests)
  add_test(NAME cli_glicko
    COMMAND "${CMAKE_COMMAND}"
      "-DPROGRAM=$<TARGET_FILE:project10_glicko>"
      "-DGENERATOR=$<TARGET_FILE:project10>"
      "-DEVALUATOR=$<TARGET_FILE:project10_evaluate>"
      "-DSAMPLE_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/configs/run_0000/config.json"
      "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/test-work"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/glicko.cmake"
  )
  add_executable(elo_tests tests/elo_tests.cpp)
  target_link_libraries(elo_tests PRIVATE project10_core)
  if(MSVC)
    target_compile_options(elo_tests PRIVATE /utf-8)
  endif()
  add_test(NAME elo_unit COMMAND elo_tests)

  add_executable(advantage_elo_tests tests/advantage_elo_tests.cpp)
  target_link_libraries(advantage_elo_tests PRIVATE project10_core)
  if(MSVC)
    target_compile_options(advantage_elo_tests PRIVATE /utf-8)
  endif()
  add_test(NAME advantage_elo_unit COMMAND advantage_elo_tests)

  foreach(case generation reproducibility invalid_input invalid_config duplicate_keys missing_config
      overwrite output_paths symlink_outputs hardlink_outputs run_lock initial_scale seed_roles)
    add_test(NAME cli_${case}
      COMMAND "${CMAKE_COMMAND}"
        "-DPROGRAM=$<TARGET_FILE:project10>"
        "-DSAMPLE_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/configs/run_0000/config.json"
        "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/test-work"
        "-DCASE=${case}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/generation.cmake"
    )
  endforeach()
  # Windowsではリンク作成権限がない環境がある。未検証を成功と混同しない。
  set_tests_properties(cli_symlink_outputs PROPERTIES
    SKIP_REGULAR_EXPRESSION "Symbolic link creation unavailable:"
  )
endif()
