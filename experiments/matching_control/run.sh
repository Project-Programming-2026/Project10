#!/bin/bash
# plan.shの1行を実行する：設定を作る → 正式な生成器 →（組み方がrandom以外なら）rematchで組み直す →
# 正式なCLIで予測 → 共通のMSE評価 → 組み方に使った状態と正式な予測を照合 → 予測CSVのハッシュを記録して削除。
# 使い方：run.sh 作業ディレクトリ 段階 runID 試行 シードの基数 組み方 λ 手法...
# 予測CSVは1つ約350MBになるため、MSEを出したら削除する（同じ設定から再生成できる）。
set -euo pipefail
WORK=$1; shift; PHASE=$1; ID=$2; TRIAL=$3; SEED=$4; MATCHER=$5; LAMBDA=$6; shift 6; METHODS=$*
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=$ROOT/build/release
DIR=runs/$(printf 'run_%04d' "$ID")
mkdir -p "$WORK/configs/$(basename "$DIR")" "$WORK/logs"
cd "$WORK"
sed -e "s/@RUN_ID@/$ID/" -e "s/@SEED_ABILITY@/$((SEED + 1))/" -e "s/@SEED_MATCHING@/$((SEED + 2))/" \
  -e "s/@SEED_RESULT@/$((SEED + 3))/" "$ROOT/experiments/matching_control/config.template.json" \
  > "configs/$(basename "$DIR")/config.json"
LOG=logs/$ID.log
: > "$LOG"
echo "$ID" | "$B/project10" >> "$LOG" 2>&1
if [ "$MATCHER" != random ]; then
  "$ROOT/build/matching_control/rematch" "$DIR" "$MATCHER" "$LAMBDA" >> "$LOG" 2>&1
fi
shasum -a 256 "$DIR/matches.csv" >> logs/matches_sha256.txt
for m in $METHODS; do
  case $m in
    Elo|Glicko2) "$B/project10_predict" "$ID" "$m" >> "$LOG" 2>&1 ;;
    Glicko) echo "$ID" | "$B/project10_glicko" >> "$LOG" 2>&1 ;;
    TrueSkill) echo "$ID" | "$B/project10_trueskill" >> "$LOG" 2>&1 ;;
    WholeHistory) "$B/project10_whole_history" "$ID" 210 whole_history.csv >> "$LOG" 2>&1 ;;
    *) echo "unknown method: $m" >&2; exit 1 ;;
  esac
  if [ "$m" = WholeHistory ]; then
    file=whole_history.csv
    "$B/project10_evaluate" --input WholeHistory "$ID" "$DIR/$file" --name "wh_$ID" >> "$LOG" 2>&1
    cp "runs/comparisons/wh_$ID/series_0001_mse.csv" "$DIR/wholehistory_mse.csv"
  else
    file=$(echo "$m" | tr 'A-Z' 'a-z').csv
    printf '%s\n%s\n' "$ID" "$m" | "$B/project10_evaluate" >> "$LOG" 2>&1
  fi
  if [ "$MATCHER" = "$m" ]; then
    awk -F, -v id="$ID" -f "$ROOT/experiments/matching_control/check.awk" "$DIR/rematch_check.csv" "$DIR/$file" \
      >> logs/check.txt
  fi
  shasum -a 256 "$DIR/$file" >> logs/prediction_sha256.txt
  rm "$DIR/$file"
done
echo "$PHASE $ID $TRIAL $SEED $MATCHER $LAMBDA $METHODS" >> logs/done.txt
