# QuickDiskRescue help

Rescue folders from disks Windows will not mount. Run elevated.

This is not a CrystalDiskInfo clone (SMART layout is similar) and not an EaseUS clone.

## Contents

- Start
- Screen
- Rules and order
- State and action
- Partition roles and auto-pick
- Hardware / SMART / GPT
- Buttons
- Status line
- Common mistakes
- Do not
- CLI
- What it does and does not do

## Start

1. Run `QuickDiskRescue.exe`. UAC appears. It must run elevated.
2. The badge stays “Source read-only”. The only write to the source is GPT repair.
3. Physical disks list on the left at startup. Diagnose starts when you click a disk.
4. Top-right “🌐 日本語” switches the UI and this help to Japanese.
5. Without WebView2 Runtime the app will not start.

Prefer attaching the damaged disk to another PC. Testing against the live C: volume often yields nothing (BitLocker or locks).

## Screen

### Left (disk list)

Looks like `Drive0  (C:)  476.9 GB  SAMSUNG...`.

- `DriveN` is `\\.\PhysicalDriveN`.
- Letters in parentheses are Windows drive letters on that physical disk. None means unmounted.
- Size is the whole disk, not a partition.
- Click starts diagnose and the elapsed clock.

Opening an image does not replace the left list. Confirm the path in the status line and diagnose pane.

Elapsed time runs only during diagnose, lost-scan, image, rescue, carve, GPT repair, or tree load. It freezes on completion or error. The frozen number is the last job’s duration.

### Buttons

Diagnose / Lost-partition scan / Save image / Rescue folder / Carve / Repair GPT / Stop

Select a disk first. GPT repair without a selection shows `select a disk first`.

### Centre (top to bottom)

1. Hardware (size, sector, bus, model, GPT summary)
2. SMART (health badge, temperature, attribute table)
3. Partition list (click a row to load that volume’s tree)
4. Folder tree (name / size / flags)

### Status line

Completion, errors, BitLocker notes, progress (`image 123 MB / 1.0 TB`). Trust this line for success or failure.

### Menus

- File → Open image (Ctrl+O). Diagnose a raw image file.
- File → Exit (Ctrl+Q)
- Help → Help (this text)
- Help → About

## Rules

1. The only write to a damaged disk is GPT repair. Everything else is read-only.
2. Image to another drive before repeated scans. If SMART is caution/bad, image first.
3. Never save recovered files onto the damaged disk. This build does not block same-disk destinations. You must pick another drive.
4. Trust the tree only after you check the partition role. Recovery and EFI are not C:.
5. BitLocker cannot be folder-rescued raw without the key. Carving does not decrypt it.

## Order

1. Select a disk or File → Open image.
2. Diagnose runs. Read hardware, SMART, partitions, GPT.
3. Match the state table below.
4. Save an image to another drive (do this first if SMART is caution/bad).
5. Prefer further work on the image.
6. Click NTFS (not Recovery). Wait for the tree.
7. Select the source folder in the tree, then Rescue folder. The Windows folder dialog is the destination.

## State and action

Use this table when unsure.

| State | What you see | Action |
|---|---|---|
| Disk listed, no Explorer letter | DriveN, partitions, no letter | Image → click NTFS (not Recovery) → folder rescue |
| Reported size does not match the disk | mismatch:true; size ≠ geom or LastUsable ≠ real LBA | Image → GPT repair (several confirms) → select the disk again |
| One GPT copy bad | primary or backup crc is 0 | Image → GPT repair |
| Both GPT copies bad | `no_valid_gpt_header` | Do not GPT-repair. Lost-scan for NTFS, then folder rescue if a listed NTFS exists, else carve |
| NTFS leftover not in the table | `lost: N` after lost-scan | Lost-scan only locates signatures. Rescue from a listed NTFS row. Offsets are not wired to copy |
| Tree shows Users / Windows | NTFS, not Recovery | Select folder → folder rescue to another drive |
| Empty tree, `ntfs_not_found` | EFI / MSR / FAT, or dead boot | Not NTFS. Click Basic + NTFS |
| Only Recovery folders | Recovery, small NTFS | Not the OS volume. If C is BitLocker, see next row |
| BitLocker | fs BitLocker | If already unlocked, rescue folders. If locked, 48-digit / passphrase / .bek. No key: cannot |
| SMART caution/bad | health caution/bad; pending/realloc > 0 | Image first; further work on the image |
| SMART n/a | `SMART n/a (... )` | Continue; ignore health |
| Names gone, content only | MFT unreadable, empty tree, dead table | Carve. Numbered files. Other drive |
| Testing live C: | (C:) | BitLocker C cannot be raw-rescued. Use non-encrypted NTFS or an image |

