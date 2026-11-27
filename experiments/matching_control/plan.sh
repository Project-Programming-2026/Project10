#!/bin/bash
# 実験の全runを「段階 runID 試行 シードの基数 組み方 λ 評価する手法...」の1行ずつで出力する。
# 使い方：plan.sh factorial|tuning|evaluation
# シード：能力・組み合わせ・勝敗 = 基数+1・+2・+3。調整用は 2026112500+10×試行、評価用は 2026113000+10×試行。
set -euo pipefail
ALL="Elo Glicko Glicko2 TrueSkill WholeHistory"
METHODS=(Elo Glicko Glicko2 TrueSkill WholeHistory)
# 調整の探索範囲。Elo以外は不確実性の倍率、EloはEloポイント。最良が範囲の端（乱数が最大）だった
# Elo・Glicko・Glicko2・TrueSkillは、評価用を実行する前に6・7番目の値を追加した。
GRID_Elo="0 50 100 200 400 800 1600"
GRID_Glicko="0 0.25 0.5 1 2 4 8"
GRID_Glicko2="0 0.25 0.5 1 2 4 8"
GRID_TrueSkill="0 0.25 0.5 1 2 4 8"
GRID_WholeHistory="0 0.25 0.5 1 2"
# 調整用5試行で選んだλ（plot.pyが調整結果から選び直して一致を確かめる）。
CHOSEN_Elo=400; CHOSEN_Glicko=2; CHOSEN_Glicko2=2; CHOSEN_TrueSkill=8; CHOSEN_WholeHistory=0.5
case ${1:-} in
  factorial)  # 組み方（ランダム／全履歴推定のレートで組む）× 5手法、評価用10試行
    for t in $(seq 1 10); do
      echo "factorial $((100 + t)) $t $((2026113000 + 10 * t)) random 0 $ALL"
      echo "factorial $((200 + t)) $t $((2026113000 + 10 * t)) WholeHistory 0.5 $ALL"
    done ;;
  tuning)  # 各手法が自分のレートで組む。調整用5試行でλを選ぶ
    for t in $(seq 1 5); do echo "tuning $((1000 + t)) $t $((2026112500 + 10 * t)) random 0 $ALL"; done
    for m in 1 2 3 4 5; do
      name=${METHODS[$((m - 1))]}; grid=GRID_$name; l=0
      for lambda in ${!grid}; do
        l=$((l + 1))
        for t in $(seq 1 5); do
          echo "tuning $((1000 + m * 100 + l * 10 + t)) $t $((2026112500 + 10 * t)) $name $lambda $name"
        done
      done
    done ;;
  evaluation)  # 選んだλで評価用10試行。ランダムの比較対象はfactorialのrun 101〜110
    for m in 1 2 3 4 5; do
      name=${METHODS[$((m - 1))]}; chosen=CHOSEN_$name
      for t in $(seq 1 10); do
        echo "evaluation $((2000 + m * 100 + t)) $t $((2026113000 + 10 * t)) $name ${!chosen} $name"
      done
    done ;;
  *) echo "usage: plan.sh factorial|tuning|evaluation" >&2; exit 1 ;;
esac
