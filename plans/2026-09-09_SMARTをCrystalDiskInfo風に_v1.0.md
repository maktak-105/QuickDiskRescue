# 2026-09-09 SMART を CrystalDiskInfo 風に

## 要望

SMART 診断を CrystalDiskInfo みたいにしたい。

## 方針

独自実装。CrystalDiskInfo のソースはコピーしない。
健康（正常／注意／異常）、全属性表（ID・名前・現在・最悪・閾値・RAW）、温度、通電時間、型番／シリアル／ファーム、ドライブ文字。
自己診断テストは走らせない。
