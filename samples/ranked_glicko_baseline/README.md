# ranked_glicko_baseline：ranked_glickoの基準分布

ranked_glicko（Glicko初代版）が、どのrunを評価するときも基準の分布Qとして読む最終レートです。評価するrunとは別シードの基準runから作るため、評価するrunの未来の試合結果を使いません（Issue #27）。ranked_glicko2の[glicko_baseline](../glicko_baseline/)と同じ扱いです。実装・結合用であり、正式な比較への採用はチームで確認します。

| ファイル | 内容 |
| --- | --- |
| [config.json](config.json) | 生成とGlickoに使った設定。`glicko_baseline/config.json` と同じ生成条件（run 3601・シード）に、`glicko`（初期レート1500・初期RD350・`rd_growth` 0）を加えたもの |
| [glicko_rating.csv](glicko_rating.csv) | 基準runを `project10_glicko --final-ratings` で最後の期間まで更新した最終レート。見出し `rating` と、プレイヤーID 0〜999の順の1,000行 |
| [SHA256SUMS](SHA256SUMS) | 上記2ファイルのSHA-256 |

評価するrunのGlickoの設定が異なっても、この基準（`rd_growth` 0）を使います。

## 生成来歴

| 項目 | 値 |
| --- | --- |
| 生成元 | このディレクトリを追加したコミット。生成器はmain `41345eeb9c500e23dc7655b52a94806a3b34a878` から変更なし |
| 生成日 | 2026-10-30 |
| OS・コンパイラ | macOS 26.6.2（arm64）、Apple clang 17.0.0（clang-1700.0.13.5）、libc++ 190102 |
| ツール・ビルド | CMake 4.4.4、Ninja 1.13.2、C++17、Debug（`-g`） |
| 確認 | 独立した2つの作業先で生成とGlickoを実行し、すべての出力のSHA-256が一致 |

Git管理しない生成物のSHA-256は次のとおりです。

| ファイル | SHA-256 |
| --- | --- |
| `players.csv` | `0775cc94cc4c89c2b09037bde48aacc0d0f51e812a61f322acd6a7770e8cdbe9` |
| `matches.csv` | `149e4ffdaf255f51c52b8536e0b8061e3b8bf608ff6b87a57fb9a545d34fd95b` |

同じ設定・シードでも、標準ライブラリが違うと対戦履歴が変わります。この環境で再生成したGlicko-2の最終レートは `glicko_baseline/glicko2_rating.csv` と一致しませんでした。そのため2つの基準は、同じシードでも別の対戦履歴にもとづきます。

## 照合と再生成

リポジトリのルートで、次のコマンドを実行します。作業先には未使用のディレクトリを指定してください。

```bash
(cd samples/ranked_glicko_baseline && shasum -a 256 -c SHA256SUMS)
work=build/dev/test-work/ranked-baseline
mkdir "$work" && mkdir -p "$work/configs/run_3601"
cp samples/ranked_glicko_baseline/config.json "$work/configs/run_3601/"
(cd "$work" && printf '3601\n' | ../../project10 && printf '3601\n' | ../../project10_glicko --final-ratings)
cmp "$work/runs/run_3601/glicko_rating.csv" samples/ranked_glicko_baseline/glicko_rating.csv
```
