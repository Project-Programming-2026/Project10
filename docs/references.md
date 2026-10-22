# 実装の参考資料

参照元と採用した範囲を記録します。記事のコードをそのまま採用したという意味ではなく、本リポジトリの設定検証・責務分離・再現性の要件に合わせて実装しています。記事や生成AIの説明だけを正しさの根拠にせず、コードと実行結果を確認してください。

| 対象 | 参考資料 | 採用した範囲・本実装での扱い |
| --- | --- | --- |
| [CSV出力](../src/io/csv.cpp) | [CSVの読み書きに関するQiita記事](https://qiita.com/shirosuke_93/items/d5d068bb15c8e8817c34) | ファイルストリームでヘッダー・各行を書き込む方法を参考にした。現在はCSV出力のみで、生成処理は別関数に分離し、書き込み失敗を検出する |
| [JSON設定読み込み](../src/io/config.cpp) | [nlohmann/jsonに関するQiita記事](https://qiita.com/yohm/items/0f389ba5c5de4e2df9cf) | `json::parse`・`at`・`get` の使い方を参考にした。型・値・重複キーの検証と、読み込んだJSON全文の保存を追加している |

同梱ライブラリの版・配布元・ライセンスは[third_party/README.md](../third_party/README.md)、研究条件は[READMEの参照資料](../README.md#参照資料)、共有サンプルの来歴は[run_0000の生成来歴](samples/run_0000-provenance.md)を参照してください。
