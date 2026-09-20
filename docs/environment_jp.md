# 開発環境

## 実行環境

| 項目 | 内容 |
|---|---|
| OS | Windows 10 / 11 (64bit) |
| C++ | C++17 |
| コンパイラ | MinGW-w64 (g++)。WinLibs MCF UCRT |
| Python | 3.x（ビルドスクリプト用） |
| WebView2 SDK | `C:\tools\webview2\build\native\include`（`WEBVIEW2_INCLUDE` で変更可） |

## ビルド

```powershell
cd QuickDiskRescue
build.bat
```

成果物は `dist\QuickDiskRescue.exe` と `QuickDiskRescue_cli.exe`。

GUI は管理者権限が必要。CLI は asInvoker。`QUICKAPPSTEST=1` では物理ディスクを開かない。
