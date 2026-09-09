# QuickDiskRescue

Windows がマウントできないディスクから、フォルダ構造を保って別ドライブへ救出するツールです。ソースへは書き込みません。

Version: **v1.0.0**

実装: **C++17 + WebView2**。配布物に Python は含みません。

## 使い方

1. `QuickDiskRescue.exe` を管理者で起動する
2. 左の物理ディスクを選ぶか、イメージファイルを開く
3. 診断 → フォルダ救出（保存先は別ドライブ）
4. 可能なら先にイメージ保存

CLI:

```
QuickDiskRescue_cli.exe list
QuickDiskRescue_cli.exe diagnose --source \\.\PhysicalDrive1
QuickDiskRescue_cli.exe tree --source disk.img --partition 0
QuickDiskRescue_cli.exe copy --source disk.img --from /Users --out E:\rescued
QuickDiskRescue_cli.exe image --source \\.\PhysicalDrive1 --out E:\disk.img
QuickDiskRescue_cli.exe carve --source disk.img --out E:\carved
```

## 注意

- ソースディスクへの書き込み、初期化、フォーマット、GPT 修復はしません
- SSD の TRIM 済み領域は空です
- 死にかけディスクへの全セクタ走査は負荷になります

MIT License. 作者: maktak-105
