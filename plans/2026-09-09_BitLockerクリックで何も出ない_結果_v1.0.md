# 結果: BitLockerクリックで何も出ない(v1.0)

## 原因

1. ボリューム照合が GPT オフセット頼みで失敗 → `ntfs_not_found`。鍵ダイアログは `bitlocker_locked` のときしか出ない。
2. 照合できても C: の MFT を 1 レコードずつ読むので、ツリーが返らず固まったように見える。

## 修正

- 同じディスクのドライブレター（`\\.\C:`）を優先。
- ボリュームは OVERLAPPED なしで開く。
- BitLocker 区画なら失敗時も鍵ダイアログ。
- MFT を 1MB 単位で読む。ツリー JSON は深さ 4 / 2500 ノードまで。

## 検証

- `tree --source \\.\C:` が約 15 秒で `ok:true`、Users / Program Files を含む。
- 既存 CLI テスト PASS。
- GUI の目視はユーザー側（UAC）。
