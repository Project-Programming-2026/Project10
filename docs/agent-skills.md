# 共有スキル

日本語を推敲する [yomiyasu](../.agents/skills/yomiyasu/SKILL.md) を `.agents/skills/yomiyasu/` に同梱しています。リポジトリ内でCodexを使うメンバーは、個人の環境へ追加インストールせずに同じ版を利用できます。

## 使い方

Codexへの依頼でスキル名と対象を指定してください。常時適用する設定やフックは追加していません。

```text
$yomiyasu を使って、README.mdの日本語を整えてください。
専門用語・数式・実験条件・未確認事項の意味は変えないでください。
```

スキルが認識されない場合はCodexを再起動してください。検査ツールにはPython 3を使います。Windowsでは `python` または `py -3` も利用できます。Pythonはこの外部スキルに付属する文章の検査専用であり、研究用プログラムの実装言語やビルド依存は変更しません。

検査時の記録は既定でリポジトリ直下の `.yomiyasu/` に保存され、ツールが作成する `.gitignore` でGit管理から除外されます。検査結果は意味の保持や読みやすさを保証するものではなく、原文との照合が必要です。

## 導入元と更新

| 項目 | 内容 |
| --- | --- |
| 配布元 | [nanaism/yomiyasu](https://github.com/nanaism/yomiyasu) |
| 取得コミット | [`db78b9bc5e868549b517a037df7a4d1c93edac23`](https://github.com/nanaism/yomiyasu/tree/db78b9bc5e868549b517a037df7a4d1c93edac23/skills/yomiyasu) |
| スキルの版 | `3.1.0`（`SKILL.md` の `metadata.version`） |
| 取得日 | 2026-11-13 |
| 取得対象 | 配布元の `skills/yomiyasu/` 全体 |
| 配置先 | `.agents/skills/yomiyasu/` |
| ライセンス | [MIT](../.agents/skills/yomiyasu/LICENSE)、[Unicodeの許諾通知](../.agents/skills/yomiyasu/UNICODE-LICENSE.txt) |

Codex付属の `skill-installer` でコミットを指定して取得しました。スキル本体・参照資料・補助スクリプト・画像・許諾通知は配布元から無改変で同梱しています。プロジェクト側で追記したのは、この文書と `AGENTS.md` の案内です。

更新時は、配布元の対象コミットを固定し、`skills/yomiyasu/` を一時領域へ取得して差分を確認します。配置先を更新したら、この表の取得コミット・版・取得日も更新してください。許諾通知を保持し、参照ファイルの存在と補助ツールの動作を確認します。
