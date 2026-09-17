# QuickDiskRescue specification

## 1. Overview

- **Name**: QuickDiskRescue
- **Purpose**: Recover folders from disks Windows will not mount. Never write to the source.
- **OS**: Windows 10 / 11 (64-bit)
- **Implementation**: C++17 (MinGW-w64) + WebView2 + HTML/CSS/vanilla JS
- **Distribution**: flat ZIP on GitHub Releases
- **Version**: v1.0.1
- **Rights**: GUI `requireAdministrator`. CLI `asInvoker`

No WRITE/TRIM/format/initialize/GPT rewrite on the source.

## 2. Architecture

```text
[HTML/CSS/JS (WebView2)]  ←WebMessage JSON {type}→  [webview_main.cpp]  →  [engine.cpp]
```

- `engine.cpp`: raw I/O, identify, GPT/MBR, NTFS raw, imaging, carving. GUI-free.
- `webview_main.cpp`: Win32 + WebView2. Workers post `WM_POST_JSON`.
- Bundled HTML is embedded as RCDATA.

## 3. UI

Quick-series dark UI. A permanent "source read-only" mark.

Stable ids: `#btn-lang` `#btn-diagnose` `#btn-deep-scan` `#btn-image` `#btn-copy` `#btn-carve` `#btn-cancel` `#disk-list` `#tree-body` `#status` `#about-overlay` `#about-close`

No initialize/format controls.

## 4. Features

Physical disk enum; hardware identify (three capacity figures, sector sizes, bus, SMART fail prediction display only); unbuffered reads with ATA/SCSI/NVMe READ fallback; GPT primary vs backup; lost-partition signature scan; NTFS folder-tree copy-out; disk image with bad-LBA map; carving of unallocated clusters; refuse output onto the source disk; CLI JSON.

Folder reconstruction order: $MFT parent → $MFTMirr / backup boot → $I30 → carving (no path).

Skip compressed/encrypted/reparse NTFS streams with a warning.

## 5. WebMessage protocol

New app. Key is `type`. `PostWebMessageAsJson`. Real parser, no substring matching.

JS → native: `ping`, `list_disks`, `open` (`path`), `set_output` (`path`), `diagnose` (`source`), `deep_scan` (`source`), `tree` (`source`,`partition`), `image` (`source`,`dest`), `copy_out` (`source`,`partition`,`path`,`dest`), `carve` (`source`,`partition`,`dest`), `cancel`, `browse_open`, `browse_save` (`kind`), `quit`.

Native → JS: `pong`, `disks`, `diagnose_result`, `tree_result`, `progress`, `copy_result`, `image_result`, `carve_result`, `error`, `state`.

`QUICKAPPSTEST=1` must not open PhysicalDrive. `--open <path>` sets the initial image.

## 6. Safety

Source handles are `GENERIC_READ` only. Default I/O queue depth is 1. No SMART long self-test. Test mode uses fixtures only.
