# QuickDiskRescue 仕様書

## 1. アプリ概要

- **名称**: QuickDiskRescue（ディスク救出）
- **目的**: Windows がマウントできないディスクから、フォルダ構造を保って別ドライブへファイルを救出する。ソースへは書かない。
- **対象OS**: Windows 10 / 11 (64-bit)
- **実装**: C++17 (MinGW-w64) + WebView2 + HTML/CSS/バニラJS
- **配布形態**: GitHub Releases の ZIP（フラット構成）
- **バージョン**: v1.0.0
- **権限**: GUI は `requireAdministrator`。CLI は `asInvoker`

ソースへの WRITE / TRIM / フォーマット / 初期化 / GPT 書き戻しは行わない。

## 2. アーキテクチャ

```text
[HTML/CSS/JS (WebView2)]  ←WebMessage JSON {type}→  [webview_main.cpp]  →  [engine.cpp]
```

- `engine.cpp`: 生ディスク I/O、識別、GPT/MBR、NTFS raw、イメージ、カービング。GUI 非依存。
- `webview_main.cpp`: Win32 + WebView2。ワーカースレッドから `WM_POST_JSON`。
- バンドル HTML を RCDATA として EXE に埋め込む。

## 3. 画面構成

Quick 共通ダーク UI。タイトルバー相当のメニューに「ソース読取専用」を常時表示。

| 領域 | 内容 |
|---|---|
| メニュー | ファイル（イメージを開く / 終了）、ヘルプ（ヘルプ / バージョン情報） |
| 言語 | 右上 🌐 日本語 / English |
| 左 | 物理ディスク一覧と開いたイメージ。経過時間 |
| メイン | ハードウェア情報、パーティション／消失候補、NTFS フォルダツリー、カービング結果 |
| 操作 | 診断、消失走査、イメージ保存、フォルダ救出、カービング、停止 |
| ステータス | 読取専用、進捗、件数、警告 |

安定 `id`: `#btn-lang` `#btn-diagnose` `#btn-deep-scan` `#btn-image` `#btn-copy` `#btn-carve` `#btn-cancel` `#disk-list` `#tree-body` `#status` `#about-overlay` `#about-close`

初期化・フォーマットボタンは置かない。

## 4. 機能一覧

| # | 機能 | 説明 |
|---|---|---|
| 1 | ディスク列挙 | `\\.\PhysicalDriveN`。検査モードでは物理ディスクを開かない |
| 2 | ハードウェア識別 | 容量、セクタ、バス、SMART（ATA 属性 / NVMe Health。自己診断テストは走らせない） |
| 3 | 生読み | `FILE_FLAG_NO_BUFFERING`。失敗 LBA は ATA/SCSI/NVMe READ へフォールバック。リトライ後スキップ |
| 4 | GPT/MBR 診断 | primary と backup、CRC、LastUsableLBA と実サイズの不一致 |
| 5 | 消失パーティション走査 | セクタ境界で `EFI PART` / `NTFS    ` / MFT `FILE` を探す |
| 6 | NTFS フォルダ救出 | $Boot、$MFTMirr、$MFT、parent、$I30。フォルダを選んでサブツリーごと別ドライブへコピー |
| 7 | イメージ | ソース read-only。不良 LBA はゼロ埋め + マップ |
| 8 | カービング | 未割り当て（$Bitmap 可なら）またはレンジ。パス無しは `RECOVERED/carved/<type>/` |
| 9 | 保存先ガード | ソースと同じ物理ディスクなら拒否 |
| 10 | CLI | JSON stdout。`list` `diagnose` `tree` `copy` `image` `carve` `repair-gpt` |
| 11 | GPT修復 | CRC が通るヘッダを正として、実サイズに合わせ primary/backup と Protective MBR を書き直す。実ディスクは二重確認 |

フォルダ構造の優先: $MFT parent → $MFTMirr / backup boot → $I30 → カービング（パス無し）。

圧縮・暗号化・リパース NTFS 属性はコピーせず警告する。

## 5. WebMessage プロトコル

新規アプリ。キーは `type`。`PostWebMessageAsJson`。本物のパーサ（部分文字列検索禁止）。

### JS → native

| type | パラメータ | 説明 |
|---|---|---|
| `ping` | なし | ハートビート |
| `list_disks` | なし | ディスク一覧 |
| `open` | `path` | イメージを診断対象にする（`--open` と同じ） |
| `set_output` | `path` | 保存先ディレクトリ |
| `diagnose` | `source` | 構造診断（先頭・末尾・各パーティション） |
| `deep_scan` | `source` | 消失パーティションの全走査 |
| `tree` | `source`, `partition` | NTFS フォルダツリー |
| `image` | `source`, `dest` | ディスクイメージ |
| `copy_out` | `source`, `partition`, `path`, `dest` | フォルダ救出 |
| `carve` | `source`, `partition`, `dest` | カービング |
| `cancel` | なし | 停止 |
| `browse_open` | なし | イメージ選択ダイアログ |
| `browse_save` | `kind` (`image`/`dir`) | 保存先ダイアログ |
| `quit` | なし | 終了 |

### native → JS

| type | パラメータ | 説明 |
|---|---|---|
| `pong` | なし | ping 応答 |
| `disks` | `items[]` | ディスク一覧 |
| `diagnose_result` | 診断 JSON | 構造診断 |
| `tree_result` | `root` | フォルダツリー |
| `progress` | `done`, `total`, `message` | 進捗 |
| `copy_result` | `copied`, `failed`, `partial` | 救出結果 |
| `image_result` | `ok`, `path`, `bad_lbas` | イメージ結果 |
| `carve_result` | `count`, `dest` | カービング結果 |
| `error` | `message` | エラー |
| `state` | `busy`, `readonly` | 状態 |

`QUICKAPPSTEST=1` のとき PhysicalDrive を開かない。`--open <path>` でイメージを初期対象にする。

## 6. 処理フロー

1. 起動（GUI は昇格）→ `list_disks`
2. ディスクまたはイメージを選ぶ → `diagnose`
3. NTFS が見えたら `tree` → フォルダ選択 → `copy_out`（別ドライブ）
4. 推奨: 先に `image`。以降はイメージを `open`
5. メタデータが死んでいる領域だけ `carve`

## 7. 安全規則

- ソース ハンドルは `GENERIC_READ` のみ
- 既定 I/O キュー深さ 1
- SMART 長時間テストはしない
- 検査モードはフィクスチャのみ