### Disk listed, no Explorer letter

Windows may simply not have mounted it. This app reads raw without a letter. If the partition table and NTFS boot are readable, the tree can still appear.

Image first, click NTFS (not Recovery), folder-rescue if the folders you need are there.

### Size wrong

After an incomplete shutdown or a cut-off write, GPT LastUsable / AlternateLBA can disagree with the real disk size. The pane shows `mismatch:true`.

GPT repair rewrites headers, the partition array, and the protective MBR to match real size. File contents are unchanged. Image first. Physical disks get several confirm dialogs.

### Both GPT copies bad

Repair copies the side whose CRC is valid. If both are invalid there is nothing to copy, so the app returns `no_valid_gpt_header` and does not write. Repeating repair will not help. Go to lost-scan or carve.

### BitLocker

Magic `-FVE-FS-` is labelled BitLocker.

If Windows has already unlocked the volume (live C: on this PC), folder rescue works without a key. If it is locked, enter the 48-digit recovery password, a passphrase, or a `.bek` file. The key stays in memory and is not saved. Unlock is done by Windows (WMI / manage-bde). `.vhd` / `.vhdx` images are attached first.

No key, or TPM-only on another machine: cannot unlock.

## Partition roles

Only the clicked row is read. The row looks like:

`#2 Basic NTFS 100.00GB`

| Field | Meaning |
|---|---|
| #N | Partition index. Used for tree load and CLI `--partition` |
| Role | EFI / MSR / Basic / Recovery |
| FS | NTFS / BitLocker / FAT32 / exFAT / GPT / empty |
| GB | `(last LBA - first LBA + 1) × 512`, even on 4K disks |
| Name | GPT partition name; may be empty |

| Label | Meaning | Folder rescue |
|---|---|---|
| EFI | Boot FAT | No (`ntfs_not_found`) |
| MSR | Microsoft reserved | No |
| Basic + NTFS | Data / OS | Yes |
| Basic + BitLocker | Encrypted NTFS | No without key |
| Recovery + NTFS | Recovery, often small | Recovery files only, not C: |
| FAT / exFAT | ESP, cards | This build rescues NTFS only |

## Auto-picked partition

After you click a disk, one row is selected in this order:

1. Largest NTFS that is not Recovery
2. Else if BitLocker exists → do not load a tree; show the BitLocker note
3. Else largest Recovery NTFS

Clicking a BitLocker row does not load a tree. Click a data NTFS yourself to load that volume.

## Hardware line

| Display | Meaning |
|---|---|
| `C:  size: 476.9 GB  geom: 476.9 GB` | size is the handle capacity; geom is IOCTL geometry. Images have no letter |
| `sector: 512 / phys 4096  bus: NVMe` | logical / physical sector, bus |
| `model: ...` | identify model |
| `GPT primary:true crc:1 backup:true mismatch:false` | primary present, primary CRC, backup present, capacity mismatch |

`crc:1` means the checksum passed. `crc:0` means that copy is bad or unread.

`mismatch:true` when:

- `gpt_last_usable_lba + 34` is not `disk_lbas` (and +1 is not either)
- `geom` ≠ `size`

## SMART

Drive-level, not partition-level. BitLocker volumes can still report disk health. No SMART self-test is run.

| Display | Meaning |
|---|---|
| Good | No threshold breach; realloc / pending / uncorrectable are 0 |
| Caution | Reallocated, pending, or uncorrectable raw > 0, or temperature high |
| Bad | Attribute at/under threshold, or Windows predict-failure |
| Temp | Sensor. May be missing |
| POH | Power-on hours |
| Count | Power cycles |
| SN / FW | Serial and firmware |
| spare / used | NVMe spare and percent used |
| SSD / N RPM | IDENTIFY rotation. 0 or 1 treated as SSD |
| Attribute table | ID (hex), name, current, worst, threshold, RAW |

