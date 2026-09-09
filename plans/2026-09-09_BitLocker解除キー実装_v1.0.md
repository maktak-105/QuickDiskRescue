# 計画書: BitLocker解除キー実装(v1.0)

## 目的
回復パスワード（48桁）または `.bek` を受け取り、BitLocker 区画を解除したうえで、今の NTFS フォルダ救出に乗せる。

## 現状分析
- アプリは OEM `-FVE-FS-` を BitLocker と表示するだけ。鍵入力も解除も無い。
- GPL エンジン禁止はユーザー決定ではない（初期計画書に書いただけ）。libbde を門前払いする根拠には使わない。
- 公式: `Win32_EncryptableVolume.UnlockWithNumericalPassword` / `UnlockWithExternalKey`。解除後は `\\?\Volume{guid}` が NTFS として読める。
- libbde（LGPL-3.0）は raw イメージ向け。Used Disk Space Only は非対応。この PC の C: はその形式。

## 変更内容（この版でやる）

Windows に解除させる。

- BitLocker 行を選んだとき、回復パスワードまたは `.bek` を受け取る。
- その物理ディスク上の BitLocker ボリュームを WMI で特定し、Unlock する。
- 成功したらソースを `\\?\Volume{guid}`（またはレター）に切り替え、既存の `tree` / `copy_out` を使う。
- 失敗はステータスに出す。鍵を保存しない。

やらない（この版）:

- libbde の同梱（Used Space Only 非対応。LGPL 告知も別判断）
- 自前 AES-XTS 復号
- 鍵無しの総当たり
- 今動いている OS の C: を生ディスク経由で無理に読むこと（起動できているなら既に解除済み。エクスプローラーで足りる）

## 検証方法
- 鍵なし BitLocker 行: 今どおり読めない。
- テスト用に Windows が認識している BitLocker データボリュームへ正しい 48桁を渡すと Unlock が成功し、ツリーが出る（実機の鍵がある場合のみ。無いなら Unlock API 呼び出しまでをコンパイル・ドライラン）。
- 誤った鍵はエラーで、ソースへ書かない。

## リスク
- Windows がボリュームを作っていない（GPT 破損・未認識）と WMI が届かない。そのときは先に GPT。
- システムディスクを別 PC に付けたときは、レター無しボリュームの特定が必要。
- イメージファイルは未マウントだと WMI 対象外。この版は物理ディスクの Windows 認識ボリュームが対象。
