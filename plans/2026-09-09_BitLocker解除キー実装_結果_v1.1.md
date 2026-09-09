# 結果: BitLocker解除キー実装(v1.1)

## 実施

- 解除済みボリューム（`\\.\C:` / `\\?\Volume{guid}`）を GPT 区画と照合して NTFS ツリーに乗せる。
- ロック時は 48桁 / パスフレーズ / `.bek` を受け取り、WMI `UnlockWithNumericalPassword` / `UnlockWithPassphrase`、失敗時 `manage-bde -unlock`。
- `.vhd` / `.vhdx` / `.iso` は `AttachVirtualDisk`（読取専用）。
- GUI に鍵入力ダイアログ。鍵は保存しない。
- CLI `unlock --recoverypassword|--bek|--password`。
- EXE 再ビルド済み（GUI 1,910,913 bytes）。

## 入れてないもの

- libbde 同梱なし（Windows がボリュームを作っていない raw のアプリ内 FVE 復号は未実装）。その場合は `ntfs_not_found`。使用領域のみ暗号化は Windows 解除経路に任せる。
- 鍵無し・他マシン TPM のみは解けない（計画どおり）。

## 検証

- 既存 CLI テスト（GPT mismatch / carve）は別途実行。
- 実機の BitLocker 48桁での解除は、鍵がこのセッションに無いため未実施。
