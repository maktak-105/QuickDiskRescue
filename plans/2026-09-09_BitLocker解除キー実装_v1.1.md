# 計画書: BitLocker解除キー実装(v1.1)

v1.0 からの差分: Windows WMI だけにしない。**使える解除・読取経路は全部使う。** v1.0 は残す。本ファイルが新しい正。

## 目的
ユーザーが渡した回復パスワード / `.bek` / パスフレーズを使い、BitLocker 区画を読んで、既存の NTFS フォルダ救出に乗せる。

## 使えるもの（全部使う）

優先順。上が失敗したら次。

| 優先 | 経路 | 何に効く | 根拠 |
|---|---|---|---|
| 1 | 既に解除済みのボリュームを読む `\\.\X:` / `\\?\Volume{guid}` | 今動いている PC の C:、Windows が既に Unlock したディスク | 鍵不要。OS が FVE を透過 |
| 2 | `Win32_EncryptableVolume.UnlockWithNumericalPassword` | 48桁回復パスワード。使用領域のみ暗号化も含む | [MS Learn](https://learn.microsoft.com/en-us/windows/win32/secprov/unlockwithnumericalpassword-win32-encryptablevolume) |
| 3 | 同 `UnlockWithExternalKey` | `.bek` | 同クラス |
| 4 | 同 `UnlockWithPassphrase` | データドライブのパスフレーズ | 同クラス |
| 5 | `manage-bde -unlock` | WMI が失敗したときの公式 CLI フォールバック。レターまたは `\\?\Volume{guid}` | [manage-bde unlock](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/manage-bde-unlock) |
| 6 | ボリューム特定: `FindFirstVolume` + `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS` + `IOCTL_STORAGE_GET_DEVICE_NUMBER` | レター無しでも PhysicalDriveN + 区画 → Volume GUID | Win32 |
| 7 | イメージは `OpenVirtualDisk` / `AttachVirtualDisk` して 2〜6 | `.img` / `.vhd` を Windows に認識させてから解除 | VirtDisk |
| 8 | libbde（LGPL-3.0、動的リンク） | Windows がボリュームを作っていない raw / イメージ。回復パスワード・パスフレーズ・`.bek` | [libbde](https://github.com/libyal/libbde) |

GPL エンジン禁止はユーザー決定ではない。libbde は経路 8 として使う。about の「サードパーティ C++ なし」は、この機能を入れるなら書き換える。

## 使えないもの（実装しない）

- 鍵が無い
- 他マシンの TPM だけ（回復キー無し）
- 総当たり
- 非公開 `fveapi` のリバース
- libbde の Used Disk Space Only（非対応）。その形式は経路 1〜7 に任せる

## 画面

BitLocker 行を選んだとき、鍵入力（48桁 / `.bek` 参照 / パスフレーズ）。成功したらツリー。失敗したら次の経路へ。全部失敗したら今どおり「暗号化されているので出せない」。鍵はメモリだけ。ディスクへ書かない。保存しない。

## 検証

- 解除済み C: → 経路 1 でツリー（鍵なし）
- Windows 認識済み BitLocker データボリューム + 正しい 48桁 → 経路 2 でツリー
- 誤った鍵 → エラー。ソースへ書かない
- ボリューム無し raw は libbde。Used Space Only で libbde が失敗したら、その旨を出して 1〜7 を案内

## リスク

- libbde 動的リンクは LGPL 告知（LICENSE に記載）が必要
- VHD アタッチは管理者と VirtDisk.dll
- 経路 1 は「今動いている OS のディスク」向け。壊れたディスクの本命は 2〜8
