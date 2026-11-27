# 実装の参考資料

参照元と採用した範囲を記録します。記事のコードをそのまま採用したという意味ではなく、本リポジトリの設定検証・責務分離・再現性の要件に合わせて実装しています。記事や生成AIの説明だけを正しさの根拠にせず、コードと実行結果を確認してください。

| 対象 | 参考資料 | 採用した範囲・本実装での扱い |
| --- | --- | --- |
| [CSV入出力](../src/io/csv.cpp) | [CSVの読み書きに関するQiita記事](https://qiita.com/shirosuke_93/items/d5d068bb15c8e8817c34)（2020-04-26更新） | ストリーム・`getline`・カンマ分割を参考にした。数値CSVに限定し、列数・数値・読み書きの失敗を検証する。引用符付きセルは対象外。記事のコードはそのまま転記していない |
| [MSE計算](../src/evaluation/MSE.cpp) | [第7回会議（2026-10-16）](https://app.notion.com/p/3f9751d0ef6d80638dc5defdc6a09deb) | 各評価時点の全 `a < b` のペアで `Σ(予測勝率−真の勝率)² / (N(N−1)/2)` を計算する。多数の小さい項の加算誤差を抑えるためKahan加算を使い、`double`・`max_digits10` で出力する。テストは独立した手計算と照合する |
| [JSON設定読み込み](../src/io/config.cpp) | [nlohmann/jsonに関するQiita記事](https://qiita.com/yohm/items/0f389ba5c5de4e2df9cf) | `json::parse`・`at`・`get` の使い方を参考にした。型・値・重複キーの検証と、読み込んだJSON全文の保存を追加している |

同梱ライブラリの版・配布元・ライセンスは[third_party/README.md](../third_party/README.md)、研究条件は[READMEの参照資料](../README.md#参照資料)、共有サンプルの来歴は[run_0000の生成来歴](samples/run_0000-provenance.md)を参照してください。

## TrueSkill

- 原典・版：[Herbrich, Minka, Graepel, *TrueSkill: A Bayesian Skill Rating System*, NIPS 2006版](https://www.microsoft.com/en-us/research/wp-content/uploads/2007/01/NIPS2006_0688.pdf)。§2–3・Table 1のモデルとモーメント整合を1対1・引き分けなしに適用し、初期値と定数は§4.2の尺度を採用する。
- 数値計算：[NIST DLMF §7.9、式7.9.1](https://dlmf.nist.gov/7.9.E1)（Version 1.2.8、2026-09-15）。相補誤差関数の連分数を標準正規分布の裾へ変換して使う。
- [採用した式・定数・近似・独立検証](trueskill.md)に、原典の仕様と本実装で選んだ数値計算上の条件を記録する。

## ロジスティック勝率生成

- 仕様の出発点は[第13回会議（2026-11-13）の拡張案](https://app.notion.com/p/416751d0ef6d809181a2f75fca13dbcc)。同ページの案を採用する利用者の実装依頼に基づく。正式なシード組・試行数・低／高の尺度は未確定。
- 数式の参照：[NIST Dataplot Reference Manual, LOGCDF（1997-03-20）](https://www.itl.nist.gov/div898/software/dataplot/refman2/ch8/logcdf.pdf)、式8-261・8-262と標準偏差`pi/sqrt(3)`。文献の尺度記号`σ`を本実装では`s_g`とし、プロビットの各人の標準偏差`σ_g`と区別する。
- [採用した式・尺度変換・数値安定化・独立検証](win-models.md)を参照。レーティング推定器の数式は変更しない。

## 全履歴推定

- 原典・版：[Coulom, *Whole-History Rating: A Bayesian Rating System for Players of Time-Varying Strength*](https://www.remi-coulom.fr/WHR/WHR.pdf)（PDFの作成日は2008-04-10）。§2.2のBradley–Terryモデルによる全履歴の事後最頻値と、§3.3の「相手を事後最頻値に固定し、1人分のヘッセ行列から不確実性を近似する」方法を[全履歴推定の実装](../src/ratings/whole_history.cpp)に採用する。§3.1と同じく、1人ずつニュートン法で更新する。
- 原典との違い：能力の時間変化（Wiener過程）を入れない。事前分布は仮想の1勝1敗ではなく、全員に共通の正規分布 N(1500, RD₀²) とする。尺度はGlicko-2と同じ173.7178で、`μ = (r − 1500) / 173.7178` がBradley–Terryモデルの自然尺度にあたる。予測は原典にないGlicko（初代）の式に両者のRDを渡す。
- 数値計算：対数事後確率は事前分布により厳密に凹になる。1人分の更新幅がすべて1e-10（Glicko-2尺度）以下になったら収束とし、10万回の反復で収束しなければ停止する。
- 検証：2人・1試合の場合の二分法による解、ランダムな対戦での勾配0とヘッセ行列によるRD、試合順と記録の向きへの不依存を[単体テスト](../tests/whole_history_tests.cpp)で照合する。
