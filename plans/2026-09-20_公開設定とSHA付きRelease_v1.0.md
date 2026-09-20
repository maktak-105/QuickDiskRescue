# 公開設定とSHA付きReleaseの復旧

## 目的
- QuickDiskRescueリポジトリを公開する。
- 既存の`v1.0.2`タグからGitHub Actionsを実行し、配布ZIPとSHA-256一覧をReleaseへ添付する。

## 確認項目
- GitHub上で可視性がPublicになっている。
- Release workflowが成功し、`QuickDiskRescue-binary.zip`と`SHA256SUMS.txt`が添付される。
- 添付SHAとRelease ZIPのSHA-256が一致する。
