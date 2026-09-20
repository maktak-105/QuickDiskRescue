# 公開設定とSHA付きReleaseの復旧 結果

## 結果
- QuickDiskRescueをPublicに変更。
- 既存`v1.0.2`タグからRelease workflowを手動実行し、Release作成に成功。
- Release URL: https://github.com/maktak-105/QuickDiskRescue/releases/tag/v1.0.2

## 検証
- Release workflow成功。
- `QuickDiskRescue-binary.zip`と3つの同梱バイナリを実ファイルで照合し、SHA-256が4/4一致。
