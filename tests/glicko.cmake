cmake_minimum_required(VERSION 3.20)
# 少人数で「生成→Glicko→MSE評価」を通し、Glickoの入出力と拒否条件を確認する。
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(work "${TEST_ROOT}/glicko-${suffix}")
set(run "${work}/runs/run_0000")
file(MAKE_DIRECTORY "${work}/configs/run_0000" "${work}/hidden")
file(READ "${SAMPLE_CONFIG}" config)
string(JSON config SET "${config}" num_players 8)
string(JSON config SET "${config}" matches_per_player 6)
string(JSON config SET "${config}" rating_period_matches_per_player 2)
string(JSON config SET "${config}" evaluation_interval_matches_per_player 2)
# rd_growthはテスト用の値で、実験で使う値ではない。
string(JSON config SET "${config}" glicko
  "{\"initial_rating\": 1500, \"initial_rd\": 350, \"rd_growth\": 30}")
file(WRITE "${work}/configs/run_0000/config.json" "${config}")

function(invoke program input expected_error)
  file(WRITE "${work}/stdin.txt" "${input}")
  execute_process(COMMAND "${program}" WORKING_DIRECTORY "${work}"
    INPUT_FILE "${work}/stdin.txt" RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 30)
  if(expected_error STREQUAL "")
    if(NOT result STREQUAL "0")
      message(FATAL_ERROR "実行失敗: ${output}${error}")
    endif()
  elseif(result STREQUAL "0" OR NOT error MATCHES "${expected_error}")
    message(FATAL_ERROR "期待したエラー「${expected_error}」と異なります: ${result} ${output}${error}")
  endif()
endfunction()

# 失敗したときに、予測CSVもロックも残らないこと。
function(glicko_fails input expected_error)
  invoke("${PROGRAM}" "${input}" "${expected_error}")
  if(EXISTS "${run}/glicko.csv" OR EXISTS "${work}/runs/.run_0000.lock")
    message(FATAL_ERROR "失敗時に結果またはロックが残りました")
  endif()
endfunction()

invoke("${GENERATOR}" "0\n" "")

# 評価専用のCSVを退避しても動くこと（正解情報を読まないこと）を確認する。
file(RENAME "${run}/ground_truth.csv" "${work}/hidden/ground_truth.csv")
file(RENAME "${run}/latent_parameters.csv" "${work}/hidden/latent_parameters.csv")
invoke("${PROGRAM}" "0\n" "")

# 6試合・2試合1期間・評価間隔2なので、評価時点は2・4・6で、各28ペア。
file(STRINGS "${run}/glicko.csv" rows)
list(POP_FRONT rows header)
list(LENGTH rows count)
if(NOT header STREQUAL "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins" OR
    NOT count EQUAL 84)
  message(FATAL_ERROR "予測CSVのヘッダーまたは行数が正しくありません: ${header} ${count}")
endif()
set(index 0)
foreach(point 2 4 6)
  foreach(a RANGE 0 6)
    math(EXPR first "${a} + 1")
    foreach(b RANGE ${first} 7)
      list(GET rows ${index} row)
      if(NOT row MATCHES "^0,${point},${a},${b},(0|1|0\\.[0-9]+|[0-9]\\.[0-9]+e-[0-9]+)$")
        message(FATAL_ERROR "予測CSVの行が正しくありません: ${row}")
      endif()
      math(EXPR index "${index} + 1")
    endforeach()
  endforeach()
endforeach()

# 既存の予測CSVは上書きしない。同じ入力からは同じ予測CSVになる。
file(SHA256 "${run}/glicko.csv" before)
invoke("${PROGRAM}" "0\n" "出力先がすでに存在します")
file(SHA256 "${run}/glicko.csv" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "既存の予測が変更されました")
endif()
file(REMOVE "${run}/glicko.csv")
invoke("${PROGRAM}" "0\n" "")
file(SHA256 "${run}/glicko.csv" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "同じ入力から予測を再現できません")
endif()

# 共通のMSE評価がそのまま読めること。
file(RENAME "${work}/hidden/ground_truth.csv" "${run}/ground_truth.csv")
invoke("${EVALUATOR}" "0\nGlicko\n" "")
file(STRINGS "${run}/glicko_mse.csv" mse)
list(LENGTH mse mse_count)
if(NOT mse_count EQUAL 4)
  message(FATAL_ERROR "評価時点の数が正しくありません: ${mse}")
endif()
file(REMOVE "${run}/glicko.csv")

invoke("${PROGRAM}" "-1\n" "run IDが不正")
file(MAKE_DIRECTORY "${work}/runs/.run_0000.lock")
invoke("${PROGRAM}" "0\n" "このrunはすでに使用中")
file(REMOVE_RECURSE "${work}/runs/.run_0000.lock")

# 設定の誤り。
file(READ "${run}/config.json" saved_config)
string(JSON invalid REMOVE "${saved_config}" glicko)
file(WRITE "${run}/config.json" "${invalid}")
glicko_fails("0\n" "設定にglicko")
string(JSON invalid SET "${saved_config}" glicko initial_rd 0)
file(WRITE "${run}/config.json" "${invalid}")
glicko_fails("0\n" "initial_rdは正")
string(JSON invalid SET "${saved_config}" files predictions Glicko "\"elo.csv\"")
file(WRITE "${run}/config.json" "${invalid}")
glicko_fails("0\n" "Glickoのファイル名")
# 2試合1期間で評価間隔3だと、評価時点3が期間の途中に入る。
string(JSON invalid SET "${saved_config}" evaluation_interval_matches_per_player 3)
file(WRITE "${run}/config.json" "${invalid}")
glicko_fails("0\n" "evaluation point falls inside a Glicko period")
file(WRITE "${run}/config.json" "${saved_config}")

# 対戦履歴の誤り（共通のmatches.csv読み込みで拒否される）。
file(READ "${run}/matches.csv" saved_matches)
set(header "match_id,period_id,player_a,player_b,result_a\n")
foreach(rows IN ITEMS "0,0,0,0,1\n" "0,0,0,1,2\n" "0,1,0,1,1\n" "0,-1,0,1,1\n" "0,0,0,1,1\n")
  file(WRITE "${run}/matches.csv" "${header}${rows}")
  glicko_fails("0\n" "エラー")
endforeach()
file(WRITE "${run}/matches.csv" "${saved_matches}")
