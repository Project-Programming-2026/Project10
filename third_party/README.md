# 同梱ライブラリ

`nlohmann/json.hpp` は既存のJSON for Modern C++ 3.12.0の単一ヘッダーです。
出典は [nlohmann/json](https://github.com/nlohmann/json/tree/v3.12.0)、ライセンスはMITです。
著作権・ライセンス表記はヘッダー内に保持し、[ライセンス全文](nlohmann/LICENSE.MIT)を同梱しています。

CMakeではこのヘッダーを利用し、FetchContentやシステムパッケージとの併用はしません。
オフラインで構成・ビルドでき、全員が同じ版を使用できます。
その分、更新は手動で行い、版・出典・ライセンスを再確認します。
同梱ヘッダーをプロジェクトの整形規則に合わせて書き換えないでください。
