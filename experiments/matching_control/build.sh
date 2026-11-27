#!/bin/bash
# 実験用プログラムをビルドする。リポジトリ直下で実行する。
# 正式なCLIはReleaseで build/release に、rematch は build/matching_control に置く。
set -euo pipefail
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/release
OUT=build/matching_control
mkdir -p "$OUT/include/project10/ratings"
# Glicko-2で組むときだけ使う、正式なglicko2.cppと同じ式の実装（取り下げたPR #43のコミット）。
git show 787c686:include/project10/ratings/glicko2_tunable.hpp > "$OUT/include/project10/ratings/glicko2_tunable.hpp"
git show 787c686:src/ratings/glicko2_tunable.cpp > "$OUT/glicko2_tunable.cpp"
c++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Iinclude -I"$OUT/include" -Ithird_party \
  experiments/matching_control/rematch.cpp "$OUT/glicko2_tunable.cpp" build/release/libproject10_core.a \
  -o "$OUT/rematch"
