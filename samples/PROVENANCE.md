# run_0000の生成来歴と再現確認

## 元データについて確認できること

`samples/run_0000/` の3ファイルは、作業前からローカルに存在した `runs/run_0000/` からバイト列を変更せずコピーしたものです。共有開始のコミットは [`81c92913d05cebaa96f8ec5ce657e77513111162`](https://github.com/Project-Programming-2026/Project10/commit/81c92913d05cebaa96f8ec5ce657e77513111162) です。

元データの生成日時、生成時のコードのコミット・未コミット差分、コンパイラ・標準ライブラリ、ビルド・実行コマンドは未記録で、特定できていません。共有開始のコミットや以下の再現確認の条件を、元の生成時の記録として扱わないでください。

## 同一データを再現できた条件

2026-10-17（日本時間）に、以下の固定コミットを `git archive` で別の作業領域へ展開し、ビルド・再生成しました。元データ・共有サンプルは変更していません。

| 項目 | 再現確認時の値 |
| --- | --- |
| ソース | [`ecd0dcfa37974334e4d94a8740e19e6dc2ee211d`](https://github.com/Project-Programming-2026/Project10/commit/ecd0dcfa37974334e4d94a8740e19e6dc2ee211d) のスナップショット。ソース変更なし |
| OS | Windows x64、OSバージョン `10.0.26300.0` |
| コンパイラ | MSYS2 UCRT64、GCC `16.2.0`（`Rev6, Built by MSYS2 project`）、ターゲット `x86_64-w64-mingw32` |
| 標準ライブラリ | libstdc++、`_GLIBCXX_RELEASE=16`、`__GLIBCXX__=20260807` |
| libstdc++ DLLのSHA-256 | `4e4822dc4aaa99383f581dae7ab9464ae89346c5ca742f7efbcdebe20b0fe7b5` |
| ビルドツール | CMake `4.4.4`、Ninja `1.13.2.git.kitware.jobserver-pipe-1` |
| ビルド条件 | C++17、Debug、`-g -std=c++17`、生成処理に `-Wall -Wextra -Wpedantic`。最適化・fast-mathの指定なし |
| 入力 | 固定コミット内の `samples/run_0000/config.json` を、専用作業領域の `configs/run_0000/config.json` へバイト列のままコピー |
| 生成条件 | 1,000人、正規分布の平均0・標準偏差1、能力シード `2026101601`。全設定は[共有設定](run_0000/config.json)を参照 |
| 乱数 | `std::mt19937_64` と `std::normal_distribution<double>` |
| 実行 | 専用作業領域をカレントディレクトリにし、`project10.exe` の標準入力へ `0` と改行を渡す |
| 検証結果 | `config.json`・`players.csv`・`latent_parameters.csv` のすべてが共有サンプルとバイト単位で一致。[SHA256SUMS](run_0000/SHA256SUMS)の3ハッシュとも一致 |

CSVはWindowsのCRLF改行を含みます。別OS・別標準ライブラリでのバイト単位の一致は未確認です。ハッシュはファイルの同一性を確認するものであり、元データの生成時の環境を証明するものではありません。

## 再現確認の手順

以下は同じ条件で確認するためのPowerShell手順です。リポジトリのルートで開始します。作業先 `build/sample-repro/` は未使用であることが必要です。既存の `runs/` や共有サンプルを上書きしません。

この環境にだけ配置したCMake・Ninjaのパスを使っています。他の環境では同じ版のツールを用意してパスを変更してください。コンパイラと標準ライブラリの版も上表と照合します。

```powershell
$projectRoot = (Get-Location).Path
$repro = Join-Path $projectRoot 'build/sample-repro'
$cmake = Join-Path $projectRoot 'build/tools/cmake/data/bin/cmake.exe'
$ninja = Join-Path $projectRoot 'build/tools/bin/ninja.exe'
$compiler = 'C:/msys64/ucrt64/bin/g++.exe'

New-Item -ItemType Directory -Path $repro -ErrorAction Stop
git archive --format=zip --output="$repro/source.zip" ecd0dcfa37974334e4d94a8740e19e6dc2ee211d
if ($LASTEXITCODE -ne 0) { throw 'ソースの取得に失敗しました' }
Expand-Archive -LiteralPath "$repro/source.zip" -DestinationPath "$repro/source" -ErrorAction Stop

& $cmake -S "$repro/source" -B "$repro/build" -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_CXX_COMPILER=$compiler" "-DCMAKE_MAKE_PROGRAM=$ninja" -DBUILD_TESTING=OFF
if ($LASTEXITCODE -ne 0) { throw 'CMakeの構成に失敗しました' }
& $cmake --build "$repro/build" --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'ビルドに失敗しました' }

New-Item -ItemType Directory -Path "$repro/work/configs/run_0000" -ErrorAction Stop
Copy-Item -LiteralPath "$repro/source/samples/run_0000/config.json" -Destination "$repro/work/configs/run_0000/config.json" -ErrorAction Stop
Push-Location "$repro/work"
try {
  cmd /c "echo 0| ..\build\project10.exe"
  if ($LASTEXITCODE -ne 0) { throw '生成に失敗しました' }
} finally {
  Pop-Location
}

Get-Content -LiteralPath "$repro/source/samples/run_0000/SHA256SUMS" | ForEach-Object {
  $expected, $name = $_ -split '\s+', 2
  $actual = (Get-FileHash -LiteralPath "$repro/work/runs/run_0000/$name" -Algorithm SHA256).Hash
  if ($actual -ne $expected) { throw "ハッシュ不一致: $name" }
  "$name : OK"
}
```

3ファイルとも `OK` になることを確認します。この操作は共有サンプルの再現確認用であり、少人数で行う通常のCTestやレーティング精度の実験とは別です。正式な実験では、生成時点からコミット・未コミット差分・環境・コマンド・設定・入力と出力のハッシュを記録してください。
