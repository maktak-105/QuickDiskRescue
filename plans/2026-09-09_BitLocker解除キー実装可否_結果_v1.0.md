# 結果: BitLocker解除キーを扱う実装ができるのか(v1.0)

実装はしていない。可否の判断のみ。

## 結論

**Windows に解除させる形なら実装できる。** 公式は `Win32_EncryptableVolume.UnlockWithNumericalPassword`（48桁）と `UnlockWithExternalKey`（.bek）。解除後は `\\?\Volume{guid}` を NTFS として読む。

**生ディスク上の FVE をアプリ内で復号する形は、このリポジトリでは採用しない。** libbde は LGPL-3.0（GPL エンジン禁止に抵触しうる）。かつ Used Disk Space Only は libbde 非対応。Microsoft は raw PhysicalDrive を鍵で直接復号する公開 C++ API を出していない。

## 前提が崩れるとき

Windows がボリュームオブジェクトを作れていない（GPT 破損・未認識・イメージ未マウント）と、WMI 解除は使えない。鍵が無い／TPM のみの他マシンディスクは解けない。
