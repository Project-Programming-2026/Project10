# 任意CSVのMSE評価・比較描画

`project10_evaluate`・`project10_plot` の `--input` モードでは、同じ手法のパラメータ違いと異なるrunの成果物を、手法名の登録や設定中のファイル名の変更なしで比較できます。既存の引数なしの評価CLI、標準入力でrun IDを読む描画CLI、一括実行の操作も利用できます。

## 実行方法

通常のCMakeビルド後、リポジトリ直下で実行します。次は保存済みrun 8に対する2つの予測CSVを別途用意した場合の呼び出し例です。この機能は予測生成・パラメータ探索を行いません。ファイル名と表示名は説明用で、実験結果や推奨パラメータを示しません。

```sh
./build/dev/project10_evaluate \
  --input "Elo K=16" 8 runs/run_0008/elo_k16.csv \
  --input "Elo K=32" 8 runs/run_0008/elo_k32.csv \
  --name elo-parameter-mse

./build/dev/project10_plot \
  --input "Elo K=16" 8 runs/comparisons/elo-parameter-mse/series_0001_mse.csv \
  --input "Elo K=32" 8 runs/comparisons/elo-parameter-mse/series_0002_mse.csv \
  --name elo-parameter-graph --png
```

Windowsでは実行ファイルを `.\build\dev\project10_evaluate.exe`・`.\build\dev\project10_plot.exe` に読み替えます。上の複数行表記はsh用です。PowerShellでは1行で指定するか、PowerShellの行継続を使用してください。

- `--input 表示名 run_ID CSVパス` を1回以上指定します。同じrun IDを繰り返せます。各系列のrun IDを変えれば異なるrunにも対応します。
- `--input` モードでは標準入力を読まず、`--name` が必須です。表示名は空・重複・制御文字・バッククォートを拒否し、日本語・空白・引用符を受け付けます。
- CSVは通常ファイルを指定します。相対パスは実行時の作業ディレクトリ基準、絶対パスも使用できます。空白を含む引数はシェルの規則に従って引用してください。
- 出力名は英数字・ハイフン・アンダースコアです。出力先は `runs/comparisons/<name>/`。評価と描画には異なる名前を指定します。
- 描画はgnuplotが必要です。`--png` でPNGを追加し、`--gnuplot executable` で実行ファイルを指定できます。両CLIとも `--help` で使い方を表示します。

## 入力と評価定義

各CSVのrun IDに対応する `runs/run_XXXX/config.json` を読みます。必須の評価条件は `run_id`・`num_players`・`matches_per_player`・`evaluation_interval_matches_per_player` です。`comparison` があれば条件付き評価とし、`comparison.scenario` に `S0` または `S1` が必要です。`files.predictions`・`files.mse` の手法名・ファイル名は参照しません。

評価では同じrunの `players.csv` と、通常なら `ground_truth.csv`、条件付きなら `ground_truth_positive.csv`・`ground_truth_negative.csv` が必要です。描画は保存済み設定とMSE CSVだけで実行でき、プレイヤー・正解・予測CSVを要求しません。

| 対象 | ヘッダー |
| --- | --- |
| 通常の予測 | `run_id,matches_per_player,player_a,player_b,predicted_p_a_wins` |
| 条件z付き予測 | `run_id,matches_per_player,player_a,player_b,z,predicted_p_a_wins` |
| MSE | `run_id,matches_per_player,mse` |

通常runでは5列予測を正解と照合します。条件付きrunでは、5列なら同じ予測を両側の正解に適用し、6列なら `z=1` と `z=-1` をそれぞれの正解と照合します。両側のMSEを等確率平均する既存定義を使います。通常runに6列予測を指定することと、未知のヘッダーは拒否します。

各評価時点に全 `player_a < player_b` の予測が必要です。条件z付きでは各側に全ペアを要求します。run ID・既知ID・評価時点の一致、欠落・重複、不正確率・非有限値を既存の共通検証で検査します。LF・CRLFと任意の行順に対応します。

評価は系列ごとに独立して計算します。描画では通常MSEと条件付きMSEの混在を拒否します。run 8の通常評価とrun 9の条件付き評価などは、別の図にしてください。同じMSE定義なら人数・シード・評価間隔・総対戦数が異なるrunを重ねられます。S0とS1の条件付き評価も重ねられますが、生成条件の違いは解釈に反映してください。

## 出力と保護

評価は指定順に `series_0001_mse.csv`・`series_0002_mse.csv` などを出力します。描画は `plot.gp`・`mse.svg`、`--png` 指定時は `mse.png` を保存します。凡例は表示名とrun ID、条件付きではシナリオも含みます。

グラフは各runの全評価点を保持し、補間・間引き・共通時点への絞り込み・平均化を行いません。横軸は各人の累積対戦数で、上限は指定runの最大対戦数です。縦軸は全系列共通で、既存の自動調整を使います。色・線種・点種は指定順に割り当て、6系列を超えても循環させて描画します。

両CLIの `manifest.json` は、実行引数・作業ディレクトリ・系列の表示名・run ID・入力の絶対パス・出力名・MSE定義・検証に使った設定内容を記録します。描画数値は `plot.gp` にも保存します。manifestは入力データの複製やハッシュ照合を行いません。移動後の再実行用設定として読み込む機能もありません。

対象runのロックをID順に重複なく取得します。外部パスのCSVは作成元のロックを管理できないため、実行中に変更しないでください。全系列の検証・計算後に出力ディレクトリを新規作成します。存在する出力先は空ディレクトリ・ファイル・リンクでも拒否し、上書き確認は行いません。エラー時は今回作成した既知のファイルだけを片付けます。強制終了で残った出力は自動再利用せず、新しい名前を使用します。

CSVから実際の学習履歴・パラメータは検証できません。表示名や保存済みrun設定だけで同一学習条件を保証したことにはならないため、任意予測の生成コマンド・パラメータ・コード・入力のハッシュなどは別途保存してください。複数runの重ね描きは平均・分散の集計ではありません。Log Loss・Brierの任意入力、複数試行の集計、実験来歴の自動記録は今回の対象外です。