Watched attributes:

| ID | Name | Reading |
|---|---|---|
| 05 | Reallocated sectors | RAW > 0 is caution. Rising means image now |
| 09 | Power-on hours | Hours |
| C5 / 197 | Pending sectors | Unprocessed defects |
| C6 / 198 | Uncorrectable sectors | Unreadable |
| C2 / 194 | Temperature | High → caution |

USB bridges often cannot pass SMART (`SMART n/a`). Continue rescue; ignore health.

Red/yellow rows are caution or bad. Postpone whole-disk scans on the physical device.

## GPT fields

| Display | Meaning |
|---|---|
| gpt_primary / backup | Headers present at start / end |
| crc | Checksum. 0 means that copy is bad |
| gpt_last_usable_lba | What GPT thinks is the last usable LBA |
| disk_lbas | Real sector count (size / sector) |
| size_mismatch | Reported size does not match the real disk |

GPT repair rewrites the protective MBR, primary header, partition array, and backup header/array location. The CRC-valid copy is the source of truth. File data is not touched.

## Buttons

### Diagnose

Reads GPT (start and end), partition types, SMART. Also runs when you click a disk. Does not write.

Status becomes `diagnose done`, then a tree load follows unless only BitLocker was found.

### Lost-partition scan

Whole-disk search. Does not copy files.

| Signature | Meaning |
|---|---|
| `EFI PART` | GPT header |
| `NTFS    ` | NTFS boot |
| `FILE` | MFT record |

`lost: N` appears under the partition list. Slow on large disks. A live NTFS yields many FILE hits. The useful hits are NTFS boots not already in the table.

This build does not start folder rescue from those offsets. They are location hints. Rescue from a listed NTFS row.

### Save image

Raw clone to a file. The Windows dialog is a file save, not a folder.

- Unreadable LBAs are zero-filled and listed in `name.img.badlba.txt`.
- Does not write the source.
- Destination free space must be at least the disk size.
- Progress: `image done / total`.
- Done: `image {"ok":true,"path":"...","bad_lbas":N}`.

After imaging, File → Open image and do diagnose / rescue / GPT repair on the file.

GPT repair on an image is safer than on the physical disk. Try the image first.

### Rescue folder

The Windows folder dialog is the destination, not a folder on the damaged disk. The source is the on-screen tree.

1. Click NTFS in the partition list (not Recovery). Wait for the tree. Large volumes take time.
2. Click the folder to rescue. If none, the whole volume (`/`).
3. Press Rescue folder.
4. Pick an empty folder on a healthy drive. Do not pick the damaged disk.
5. Done: `copied N failed M`. Failures include compressed/encrypted NTFS attributes and read errors.

Tree columns:

| Column | Meaning |
|---|---|
| Name | 📁 folder, 📄 file |
| Size | File size. Folders may show near 0 |
| Flags | `dir` / `file`. Deleted: `DEL`. Unused MFT: `unused` |

`tree failed: ntfs_not_found` means that row is not readable NTFS. Click another row.

### Carve

Signature cut without MFT. Files land as `carved\jpeg\00001.jpeg`. No original paths. False positives happen. docx may appear as ZIP. Types: JPEG / PNG / GIF / PDF / ZIP.

The folder dialog is the destination. Done: `carved N`.

Try folder rescue first if names matter. Carve is last resort.

### Repair GPT

Rewrites GPT headers, the partition array, and the protective MBR to match real size. File contents unchanged.

Use when:

- Real size and LastUsable disagree (`mismatch:true`)
- Only one of primary/backup CRC is valid

Do not use when:

- Both GPT copies are invalid (`no_valid_gpt_header`)
- You only want to probe a BitLocker system disk
- The physical disk has not been imaged
- SMART is bad and you have not imaged yet

Physical disk confirms (three steps):

