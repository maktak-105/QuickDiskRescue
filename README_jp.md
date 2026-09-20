# QuickDiskRescue

Windows がマウントできないディスクから、フォルダ構造を保って別ドライブへ救出するツールです。ソースへは書き込みません。

バージョン: **v1.0.2**

実装: **C++17 + WebView2**。配布物に Python は含みません。

## 配布版を使う

GitHub Releases から ZIP をダウンロードしてください:

- [最新の Release](https://github.com/maktak-105/QuickDiskRescue/releases)
- [QuickDiskRescue v1.0.2](https://github.com/maktak-105/QuickDiskRescue/releases/tag/v1.0.2)
- [QuickDiskRescue-binary.zip を直接ダウンロード](https://github.com/maktak-105/QuickDiskRescue/releases/download/v1.0.2/QuickDiskRescue-binary.zip)

ZIP を同じフォルダに展開して `QuickDiskRescue.exe` を実行します。

- `QuickDiskRescue.exe` — 本体（UIはEXEに埋め込み済み）
- `QuickDiskRescue_cli.exe` — CLI版
- `WebView2Loader.dll` — WebView2 ローダー
- `readme.txt` / `readme_jp.txt` — 使い方
- `history.txt` / `history_jp.txt` — 更新履歴
- `LICENSE.txt` / `LICENSE_jp.txt` — MIT License

### 完全性検証 (SHA-256)

配布用 ZIP および各バイナリの公式 SHA-256 ハッシュ値は、CI (GitHub Actions) ビルド時に自動計算され、各リリースページに `SHA256SUMS.txt` として添付・公開されています。PowerShell でダウンロードファイルの完全性を確認できます:

```powershell
Get-FileHash .\QuickDiskRescue-binary.zip -Algorithm SHA256
```

## 使い方

1. `QuickDiskRescue.exe` を管理者で起動する
2. 左の物理ディスクを選ぶか、イメージファイルを開く
3. 診断 → フォルダ救出（保存先は別ドライブ）
4. 可能なら先にイメージ保存

CLI:
## CLI

```powershell
.\dist\QuickDiskRescue_cli.exe list
.\dist\QuickDiskRescue_cli.exe diagnose --source \\.\PhysicalDrive1
.\dist\QuickDiskRescue_cli.exe tree --source disk.img --partition 0
.\dist\QuickDiskRescue_cli.exe copy --source disk.img --from /Users --out E:\rescued
.\dist\QuickDiskRescue_cli.exe image --source \\.\PhysicalDrive1 --out E:\disk.img
.\dist\QuickDiskRescue_cli.exe carve --source disk.img --out E:\carved
```
QuickDiskRescue_cli.exe list
QuickDiskRescue_cli.exe diagnose --source \\.\PhysicalDrive1
QuickDiskRescue_cli.exe tree --source disk.img --partition 0
QuickDiskRescue_cli.exe copy --source disk.img --from /Users --out E:\rescued
QuickDiskRescue_cli.exe image --source \\.\PhysicalDrive1 --out E:\disk.img
QuickDiskRescue_cli.exe carve --source disk.img --out E:\carved

## ソースからのビルド

```powershell
winget install --id BrechtSanders.WinLibs.MCF.UCRT --exact --source winget
scripts\build.bat
# → dist\QuickDiskRescue.exe
```

## リポジトリ構成

```
QuickDiskRescue/
├── src/
│   ├── app/              GUIホスト & Windowsリソース（main_gui.cpp, .rc, .ico, .manifest）
│   ├── cli/              CLIエントリーポイント & リソース（main_cli.cpp, .rc）
│   ├── engine/           ディスク救出 & BitLockerエンジン（engine.cpp, engine.h, bitlocker.cpp）
│   └── ui/               UIソース（index.html, css/, js/, img/）
├── proto/tests/          Pythonテスト・検証スクリプト
├── scripts/              build.py, build.bat, bundle_html.py
├── docs/                 仕様、開発環境、バージョン情報
│   └── distribution/     配布用ユーザー文書
├── dist/                 フラットなビルド成果物（Git管理外、.gitkeepのみ保持）
└── .github/workflows/    CI / Release ワークフロー
```

## 注意

- ソースディスクへの書き込み、初期化、フォーマット、GPT 修復はしません
- SSD の TRIM 済み領域は空です
- 死にかけディスクへの全セクタ走査は負荷になります

MIT License. 作者: maktak-105
