cmake_minimum_required(VERSION 3.20)
# 少人数の設定でGlicko-2調整実験のCLIを実行し、出力・再現性・上書き拒否・設定の拒否を確認する。
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(work "${TEST_ROOT}/glicko2-tuning-${suffix}")
file(MAKE_DIRECTORY "${work}")
# シード・パラメータはテスト用の値で、実験で使う値ではない。
set(config [=[{
  "schema_version": "1.0",
  "num_players": 8,
  "matches_per_player": 20,
  "rating_period_matches_per_player": 10,
  "evaluation_interval_matches_per_player": 10,
  "model": "probit_1d",
  "ability_distribution": {"type": "normal", "mean": 0.0, "std": 1.0},
  "performance_std": 1.0,
  "matchmaking": "uniform_random",
  "trials": [
    {"ability": 1, "matching": 2, "result": 3},
    {"ability": 4, "matching": 5, "result": 6}
  ],
  "glicko2_parameter_sets": [
    {"initial_rd": [200, 350], "initial_volatility": 0.06, "tau": 0.5}
  ]
}]=])
file(WRITE "${work}/config.json" "${config}")

function(tune config_path output expected_error)
  execute_process(COMMAND "${PROGRAM}" "${config_path}" "${output}"
    RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 60)
  if(expected_error STREQUAL "")
    if(NOT result STREQUAL "0")
      message(FATAL_ERROR "実行に失敗しました: ${stdout}${stderr}")
    endif()
  elseif(result STREQUAL "0" OR NOT stderr MATCHES "${expected_error}")
    message(FATAL_ERROR "想定したエラーを確認できませんでした。\n想定: ${expected_error}\n実際: ${result} ${stdout}${stderr}")
  endif()
endfunction()

tune("${work}/config.json" "${work}/first" "")
file(READ "${work}/first/config.json" snapshot)
if(NOT snapshot STREQUAL config)
  message(FATAL_ERROR "設定のスナップショットが入力と一致しません")
endif()
# 2組 × 2試行 × 2評価時点。ヘッダーを含む行数と、2組目の先頭行のパラメータ列を確認する。
set(parameters "set_id,initial_rd,initial_volatility,tau")
foreach(expected
    "mse.csv|9|${parameters},trial,matches_per_player,mse|2,350,0.06,0.5,1,10,"
    "trials.csv|5|${parameters},trial,mean_mse|2,350,0.06,0.5,1,"
    "curves.csv|5|${parameters},matches_per_player,mean_mse,mse_sd|2,350,0.06,0.5,10,"
    "summary.csv|3|${parameters},trials,mean_mse,mean_mse_sd,first_mse,last_mse|2,350,0.06,0.5,2,")
  string(REPLACE "|" ";" expected "${expected}")
  list(GET expected 0 name)
  list(GET expected 1 lines)
  list(GET expected 2 header)
  list(GET expected 3 prefix)
  file(STRINGS "${work}/first/${name}" rows)
  list(LENGTH rows count)
  list(GET rows 0 actual_header)
  math(EXPR second_set_row "(${count} - 1) / 2 + 1")
  list(GET rows ${second_set_row} second_set)
  string(FIND "${second_set}" "${prefix}" position)
  if(NOT count EQUAL lines OR NOT actual_header STREQUAL header OR NOT position EQUAL 0)
    message(FATAL_ERROR "${name}の行数・ヘッダー・パラメータ列が想定と異なります: ${second_set}")
  endif()
endforeach()

# 同じ設定で再実行すると、同じ結果になること。
tune("${work}/config.json" "${work}/second" "")
foreach(name mse.csv trials.csv curves.csv summary.csv)
  file(SHA256 "${work}/first/${name}" first_hash)
  file(SHA256 "${work}/second/${name}" second_hash)
  if(NOT first_hash STREQUAL second_hash)
    message(FATAL_ERROR "再実行で${name}が一致しません")
  endif()
endforeach()

# 既存の出力先は上書きしないこと。
file(SHA256 "${work}/first/mse.csv" before)
tune("${work}/config.json" "${work}/first" "出力先がすでに存在します")
file(SHA256 "${work}/first/mse.csv" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "既存の結果が変更されました")
endif()

# 不正な設定では、出力先を作らずに停止すること。
function(rejects name modified expected_error)
  file(WRITE "${work}/${name}.json" "${modified}")
  tune("${work}/${name}.json" "${work}/${name}" "${expected_error}")
  if(EXISTS "${work}/${name}")
    message(FATAL_ERROR "${name}: 失敗時に出力先が作られました")
  endif()
endfunction()
string(JSON modified SET "${config}" unknown_key 1)
rejects(unknown_key "${modified}" "未知の項目")
string(JSON modified SET "${config}" glicko2_parameter_sets 0 tau 0)
rejects(zero_tau "${modified}" "tauは正の数値")
string(JSON modified REMOVE "${config}" trials 1)
rejects(single_trial "${modified}" "2つ以上の試行")
string(JSON modified SET "${config}" glicko2_parameter_sets 0 initial_rd "[350, 350]")
rejects(duplicate_set "${modified}" "パラメータの組が重複")
string(REPLACE "\"model\": \"probit_1d\"," "\"model\": \"probit_1d\", \"model\": \"probit_1d\","
  modified "${config}")
rejects(duplicate_key "${modified}" "キーが重複")
string(JSON modified SET "${config}" num_players 7)
rejects(odd_players "${modified}" "エラー")
