# 実装の参考資料

参照元と採用した範囲を記録します。記事のコードをそのまま採用したという意味ではなく、本リポジトリの設定検証・責務分離・再現性の要件に合わせて実装しています。記事や生成AIの説明だけを正しさの根拠にせず、コードと実行結果を確認してください。

| 対象 | 参考資料 | 採用した範囲・本実装での扱い |
| --- | --- | --- |
| [CSV入出力](../src/io/csv.cpp) | [CSVの読み書きに関するQiita記事](https://qiita.com/shirosuke_93/items/d5d068bb15c8e8817c34)（2020-04-26更新） | ストリーム・`getline`・カンマ分割を参考にした。数値CSVに限定し、列数・数値・読み書きの失敗を検証する。引用符付きセルは対象外。記事のコードはそのまま転記していない |
| [MSE計算](../src/evaluation/MSE.cpp) | [第7回会議（2026-10-16）](https://app.notion.com/p/3f9751d0ef6d80638dc5defdc6a09deb) | 各評価時点の全 `a < b` のペアで `Σ(予測勝率−真の勝率)² / (N(N−1)/2)` を計算する。多数の小さい項の加算誤差を抑えるためKahan加算を使い、`double`・`max_digits10` で出力する。テストは独立した手計算と照合する |
| [JSON設定読み込み](../src/io/config.cpp) | [nlohmann/jsonに関するQiita記事](https://qiita.com/yohm/items/0f389ba5c5de4e2df9cf) | `json::parse`・`at`・`get` の使い方を参考にした。型・値・重複キーの検証と、読み込んだJSON全文の保存を追加している |

同梱ライブラリの版・配布元・ライセンスは[third_party/README.md](../third_party/README.md)、研究条件は[READMEの参照資料](../README.md#参照資料)、共有サンプルの来歴は[run_0000の生成来歴](samples/run_0000-provenance.md)を参照してください。