1. In-app “This writes GPT headers only…”
2. Warning MessageBox about writing headers and the partition table
3. Final MessageBox “最終確認: 実ディスクの GPT を修復します。”

Images get step 1 only (no MessageBox).

| Status | Meaning |
|---|---|
| `GPT repaired (last usable A -> B)` | Wrote. A is old LastUsable, B matches real size |
| `GPT repair: already consistent` | Nothing to fix |
| `GPT repair failed: no_valid_gpt_header` | Both copies invalid; no write |
| `GPT repair failed: write_open_failed` | Could not open for write (lock, rights, read-only) |
| `GPT repair failed: write_failed` | Partial write possible. Restart from the image |

After repair, select the disk again. Confirm `mismatch:false` and CRC before folder rescue.

### Stop

Cancels image, scan, or copy. Partial rescued files remain. Prefer letting a GPT write finish rather than stopping mid-write.

## Status line

| Text | Meaning |
|---|---|
| Ready | Startup. No disk selected |
| diagnose done | Diagnose finished; a tree load may follow |
| tree done (partition N) | Tree for partition N |
| tree failed: ntfs_not_found | That partition is not readable NTFS |
| tree failed: open_failed | Could not open the source |
| copied N failed M | Folder rescue counts |
| copy failed: ... | Rescue never started |
| image done / total | Image progress |
| image { ... } | Image finished; bad_lbas is the bad-sector count |
| carved N | Files carved |
| lost: N | Lost-scan hits (also under the partition list) |
| select a disk first | GPT repair with no disk selected |
| この区画は BitLocker です。... | That partition is encrypted. Raw folder rescue is not possible |
| GPT repaired ... | Repair wrote |
| GPT repair failed: ... | Refused or write failed |
| error: ... | Other failure |

## Common mistakes

- Treating a Recovery tree as “C: is back”. A few GB is Recovery.
- Using the folder-rescue dialog to pick a folder on the damaged disk. That dialog is the destination.
- Reading `lost: N` as “N folders will return”. It is signature hits.
- Expecting GPT repair to fix file contents. It fixes headers and the partition array.
- Assuming SMART good means every file is readable. Filesystem damage is separate.
- Testing live C:, getting an empty tree, and blaming the app. BitLocker is by design.
- Imaging onto the same physical disk. This build does not reject it. Pick another drive.

## Do not

- Format or initialize the damaged disk in Disk Management or a Windows installer
- Save recovered files onto that same disk
- Repeat lost-scan or GPT repair on a dying disk before imaging
- Treat a Recovery tree as “C: is back”
- Expect GPT repair when both headers are invalid
- Expect carving to decrypt BitLocker
- Expect TRIM’d SSD free space to still hold deleted files. After TRIM it is empty

## CLI

`QuickDiskRescue_cli.exe` does not prompt UAC. JSON on stdout. Opening a physical disk usually still needs elevation.

- `list`
- `diagnose --source \\.\PhysicalDrive1`
- `diagnose --source disk.img --deep`
- `tree --source disk.img --partition 2`
- `copy --source disk.img --from /Users --out E:\rescued --partition 2`
- `image --source \\.\PhysicalDrive1 --out E:\disk.img`
- `carve --source disk.img --out E:\carved`
- `repair-gpt --source disk.img`
- `repair-gpt --source disk.img --write`
- `unlock --source \\.\PhysicalDrive1 --partition 2 --recoverypassword 123456-...`

`repair-gpt` does not write unless `--write` is passed. The GUI writes after confirms.

`--partition` is `#N` in the partition list. The GUI uses the clicked row.

## What it does and does not do

Does:

- Raw-read a physical disk or image without a Windows letter
- Diagnose GPT/MBR, partitions, SMART
- Copy NTFS folders out when $MFT is readable
- Full-disk image
- Carve JPEG / PNG / GIF / PDF / ZIP when names are gone
- Rewrite GPT headers to match real capacity

Does not:

- Format, initialize, create partitions, or assign drive letters
- Decrypt BitLocker
- Run SMART self-tests
- Start folder rescue from lost-scan offsets
- Reject same-disk destinations (you must avoid them)
- Rescue FAT / exFAT / ReFS folders (NTFS only in this build)
