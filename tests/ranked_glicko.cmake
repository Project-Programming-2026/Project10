cmake_minimum_required(VERSION 3.20)
# 少人数のrunを生成し、ranked_glickoの入出力と拒否条件を確認する。
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(work "${TEST_ROOT}/ranked-glicko-${suffix}")
set(run "${work}/runs/run_0000")
set(baseline "${work}/samples/ranked_glicko_baseline/glicko_rating.csv")
file(MAKE_DIRECTORY "${work}/configs/run_0000" "${work}/hidden")
# 共有した基準の最終レートを、実行時と同じ相対パスへ置く。
configure_file("${BASELINE}" "${baseline}" COPYONLY)
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

invoke("${GENERATOR}" "0\n" "")

# 評価専用のCSVを退避しても動くこと（正解情報を読まないこと）を確認する。
file(RENAME "${run}/ground_truth.csv" "${work}/hidden/ground_truth.csv")
file(RENAME "${run}/latent_parameters.csv" "${work}/hidden/latent_parameters.csv")
invoke("${PROGRAM}" "0\n" "")

# 6試合・2試合1期間・評価間隔2なので、評価時点は2・4・6で、各28ペア。
file(STRINGS "${run}/ranked_glicko.csv" rows)
list(POP_FRONT rows header)
list(LENGTH rows count)
if(NOT header STREQUAL "run_id,matches_per_player,player_a,player_b,predicted_p_a_wins" OR
    NOT count EQUAL 84)
  message(FATAL_ERROR "予測CSVのヘッダーまたは行数が正しくありません: ${header} ${count}")
endif()
foreach(row IN LISTS rows)
  if(NOT row MATCHES "^0,(2|4|6),[0-7],[0-7],(0|1|0\\.[0-9]+|[0-9]\\.[0-9]+e-[0-9]+)$")
    message(FATAL_ERROR "予測CSVの行が正しくありません: ${row}")
  endif()
endforeach()

# 既存の予測CSVは上書きしない。同じ入力からは同じ予測CSVになる。
file(SHA256 "${run}/ranked_glicko.csv" before)
invoke("${PROGRAM}" "0\n" "出力先がすでに存在します")
file(REMOVE "${run}/ranked_glicko.csv")
invoke("${PROGRAM}" "0\n" "")
file(SHA256 "${run}/ranked_glicko.csv" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "同じ入力から予測を再現できません")
endif()
file(REMOVE "${run}/ranked_glicko.csv")

invoke("${PROGRAM}" "-1\n" "run IDが不正")
file(MAKE_DIRECTORY "${work}/runs/.run_0000.lock")
invoke("${PROGRAM}" "0\n" "このrunはすでに使用中")
file(REMOVE_RECURSE "${work}/runs/.run_0000.lock")

# 失敗したときに、予測CSVもロックも残らないこと。
function(ranked_fails expected_error)
  invoke("${PROGRAM}" "0\n" "${expected_error}")
  if(EXISTS "${run}/ranked_glicko.csv" OR EXISTS "${work}/runs/.run_0000.lock")
    message(FATAL_ERROR "失敗時に結果またはロックが残りました")
  endif()
endfunction()

# 設定にglickoがなければ拒否する。
file(READ "${run}/config.json" saved_config)
string(JSON invalid REMOVE "${saved_config}" glicko)
file(WRITE "${run}/config.json" "${invalid}")
ranked_fails("設定にglicko")
file(WRITE "${run}/config.json" "${saved_config}")

# 基準の最終レートがない、または形式が正しくなければ拒否する。
file(RENAME "${baseline}" "${work}/hidden/glicko_rating.csv")
ranked_fails("基準の最終レートがありません")
file(WRITE "${baseline}" "rating\n1500\nabc\n")
ranked_fails("基準の最終レートが不正です")
file(RENAME "${work}/hidden/glicko_rating.csv" "${baseline}")
