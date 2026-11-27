#!/bin/bash
# plan.shの行を標準入力から読み、作業ディレクトリのMSE CSVを1つの縦長CSVへまとめて標準出力に書く。
# 使い方：plan.sh 段階 | collect.sh 作業ディレクトリ
# 列：phase,run_id,trial,seed_base,matcher,lambda,estimator,matches_per_player,mse
set -euo pipefail
WORK=$1
while read -r phase id trial seed matcher lambda methods; do
  dir=$WORK/runs/$(printf 'run_%04d' "$id")
  for m in $methods; do
    if [ "$m" = WholeHistory ]; then
      file=$dir/wholehistory_mse.csv
      [ -f "$file" ] || file=$WORK/runs/comparisons/wh_$id/series_0001_mse.csv
    else
      file=$dir/$(echo "$m" | tr 'A-Z' 'a-z')_mse.csv
    fi
    # ヘッダーを除き、run IDと20時点がそろっていることを確かめてから書く。
    awk -F, -v p="$phase" -v id="$id" -v t="$trial" -v s="$seed" -v mt="$matcher" -v l="$lambda" -v e="$m" '
      FNR == 1 { next }
      $1 != id { print "run_id mismatch in " FILENAME > "/dev/stderr"; exit 1 }
      { printf "%s,%s,%s,%s,%s,%s,%s,%s,%s\n", p, id, t, s, mt, l, e, $2, $3; n++ }
      END { if (n != 20) { print "expected 20 checkpoints in " FILENAME > "/dev/stderr"; exit 1 } }' "$file"
  done
done
