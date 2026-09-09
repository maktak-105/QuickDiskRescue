# -*- coding: utf-8 -*-
import os, struct, zlib, pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIX = ROOT / "tests" / "fixtures"
FIX.mkdir(parents=True, exist_ok=True)

SECTOR = 512
N = 4096  # 2 MiB


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def gpt_header(my, alt, first_usable, last_usable, part_lba, part_crc):
    disk_guid = bytes(range(16))
    raw = bytearray(92)
    raw[0:8] = b"EFI PART"
    struct.pack_into("<I", raw, 8, 0x00010000)
    struct.pack_into("<I", raw, 12, 92)
    struct.pack_into("<Q", raw, 24, my)
    struct.pack_into("<Q", raw, 32, alt)
    struct.pack_into("<Q", raw, 40, first_usable)
    struct.pack_into("<Q", raw, 48, last_usable)
    raw[56:72] = disk_guid
    struct.pack_into("<Q", raw, 72, part_lba)
    struct.pack_into("<I", raw, 80, 128)
    struct.pack_into("<I", raw, 84, 128)
    struct.pack_into("<I", raw, 88, part_crc)
    c = crc32(bytes(raw))
    struct.pack_into("<I", raw, 16, c)
    return bytes(raw) + b"\x00" * (SECTOR - 92)


img = bytearray(N * SECTOR)
# protective MBR
img[0x1BE] = 0x00
img[0x1C2] = 0xEE
struct.pack_into("<I", img, 0x1C6, 1)
struct.pack_into("<I", img, 0x1CA, 0xFFFFFFFF)
img[510] = 0x55
img[511] = 0xAA

# one NTFS-like partition entry (type Microsoft basic data GUID)
entries = bytearray(128 * 128)
ms_basic = bytes.fromhex("A2A0D0EBE5B9334487C068B6B72699C7")
entries[0:16] = ms_basic
entries[16:32] = bytes(range(16, 32))
struct.pack_into("<Q", entries, 32, 34)
struct.pack_into("<Q", entries, 40, N - 34 - 1)
name = "RESCUE".encode("utf-16le")
entries[56:56 + len(name)] = name
part_crc = crc32(bytes(entries))

# mismatch: claim last_usable as if ~2TB disk (3907029168 LBA-ish ~2e12/512)
fake_last = 3907029168
hdr = gpt_header(1, N - 1, 34, fake_last, 2, part_crc)
img[SECTOR:SECTOR * 2] = hdr
img[SECTOR * 2:SECTOR * 2 + len(entries)] = entries

# backup header at last LBA, backup entries before it
b_entries_lba = N - 1 - 32
img[b_entries_lba * SECTOR:b_entries_lba * SECTOR + len(entries)] = entries
bhdr = gpt_header(N - 1, 1, 34, fake_last, b_entries_lba, part_crc)
img[(N - 1) * SECTOR:] = bhdr

# NTFS OEM at partition start LBA 34
ntfs = bytearray(SECTOR)
ntfs[3:11] = b"NTFS    "
struct.pack_into("<H", ntfs, 11, 512)
ntfs[13] = 8
img[34 * SECTOR:34 * SECTOR + SECTOR] = ntfs

(FIX / "gpt_mismatch.img").write_bytes(bytes(img))

# carving fixture: JPEG + PDF magics in empty image
carve = bytearray(1024 * 64)
carve[4096:4099] = b"\xff\xd8\xff"
carve[5000:5002] = b"\xff\xd9"
carve[8192:8196] = b"%PDF"
carve[9000:9005] = b"%%EOF"
(FIX / "carve.img").write_bytes(bytes(carve))
print("fixtures ok", FIX)
