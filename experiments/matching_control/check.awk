# rematch_check.csv（組み方に使った最終状態の予測）と、正式なCLIの200試合時点の予測を照合する。
# 使い方：awk -F, -v id=runID -f check.awk rematch_check.csv 予測CSV
FNR == 1 { next }
NR == FNR { want[$1 "," $2] = $3; n++; next }
$1 == id && $2 == 200 && (($3 "," $4) in want) {
  d = $5 - want[$3 "," $4]; if (d < 0) d = -d
  if (d > worst) worst = d
  found++
}
END {
  if (found != n) { print id, FILENAME, "pairs missing", found, n; exit 1 }
  printf "%s %s pairs=%d max_abs_diff=%.3g\n", id, FILENAME, n, worst
}
