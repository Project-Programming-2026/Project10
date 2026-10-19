# run_0001：共通の対戦履歴

1,000人・各200試合の共通入力です。全手法へ同じ `players.csv`・`matches.csv` を渡し、行順を保持してください。実装・結合用のサンプルであり、正式な性能比較への採用や手法の優劣を示すものではありません。

| ファイル | 内容 |
| --- | --- |
| [players.csv](players.csv) | ID 0〜999、ヘッダーを含む1,001行、4,901バイト |
| [matches.csv](matches.csv) | 全100,000試合、ヘッダーを含む100,001行、1,916,937バイト |
| [config.json](config.json) | 読み込んだ設定のスナップショット |
| [SHA256SUMS](SHA256SUMS) | 上記3ファイルのSHA-256 |

CSVはWindowsのCRLFを保持し、`.gitattributes` で改行変換を抑止します。既存の `samples/run_0000/` は変更していません。

## 生成来歴

| 項目 | 生成時の値 |
| --- | --- |
| 生成元 | [`b06954593b46d839cb5a2d6608257be33024b05e`](https://github.com/Project-Programming-2026/Project10/commit/b06954593b46d839cb5a2d6608257be33024b05e) を `git archive` で展開。ソース変更なし |
| 生成・照合日 | 2026-10-17、日本時間。照合完了時刻 2026-10-17T10:43:22Z |
| OS・コンパイラ | Windows x64、10.0.26300.0、MSYS2 UCRT64 GCC 16.2.0（Rev6） |
| 標準ライブラリ | libstdc++、`_GLIBCXX_RELEASE=16`、`__GLIBCXX__=20260807` |
| libstdc++ DLLのSHA-256 | `4e4822dc4aaa99383f581dae7ab9464ae89346c5ca742f7efbcdebe20b0fe7b5` |
| ツール・ビルド | CMake 4.4.4、Ninja 1.13.2.git.kitware.jobserver-pipe-1、C++17、Debug、`-g`、最適化・fast-math指定なし |
| 設定の作成 | 生成元の `configs/run_0000/config.json` の `run_id` だけを1へ変更。他の条件・シードは維持 |
| 条件 | 1,000人、各200ラウンド、10ラウンド1期間、正規分布の平均0・標準偏差1、パフォーマンス標準偏差1 |
| 乱数 | 独立した3つの `std::mt19937_64`。能力2026101601、組み合わせ2026101602、勝敗2026101603 |
| ペア・勝敗 | 各ラウンドのシャッフルと隣接ペア、プロビット勝率とベルヌーイ抽選。自己対戦なし、再戦あり |
| 実行・照合 | 独立した2作業先で標準入力へ1と改行を渡した。設定と4つのCSVがすべて同じハッシュ。共有する3ファイルは結果からバイト列のままコピー |

シードは既存の説明用設定を引き継いだ値です。ラウンド方式・最終期間の端数の扱いは実装案であり、正式な比較への採用前にチームで確認します。この設定は端数なしの20期間です。

潜在能力と真の勝率は推定器へ渡しません。共有入力には評価専用の2ファイルを含めず、再生成時の照合用ハッシュを記録します。生成器は同じrunへこれらも出力します。

| 評価用生成物（Git管理対象外） | SHA-256 |
| --- | --- |
| `latent_parameters.csv` | `acc51b74b108ef503d19704bc860ebe49425583f62dfbd4061720fffb87326cc` |
| `ground_truth.csv` | `227945c755dfe92e629f16958b207b773948f003b99ad7b4f891ed932a77c669` |

## 取得・照合と再生成

取得後は、リポジトリのルートから次のPowerShellで3ファイルがすべて `OK` になることを確認します。レーティングへ渡すのは2つのCSVだけです。

```powershell
Get-Content .\samples\run_0001\SHA256SUMS | ForEach-Object {
  $expected, $name = $_ -split '\s+', 2
  $actual = (Get-FileHash -LiteralPath ".\samples\run_0001\$name" -Algorithm SHA256).Hash
  if ($actual -ne $expected) { throw "ハッシュ不一致: $name" }
  "$name : OK"
}
```

再生成は同じコンパイラ・標準ライブラリと固定コミットを使います。以下は検証環境のローカルツールのパスです。他の環境では同じ版を用意してパスを変更します。作業先は未使用であることが必要で、既存の結果・共有データは上書きしません。

```powershell
$projectRoot = (Get-Location).Path
$repro = Join-Path $projectRoot 'build/shared-match-verify'
$cmake = Join-Path $projectRoot 'build/tools/cmake/data/bin/cmake.exe'
$ninja = Join-Path $projectRoot 'build/tools/bin/ninja.exe'
New-Item -ItemType Directory -Path $repro -ErrorAction Stop
git archive --format=zip --output="$repro/source.zip" b06954593b46d839cb5a2d6608257be33024b05e
if ($LASTEXITCODE -ne 0) { throw 'ソースの取得に失敗しました' }
Expand-Archive -LiteralPath "$repro/source.zip" -DestinationPath "$repro/source" -ErrorAction Stop
& $cmake -S "$repro/source" -B "$repro/build" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe "-DCMAKE_MAKE_PROGRAM=$ninja" -DBUILD_TESTING=OFF
if ($LASTEXITCODE -ne 0) { throw 'CMakeの構成に失敗しました' }
& $cmake --build "$repro/build" --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'ビルドに失敗しました' }
New-Item -ItemType Directory -Path "$repro/work/configs/run_0001" -ErrorAction Stop
Copy-Item -LiteralPath "$projectRoot/samples/run_0001/config.json" -Destination "$repro/work/configs/run_0001/config.json" -ErrorAction Stop
Push-Location "$repro/work"
try {
  cmd /d /c 'echo 1| ..\build\project10.exe'
  if ($LASTEXITCODE -ne 0) { throw '生成に失敗しました' }
} finally { Pop-Location }
Get-Content -LiteralPath "$projectRoot/samples/run_0001/SHA256SUMS" | ForEach-Object {
  $expected, $name = $_ -split '\s+', 2
  $actual = (Get-FileHash -LiteralPath "$repro/work/runs/run_0001/$name" -Algorithm SHA256).Hash
  if ($actual -ne $expected) { throw "ハッシュ不一致: $name" }
  "$name : OK"
}
```

別OS・別標準ライブラリで同じ乱数列になる保証はありません。比較には、配布された同一のCSVを使ってください。
