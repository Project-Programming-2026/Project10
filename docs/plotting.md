# MSEグラフの描画方式

単一runの描画は、C++17でMSE CSVを検証してgnuplotスクリプトを生成し、外部のgnuplotを起動する方式を採用しました。CSV・設定の既存処理を再利用し、C++用の描画ライブラリは追加しません。第7回会議（2026-10-16）の「C++に統一、グラフ生成のみPythonも可」に沿い、今回の依頼ではC++を選択しています。[議事録](https://app.notion.com/p/3f9751d0ef6d80638dc5defdc6a09deb)の取得内容には省略・未対応ブロックがあり、参照できたCSV仕様・末尾の決定事項に限定して判断しました。

## 比較と採用理由

| 方式 | 依存・ライセンス | CMake・オフラインへの影響と判断 |
| --- | --- | --- |
| sciplot 0.3.1 + gnuplot | C++17のヘッダー、MIT。描画時は別途gnuplotが必要 | 固定版のヘッダーを手動同梱すればオフライン構成が可能。ただし今回の固定形式の折れ線では追加APIの利点が小さい |
| Matplot++ | C++17、MIT。標準バックエンドはgnuplot。CImg・nodesoup等も扱う | 手動導入後の `find_package(Matplot++)` は可能だが、本件には依存・ビルド対象が増える。自動取得方式は採用しない |
| gnuplot直接起動（採用） | C++17標準ライブラリとOSのプロセスAPI。描画時のみgnuplot | ヘッダー同梱・追加リンク・ダウンロード不要。WindowsはCreateProcessW、Linuxはfork/execでシェルを経由せず起動する |
| Python + matplotlib | Python、matplotlibとその依存。matplotlibはPSFベースの独自ライセンス | 非GUIバックエンドでSVG・PNGに対応し、将来の統計図には有力。今回はC++統一を優先。CSV契約の共有とPython環境管理が別途必要 |

sciplotの [導入説明](https://sciplot.github.io/installation/) と、固定版 [Canvas.hpp](https://github.com/sciplot/sciplot/blob/v0.3.1/sciplot/Canvas.hpp)・[Utils.hpp](https://github.com/sciplot/sciplot/blob/v0.3.1/sciplot/Utils.hpp) を確認しました。0.3.1の `Canvas::save` は `runscript` のbool結果を呼び出し側へ返さず、内部の起動は `std::system` です。今回必要な外部プログラムの失敗判定・出力保護は別途必要になるため、固定形式のスクリプトを直接生成する方が扱いやすいと判断しました。sciplot自体がオフライン利用できないという意味ではありません。

Matplot++は [公式依存一覧](https://alandefreitas.github.io/matplotplusplus/integration/install/build-from-source/dependencies/) と [公式リポジトリ](https://github.com/alandefreitas/matplotplusplus) を参照しました。実験的OpenGLバックエンドやその自動取得は今回不要です。Python案は [matplotlibの非GUIバックエンド](https://matplotlib.org/stable/users/explain/figure/backends.html)・[ライセンス](https://matplotlib.org/stable/project/license.html) を参照しました。Matplot++・matplotlibのビルド比較実験は実施しておらず、上表は依存構成の比較です。

## 依存と互換性

実行時はgnuplot 5.4以降の `svg` と、PNG出力時の `pngcairo` を使います。5.4は対応下限として指定していますが、全パッチ版を検証したものではありません。PNGにはパッケージ側のCairo・Pango等の依存と利用可能なフォントが必要です。[MSYS2 UCRT64公式パッケージ](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-gnuplot)にも対応する実行ファイルと依存一覧があります。画面表示用端末は使いません。

gnuplotはGPLではなく、[gnuplot固有のライセンス](https://github.com/gnuplot/gnuplot/blob/master/Copyright)です。本リポジトリではソース・バイナリを同梱・改変せず、利用者が導入した実行ファイルを呼び出します。配布し直す場合はgnuplot本体と付属ライブラリそれぞれのライセンスを確認してください。新しい第三者ソースを同梱しないため、`third_party/` の既存JSON依存に変更はありません。

CMakeは `project10_plotting` と `project10_plot` を追加します。gnuplotの探索はCTest用であり、未導入でもC++の構成・ビルドと入力検証テストは可能です。構成時・実行時ともライブラリの自動ダウンロードを行いません。オフラインではコンパイラ・CMake・ビルドツールを、描画まで行う場合はgnuplot・共有ライブラリ・フォントも事前に用意します。

## 設計・レビュー範囲

`io::loadPlotConfig` は保存済み設定、`io::readMseCsv` は共通のCSVヘッダー・列・数値処理を使ってMSEを読みます。確率間の二乗誤差の平均なのでMSEは0〜1です。`MseSeries` が手法・出典・時系列を保持し、その後段で描画します。将来の複数run集計は別の処理としてこの間に置けますが、標準偏差や信頼区間の表示契約は追加設計が必要です。

全手法の全評価時点を設定へ照合し、欠落を検出した時点で停止します。部分データを黙って重ねたり、共通部分だけへ狭めたりしません。未実装手法は設定未指定・ファイル不在として表示して省略できます。既存CSVの不正値は省略の理由にしません。生成モデル・レーティング・MSE計算式には変更を加えていません。

新しいグラフ出力ディレクトリの作成で既存出力を保護し、検証済み数値だけをデータブロックとして `plot.gp` に保存します。元のCSVパスを描画言語に渡さず、gnuplotの起動時は `-d` で初期化ファイルを読みません。SVGのタグ・PNGのシグネチャも確認します。中断・強制終了時に残ったディレクトリを勝手に再利用せず、次の実行では別名を指定します。

変更はテスト・ドキュメントを含め500行を超えます。1機能の入出力契約と保護・検証を同時にレビューできるようまとめました。推奨順は、設定・CSV読込 → 描画・CLI → CMake → テスト → READMEです。

## 検証記録

2026-10-22に、次の環境でDebugビルドとCTestを確認しました。テストの架空値は正式な実験結果ではなく、手法の優劣の根拠にはしません。

| 環境 | ツール | 結果 |
| --- | --- | --- |
| WSL2 Ubuntu 26.04.1 LTS | GCC 15.2.0、CMake 4.2.3、Ninja 1.13.2、gnuplot 6.0.2 | 全19件成功、スキップなし。2手法のSVG・PNGを生成してラベル・形式と画像表示を確認 |
| Windows・MSYS2 UCRT64 | GCC 15.2.0、CMake 4.2.3、Ninja 1.13.2、既存gnuplot 6.0.1 | 18件成功・既存のシンボリックリンク保護1件は権限不足でSkipped。描画の3件はすべて成功し、SVG・PNGを実生成 |

Linuxではシステムへインストールせず、公式Ubuntuパッケージを `/tmp` へ手動展開し、対応する `usr/bin` をPATHへ、`usr/lib/x86_64-linux-gnu` をLD_LIBRARY_PATHへ指定して以下を実行しました。

```sh
cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dev --parallel 2
ctest --test-dir build/dev --output-on-failure
```

Windowsでは既存のUCRT64コンパイラ・gnuplotと、一時展開した公式CMake・Ninjaを使用しました。ソースはWSLから参照し、`-B` とCTestの `--test-dir` にはWindowsの専用一時ビルド領域を指定しました。UNC共有ドライブ上のビルドも試しましたが、既存MSE評価のハードリンク公開がエラー50となるため、NTFS上へビルド先を変更しました。ソース・MSE処理はこの環境差のために変更していません。

Windowsの検証用コンソールは `chcp 65001` でUTF-8にし、新しい描画CLIテストも `execute_process(ENCODING UTF-8)` を明示しています。描画出力のシンボリックリンク保護確認も、Windowsではリンク作成権限がないため未検証です。Linuxでは作成・保護まで確認しました。MSVC、gnuplot 5.4での実動作、異なるフォント環境での画像のバイト一致は未検証です。

追加したテストでは、CRLF・行順・MSEの境界値、ヘッダー・列数・run ID・評価時点・重複・欠落・NaN/Infの拒否、設定のファイル名・重複キー、1手法/4手法、未指定・不在の手法の明示、既存出力のハッシュ保全、ロック、描画プログラムの不在・異常終了・画像未生成を確認しました。既存16件は無効化していません。
