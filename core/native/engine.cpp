#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <cstdio>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <memory>
#include <sstream>
#include <cwctype>

#include "engine.h"

namespace {

int g_test_mode = 0;

bool json_get_str(const wchar_t* json, const wchar_t* key, std::wstring& out) {
    if (!json || !key) return false;
    std::wstring j = json, k = std::wstring(L"\"") + key + L"\"";
    size_t p = j.find(k);
    if (p == std::wstring::npos) return false;
    p = j.find(L'"', p + k.size());
    if (p == std::wstring::npos) return false;
    size_t p2 = j.find(L'"', p + 1);
    if (p2 == std::wstring::npos) return false;
    out = j.substr(p + 1, p2 - p - 1);
    for (size_t i = 0; i + 1 < out.size(); ++i) {
        if (out[i] == L'\\' && out[i + 1] == L'\\') out.erase(i, 1);
    }
    return true;
}

bool json_get_true(const wchar_t* json, const wchar_t* key) {
    if (!json || !key) return false;
    std::wstring j = json, k = std::wstring(L"\"") + key + L"\"";
    size_t p = j.find(k);
    if (p == std::wstring::npos) return false;
    size_t t = j.find(L"true", p + k.size());
    size_t f = j.find(L"false", p + k.size());
    if (t == std::wstring::npos) return false;
    if (f != std::wstring::npos && f < t) return false;
    return true;
}

std::wstring json_escape(const std::wstring& in) {
    std::wstring o;
    o.reserve(in.size() + 8);
    for (wchar_t c : in) {
        switch (c) {
        case L'\\': o += L"\\\\"; break;
        case L'"': o += L"\\\""; break;
        case L'\n': o += L"\\n"; break;
        case L'\r': o += L"\\r"; break;
        case L'\t': o += L"\\t"; break;
        default: o += c; break;
        }
    }
    return o;
}

std::wstring utf8_to_wide(const char* s, size_t n) {
    if (!s || n == 0) return L"";
    int w = MultiByteToWideChar(CP_UTF8, 0, s, (int)n, nullptr, 0);
    if (w <= 0) {
        std::wstring r;
        for (size_t i = 0; i < n; ++i) r += (wchar_t)(unsigned char)s[i];
        return r;
    }
    std::wstring out((size_t)w, 0);
    MultiByteToWideChar(CP_UTF8, 0, s, (int)n, &out[0], w);
    return out;
}

wchar_t* dup_json(const std::wstring& s) {
    size_t n = s.size() + 1;
    wchar_t* p = (wchar_t*)malloc(n * sizeof(wchar_t));
    if (!p) return nullptr;
    memcpy(p, s.c_str(), n * sizeof(wchar_t));
    return p;
}

uint32_t crc32_ieee(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            uint32_t mask = (uint32_t)-(int)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

uint16_t rd16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
uint32_t rd32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}
uint64_t rd64(const uint8_t* p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}
void wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
void wr64(uint8_t* p, uint64_t v) {
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

bool env_test_mode() {
    if (g_test_mode) return true;
    char buf[32];
    DWORD n = GetEnvironmentVariableA("QUICKAPPSTEST", buf, sizeof(buf));
    return n > 0 && buf[0] == '1';
}

struct Source {
    HANDLE h = INVALID_HANDLE_VALUE;
    uint64_t size = 0;
    uint32_t sector = 512;
    uint32_t phys_sector = 512;
    bool is_image = false;
    bool use_ov = true;
    std::wstring path;
    std::wstring model;
    std::wstring bus;
    int smart_fail = -1;
    uint64_t geom_size = 0;

    ~Source() {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }

    bool open(const std::wstring& src) {
        path = src;
        std::wstring open_path = src;
        if (src.size() == 1 && src[0] >= L'0' && src[0] <= L'9') {
            open_path = L"\\\\.\\PhysicalDrive" + src;
        } else if (src.size() <= 2 && iswdigit(src[0])) {
            open_path = L"\\\\.\\PhysicalDrive" + src;
        }
        bool is_vol = open_path.find(L"Volume{") != std::wstring::npos ||
                      (open_path.size() >= 6 && open_path.rfind(L"\\\\.\\", 0) == 0 &&
                       open_path[5] == L':') ||
                      (open_path.size() == 2 && open_path[1] == L':');
        is_image = open_path.rfind(L"\\\\.\\PhysicalDrive", 0) != 0 && !is_vol;

        if (env_test_mode() && !is_image) return false;

        DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
        DWORD flags = FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED;
        use_ov = true;
        if (is_vol) {
            flags = FILE_ATTRIBUTE_NORMAL;
            use_ov = false;
            if (open_path.size() == 2 && open_path[1] == L':') open_path = L"\\\\.\\" + open_path;
        } else if (is_image) {
            flags = FILE_FLAG_OVERLAPPED;
        }
        h = CreateFileW(open_path.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, flags, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            flags = (is_image || is_vol) ? FILE_ATTRIBUTE_NORMAL : FILE_FLAG_NO_BUFFERING;
            use_ov = false;
            h = CreateFileW(open_path.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, flags, nullptr);
        }
        if (h == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER li{};
        if (GetFileSizeEx(h, &li)) size = (uint64_t)li.QuadPart;

        DISK_GEOMETRY_EX geom{};
        DWORD br = 0;
        if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &geom, sizeof(geom), &br, nullptr)) {
            sector = geom.Geometry.BytesPerSector ? geom.Geometry.BytesPerSector : 512;
            geom_size = (uint64_t)geom.DiskSize.QuadPart;
            if (!size) size = geom_size;
        }
        GET_LENGTH_INFORMATION glen{};
        if (DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &glen, sizeof(glen), &br, nullptr)) {
            size = (uint64_t)glen.Length.QuadPart;
        }
        STORAGE_PROPERTY_QUERY q{};
        q.PropertyId = StorageAccessAlignmentProperty;
        q.QueryType = PropertyStandardQuery;
        STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR align{};
        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), &align, sizeof(align), &br, nullptr)) {
            if (align.BytesPerLogicalSector) sector = align.BytesPerLogicalSector;
            if (align.BytesPerPhysicalSector) phys_sector = align.BytesPerPhysicalSector;
        }
        if (!sector) sector = 512;
        if (!phys_sector) phys_sector = sector;

        q.PropertyId = StorageDeviceProperty;
        std::vector<uint8_t> buf(1024);
        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), buf.data(), (DWORD)buf.size(), &br, nullptr)) {
            auto* desc = (STORAGE_DEVICE_DESCRIPTOR*)buf.data();
            static const wchar_t* buses[] = {
                L"Unknown", L"SCSI", L"ATAPI", L"ATA", L"1394", L"SSA", L"Fibre", L"USB",
                L"RAID", L"iSCSI", L"SAS", L"SATA", L"SD", L"MMC", L"Virtual", L"FileBackedVirtual",
                L"Spaces", L"NVMe"
            };
            unsigned b = desc->BusType;
            bus = b < 18 ? buses[b] : L"Other";
            auto take = [&](DWORD off) {
                if (!off || off >= br) return std::wstring();
                std::string s((char*)buf.data() + off);
                while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
                return utf8_to_wide(s.data(), s.size());
            };
            std::wstring ven = take(desc->VendorIdOffset);
            std::wstring prod = take(desc->ProductIdOffset);
            model = ven;
            if (!prod.empty()) {
                if (!model.empty()) model += L" ";
                model += prod;
            }
        }
        STORAGE_PREDICT_FAILURE pred{};
        if (DeviceIoControl(h, IOCTL_STORAGE_PREDICT_FAILURE, nullptr, 0, &pred, sizeof(pred), &br, nullptr)) {
            smart_fail = pred.PredictFailure ? 1 : 0;
        }
        return true;
    }

    bool read(uint64_t off, void* buf, uint32_t len) {
        if (h == INVALID_HANDLE_VALUE) return false;
        if (size && off + len > size) {
            if (off >= size) return false;
            len = (uint32_t)(size - off);
            memset(buf, 0, len);
        }
        DWORD br = 0;
        BOOL ok = FALSE;
        if (!use_ov) {
            LARGE_INTEGER li;
            li.QuadPart = (LONGLONG)off;
            if (SetFilePointerEx(h, li, nullptr, FILE_BEGIN))
                ok = ReadFile(h, buf, len, &br, nullptr);
        } else {
            OVERLAPPED ov{};
            ov.Offset = (DWORD)(off & 0xFFFFFFFFu);
            ov.OffsetHigh = (DWORD)(off >> 32);
            ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            ok = ReadFile(h, buf, len, &br, &ov);
            if (!ok && GetLastError() == ERROR_IO_PENDING) {
                ok = GetOverlappedResult(h, &ov, &br, TRUE);
            }
            CloseHandle(ov.hEvent);
        }
        if (!ok || br < len) {
            if (len > sector && (len % sector) == 0) {
                uint8_t* p = (uint8_t*)buf;
                uint32_t left = len;
                uint64_t at = off;
                while (left) {
                    uint32_t chunk = sector;
                    if (!read(at, p, chunk)) {
                        memset(p, 0, chunk);
                    }
                    p += chunk;
                    at += chunk;
                    left -= chunk;
                }
                return true;
            }
            return false;
        }
        return true;
    }

    uint32_t aligned_len(uint32_t n) const {
        uint32_t s = sector ? sector : 512;
        return (n + s - 1) / s * s;
    }
};

#ifndef SMART_RCV_DRIVE_DATA
#define SMART_RCV_DRIVE_DATA CTL_CODE(IOCTL_DISK_BASE, 0x0022, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#endif
#ifndef SMART_GET_VERSION
#define SMART_GET_VERSION CTL_CODE(IOCTL_DISK_BASE, 0x0020, METHOD_BUFFERED, FILE_READ_ACCESS)
#endif

#pragma pack(push, 1)
struct SmartIdRegs {
    BYTE bFeaturesReg;
    BYTE bSectorCountReg;
    BYTE bSectorNumberReg;
    BYTE bCylLowReg;
    BYTE bCylHighReg;
    BYTE bDriveHeadReg;
    BYTE bCommandReg;
    BYTE bReserved;
};
struct SmartCmdIn {
    DWORD cBufferSize;
    SmartIdRegs irDriveRegs;
    BYTE bDriveNumber;
    BYTE bReserved[3];
    DWORD dwReserved[4];
    BYTE bBuffer[512];
};
struct SmartDriverStatus {
    BYTE bDriverError;
    BYTE bIDEStatus;
    BYTE bReserved[2];
    DWORD dwReserved[2];
};
struct SmartCmdOut {
    DWORD cBufferSize;
    SmartDriverStatus DriverStatus;
    BYTE bBuffer[512];
};
#pragma pack(pop)

uint64_t raw48(const uint8_t* r) {
    uint64_t v = 0;
    for (int i = 0; i < 6; ++i) v |= (uint64_t)r[i] << (8 * i);
    return v;
}

int drive_num_from_path(const std::wstring& path) {
    const wchar_t* key = L"PhysicalDrive";
    size_t p = path.find(key);
    if (p == std::wstring::npos) return 0;
    return _wtoi(path.c_str() + p + 13);
}

HANDLE open_smart_handle(const std::wstring& path) {
    return CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
}

std::wstring letters_for_physical(int n) {
    std::wstring out;
    wchar_t buf[512];
    if (!GetLogicalDriveStringsW(511, buf)) return out;
    for (wchar_t* p = buf; *p; p += wcslen(p) + 1) {
        wchar_t vol[8] = {L'\\', L'\\', L'.', L'\\', p[0], L':', 0};
        HANDLE h = CreateFileW(vol, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        STORAGE_DEVICE_NUMBER num{};
        DWORD br = 0;
        if (DeviceIoControl(h, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &num, sizeof(num), &br, nullptr) &&
            (int)num.DeviceNumber == n) {
            if (!out.empty()) out += L" ";
            out += p[0];
            out += L":";
        }
        CloseHandle(h);
    }
    return out;
}

std::wstring ata_swap_str(const uint8_t* p, int n) {
    std::string s;
    s.reserve((size_t)n);
    for (int i = 0; i + 1 < n; i += 2) {
        if (p[i + 1]) s += (char)p[i + 1];
        if (p[i]) s += (char)p[i];
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return utf8_to_wide(s.c_str() + i, s.size() - i);
}

bool smart_recv(HANDLE h, int drive, BYTE feature, BYTE command, BYTE cyl_lo, BYTE cyl_hi, BYTE* out512) {
    SmartCmdIn in{};
    SmartCmdOut out{};
    in.cBufferSize = 512;
    in.bDriveNumber = (BYTE)drive;
    in.irDriveRegs.bFeaturesReg = feature;
    in.irDriveRegs.bSectorCountReg = 1;
    in.irDriveRegs.bSectorNumberReg = 1;
    in.irDriveRegs.bCylLowReg = cyl_lo;
    in.irDriveRegs.bCylHighReg = cyl_hi;
    in.irDriveRegs.bDriveHeadReg = (BYTE)(0xA0 | ((drive & 1) << 4));
    in.irDriveRegs.bCommandReg = command;
    DWORD br = 0;
    if (!DeviceIoControl(h, SMART_RCV_DRIVE_DATA, &in, sizeof(in), &out, sizeof(out), &br, nullptr)) {
        return false;
    }
    memcpy(out512, out.bBuffer, 512);
    return true;
}

bool smart_ata(HANDLE h, int drive, std::wstring& json) {
    uint8_t attrs[512]{}, thres[512]{}, ident[512]{};
    if (!smart_recv(h, drive, 0xD0, 0xB0, 0x4F, 0xC2, attrs)) return false;
    smart_recv(h, drive, 0xD1, 0xB0, 0x4F, 0xC2, thres);
    smart_recv(h, drive, 0, 0xEC, 0, 0, ident);
    uint8_t thr_by_id[256]{};
    for (int i = 0; i < 30; ++i) {
        uint8_t id = thres[2 + i * 12];
        if (id) thr_by_id[id] = thres[2 + i * 12 + 1];
    }
    int temp = -1, failing = 0, caution = 0;
    uint64_t realloc = 0, pending = 0, uncorr = 0, poh = 0, cycles = 0;
    std::wstring attr_json = L"[";
    bool first = true;
    for (int i = 0; i < 30; ++i) {
        const uint8_t* a = attrs + 2 + i * 12;
        uint8_t id = a[0];
        if (!id) continue;
        uint8_t flags = a[1];
        uint8_t cur = a[3];
        uint8_t worst = a[4];
        uint8_t thr = thr_by_id[id];
        uint64_t raw = raw48(a + 5);
        int st = 0;
        if ((flags & 1) && thr > 0 && cur > 0 && cur <= thr) {
            failing = 1;
            st = 2;
        }
        if (id == 5) realloc = raw;
        if (id == 9) poh = raw;
        if (id == 12) cycles = raw;
        if (id == 194 || id == 190) {
            int t = (int)(raw & 0xFF);
            if (t > 0 && t < 125) temp = t;
        }
        if (id == 197) pending = raw;
        if (id == 198) uncorr = raw;
        if ((id == 5 || id == 197 || id == 198) && raw > 0 && st == 0) {
            caution = 1;
            st = 1;
        }
        if (!first) attr_json += L",";
        first = false;
        attr_json += L"{\"id\":" + std::to_wstring(id) +
                     L",\"current\":" + std::to_wstring(cur) +
                     L",\"worst\":" + std::to_wstring(worst) +
                     L",\"threshold\":" + std::to_wstring(thr) +
                     L",\"raw\":" + std::to_wstring(raw) +
                     L",\"status\":" + std::to_wstring(st) + L"}";
    }
    attr_json += L"]";
    if (temp >= 60) caution = 1;
    const wchar_t* health = failing ? L"bad" : (caution ? L"caution" : L"good");
    std::wstring serial = ata_swap_str(ident + 20, 20);
    std::wstring firmware = ata_swap_str(ident + 46, 8);
    std::wstring model = ata_swap_str(ident + 54, 40);
    uint16_t rot = (uint16_t)(ident[434] | (ident[435] << 8));
    json = L"{\"available\":true,\"protocol\":\"ATA\",\"health\":\"";
    json += health;
    json += L"\",\"failing\":" + std::to_wstring(failing) +
            L",\"temp_c\":" + std::to_wstring(temp) +
            L",\"reallocated\":" + std::to_wstring(realloc) +
            L",\"pending\":" + std::to_wstring(pending) +
            L",\"uncorrectable\":" + std::to_wstring(uncorr) +
            L",\"power_on_hours\":" + std::to_wstring(poh) +
            L",\"power_on_count\":" + std::to_wstring(cycles) +
            L",\"serial\":\"" + json_escape(serial) +
            L"\",\"firmware\":\"" + json_escape(firmware) +
            L"\",\"identify_model\":\"" + json_escape(model) +
            L"\",\"rotation\":" + std::to_wstring(rot) +
            L",\"attributes\":" + attr_json + L"}";
    return true;
}

bool smart_nvme(HANDLE h, std::wstring& json) {
    const DWORD extra = 512;
    DWORD qlen = (DWORD)(sizeof(STORAGE_PROPERTY_QUERY) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + extra);
    std::vector<uint8_t> buf(qlen + 64);
    auto* query = (STORAGE_PROPERTY_QUERY*)buf.data();
    query->PropertyId = StorageDeviceProtocolSpecificProperty;
    query->QueryType = PropertyStandardQuery;
    auto* proto = (STORAGE_PROTOCOL_SPECIFIC_DATA*)query->AdditionalParameters;
    proto->ProtocolType = ProtocolTypeNvme;
    proto->DataType = NVMeDataTypeLogPage;
    proto->ProtocolDataRequestValue = 2;
    proto->ProtocolDataRequestSubValue = 0xFFFFFFFF;
    proto->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    proto->ProtocolDataLength = extra;
    DWORD br = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, buf.data(), qlen, buf.data(), (DWORD)buf.size(), &br, nullptr)) {
        return false;
    }
    auto* desc = (STORAGE_PROTOCOL_DATA_DESCRIPTOR*)buf.data();
    auto* outp = (STORAGE_PROTOCOL_SPECIFIC_DATA*)&desc->ProtocolSpecificData;
    if (outp->ProtocolDataOffset + 512 > br && outp->ProtocolDataOffset + 32 > br) return false;
    const uint8_t* log = (const uint8_t*)outp + outp->ProtocolDataOffset;
    int warn = log[0];
    int temp = (int)rd16(log + 1) - 273;
    int spare = log[3];
    int spare_th = log[4];
    int used = log[5];
    uint64_t poh = rd64(log + 128);
    uint64_t media_err = rd64(log + 160);
    int failing = (warn || (spare_th && spare <= spare_th)) ? 1 : 0;
    const wchar_t* health = failing ? L"bad" : ((used >= 90 || temp >= 70) ? L"caution" : L"good");
    json = L"{\"available\":true,\"protocol\":\"NVMe\",\"health\":\"";
    json += health;
    json += L"\",\"failing\":" + std::to_wstring(failing) +
            L",\"critical_warning\":" + std::to_wstring(warn) +
            L",\"temp_c\":" + std::to_wstring(temp) +
            L",\"available_spare\":" + std::to_wstring(spare) +
            L",\"spare_threshold\":" + std::to_wstring(spare_th) +
            L",\"percent_used\":" + std::to_wstring(used) +
            L",\"power_on_hours\":" + std::to_wstring(poh) +
            L",\"media_errors\":" + std::to_wstring(media_err) + L"}";
    return true;
}

std::wstring collect_smart_json(const Source& src) {
    if (src.is_image) return L"{\"available\":false,\"reason\":\"image\"}";
    HANDLE hs = open_smart_handle(src.path);
    if (hs == INVALID_HANDLE_VALUE) {
        return L"{\"available\":false,\"reason\":\"open_denied\",\"predict_failure\":" +
               std::to_wstring(src.smart_fail) + L"}";
    }
    std::wstring js;
    bool ok = false;
    if (_wcsicmp(src.bus.c_str(), L"NVMe") == 0) ok = smart_nvme(hs, js);
    if (!ok) ok = smart_ata(hs, drive_num_from_path(src.path), js);
    if (!ok && _wcsicmp(src.bus.c_str(), L"NVMe") != 0) ok = smart_nvme(hs, js);
    CloseHandle(hs);
    if (!ok) {
        return L"{\"available\":false,\"reason\":\"ioctl_failed\",\"predict_failure\":" +
               std::to_wstring(src.smart_fail) + L",\"bus\":\"" + json_escape(src.bus) + L"\"}";
    }
    if (js.size() > 1 && js.back() == L'}') {
        js.pop_back();
        js += L",\"predict_failure\":" + std::to_wstring(src.smart_fail) + L"}";
    }
    return js;
}

struct GptPart {
    uint64_t first = 0, last = 0;
    std::wstring name;
    std::wstring type;
    std::wstring role;
    int index = 0;
};

std::wstring gpt_role(const std::wstring& type) {
    if (_wcsicmp(type.c_str(), L"C12A7328-F81F-11D2-BA4B-00A0C93EC93B") == 0) return L"EFI";
    if (_wcsicmp(type.c_str(), L"E3C9E316-0B5C-4DB8-817D-F92DF00215AE") == 0) return L"MSR";
    if (_wcsicmp(type.c_str(), L"EBD0A0A2-B9E5-4433-87C0-68B6B72699C7") == 0) return L"Basic";
    if (_wcsicmp(type.c_str(), L"DE94BBA4-06D1-4D40-A16A-BFD50179D6AC") == 0) return L"Recovery";
    return L"";
}

std::wstring guid_str(const uint8_t* g) {
    wchar_t b[64];
    swprintf(b, 64, L"%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
             rd32(g), rd16(g + 4), rd16(g + 6),
             g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    return b;
}

bool parse_gpt_header(const uint8_t* sec, uint32_t sector, uint64_t* alternate, uint64_t* part_lba,
                      uint32_t* part_count, uint32_t* part_size, uint64_t* last_usable, uint32_t* crc_ok) {
    if (memcmp(sec, "EFI PART", 8) != 0) return false;
    uint32_t hdr_size = rd32(sec + 12);
    if (hdr_size < 92 || hdr_size > sector) return false;
    std::vector<uint8_t> tmp(sec, sec + hdr_size);
    memset(tmp.data() + 16, 0, 4);
    uint32_t calc = crc32_ieee(tmp.data(), hdr_size);
    uint32_t stored = rd32(sec + 16);
    *crc_ok = calc == stored ? 1 : 0;
    *alternate = rd64(sec + 32);
    *part_lba = rd64(sec + 72);
    *part_count = rd32(sec + 80);
    *part_size = rd32(sec + 84);
    *last_usable = rd64(sec + 48);
    return true;
}

void read_gpt_parts(Source& src, uint64_t part_lba, uint32_t count, uint32_t esize, std::vector<GptPart>& out) {
    if (!count || !esize || count > 128) return;
    uint32_t bytes = count * esize;
    uint32_t alen = src.aligned_len(bytes);
    std::vector<uint8_t> buf(alen);
    if (!src.read(part_lba * src.sector, buf.data(), alen)) return;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = buf.data() + i * esize;
        uint64_t first = rd64(e + 32);
        uint64_t last = rd64(e + 40);
        bool empty = true;
        for (int k = 0; k < 16; ++k) if (e[k]) empty = false;
        if (empty || last < first) continue;
        GptPart p;
        p.first = first;
        p.last = last;
        p.index = (int)out.size();
        p.type = guid_str(e);
        p.role = gpt_role(p.type);
        std::wstring nm;
        const uint16_t* w = (const uint16_t*)(e + 56);
        for (int k = 0; k < 36 && w[k]; ++k) nm += (wchar_t)w[k];
        p.name = nm;
        out.push_back(p);
    }
}

struct NtfsFile {
    uint64_t rec = 0;
    uint64_t parent = 0;
    std::wstring name;
    bool in_use = false;
    bool is_dir = false;
    bool deleted = false;
    uint64_t size = 0;
    std::vector<uint8_t> resident;
    bool data_resident = false;
    std::vector<std::pair<uint64_t, uint64_t>> runs; // lcn, clusters (lcn=~0 sparse)
    uint64_t cluster_size = 0;
};

struct DataRun {
    uint64_t lcn = 0;
    uint64_t clusters = 0;
    bool sparse = false;
};

bool parse_runs(const uint8_t* p, const uint8_t* end, std::vector<DataRun>& runs) {
    int64_t lcn = 0;
    while (p < end && *p != 0) {
        int nlen = *p & 0x0F;
        int noff = (*p >> 4) & 0x0F;
        p++;
        if (p + nlen + noff > end) return false;
        uint64_t length = 0;
        for (int i = 0; i < nlen; ++i) length |= (uint64_t)(*p++) << (8 * i);
        int64_t offset = 0;
        if (noff) {
            for (int i = 0; i < noff; ++i) offset |= (int64_t)(*p++) << (8 * i);
            if (p[-1] & 0x80) {
                for (int i = noff; i < 8; ++i) offset |= (int64_t)0xFF << (8 * i);
            }
            lcn += offset;
            runs.push_back({(uint64_t)lcn, length, false});
        } else {
            runs.push_back({0, length, true});
        }
    }
    return true;
}

void apply_fixup(uint8_t* rec, uint32_t rec_size) {
    if (rec_size < 8) return;
    uint16_t usa_ofs = rd16(rec + 4);
    uint16_t usa_count = rd16(rec + 6);
    if (!usa_ofs || usa_ofs + usa_count * 2 > rec_size) return;
    for (uint16_t i = 1; i < usa_count; ++i) {
        uint32_t off = (uint32_t)i * 512 - 2;
        if (off + 1 >= rec_size) break;
        rec[off] = rec[usa_ofs + i * 2];
        rec[off + 1] = rec[usa_ofs + i * 2 + 1];
    }
}

volatile int* g_ntfs_stop = nullptr;
void (*g_ntfs_progress)(unsigned long long, unsigned long long, const wchar_t*, void*) = nullptr;
void* g_ntfs_progress_user = nullptr;

struct NtfsVol {
    Source* src = nullptr;
    uint64_t part_off = 0;
    uint32_t bytes_per_sec = 512;
    uint32_t sec_per_cluster = 8;
    uint32_t rec_size = 1024;
    uint64_t cluster_size = 4096;
    uint64_t mft_cluster = 0;
    uint64_t total_sectors = 0;
    bool ok = false;
    std::map<uint64_t, NtfsFile> files;
    std::vector<uint8_t> bitmap;

    bool probe(Source& s, uint64_t off) {
        src = &s;
        part_off = off;
        uint32_t alen = s.aligned_len(512);
        std::vector<uint8_t> boot(alen);
        if (!s.read(off, boot.data(), alen)) return false;
        if (memcmp(boot.data() + 3, "NTFS    ", 8) != 0) return false;
        bytes_per_sec = rd16(boot.data() + 11);
        sec_per_cluster = boot[13];
        if (!bytes_per_sec || !sec_per_cluster) return false;
        cluster_size = (uint64_t)bytes_per_sec * sec_per_cluster;
        total_sectors = rd64(boot.data() + 0x28);
        mft_cluster = rd64(boot.data() + 0x30);
        int8_t cpr = (int8_t)boot[0x40];
        if (cpr < 0) rec_size = 1u << (-cpr);
        else rec_size = (uint32_t)(cpr * cluster_size);
        if (rec_size < 256 || rec_size > 4096) rec_size = 1024;
        ok = true;
        return true;
    }

    bool read_abs(uint64_t off, void* buf, uint32_t len) {
        return src->read(part_off + off, buf, len);
    }

    bool read_clusters(uint64_t lcn, uint64_t ncl, std::vector<uint8_t>& out) {
        uint64_t bytes = ncl * cluster_size;
        if (bytes > 64ull * 1024 * 1024) return false;
        uint32_t alen = src->aligned_len((uint32_t)bytes);
        out.assign(alen, 0);
        return read_abs(lcn * cluster_size, out.data(), alen);
    }

    bool load_mft_record(uint64_t rec, std::vector<uint8_t>& out, const std::vector<DataRun>* mft_runs) {
        out.assign(rec_size, 0);
        uint64_t rec_off = rec * rec_size;
        if (mft_runs && !mft_runs->empty()) {
            uint64_t vcn = rec_off / cluster_size;
            uint64_t inner = rec_off % cluster_size;
            uint64_t cur = 0;
            for (const auto& r : *mft_runs) {
                if (vcn >= cur && vcn < cur + r.clusters) {
                    if (r.sparse) return false;
                    uint64_t lcn = r.lcn + (vcn - cur);
                    std::vector<uint8_t> cl;
                    if (!read_clusters(lcn, 1, cl) || inner + rec_size > cl.size()) return false;
                    memcpy(out.data(), cl.data() + inner, rec_size);
                    apply_fixup(out.data(), rec_size);
                    return memcmp(out.data(), "FILE", 4) == 0;
                }
                cur += r.clusters;
            }
            return false;
        }
        uint64_t abs = mft_cluster * cluster_size + rec_off;
        uint32_t alen = src->aligned_len(rec_size);
        std::vector<uint8_t> buf(alen);
        if (!read_abs(abs, buf.data(), alen)) return false;
        memcpy(out.data(), buf.data(), rec_size);
        apply_fixup(out.data(), rec_size);
        return memcmp(out.data(), "FILE", 4) == 0;
    }

    bool parse_record(uint64_t recno, const uint8_t* rec, NtfsFile& f, std::vector<DataRun>* out_mft_runs) {
        f.rec = recno;
        uint16_t flags = rd16(rec + 0x16);
        f.in_use = (flags & 1) != 0;
        f.is_dir = (flags & 2) != 0;
        f.deleted = !f.in_use;
        uint16_t attr_off = rd16(rec + 0x14);
        const uint8_t* p = rec + attr_off;
        const uint8_t* end = rec + rec_size;
        while (p + 8 < end) {
            uint32_t type = rd32(p);
            uint32_t len = rd32(p + 4);
            if (type == 0xFFFFFFFFu || len < 16 || p + len > end) break;
            uint8_t nonres = p[8];
            uint8_t nlen = p[9];
            uint16_t noff = rd16(p + 10);
            (void)nlen;
            (void)noff;
            if (type == 0x30 && nonres == 0) {
                uint16_t voff = rd16(p + 20);
                const uint8_t* v = p + voff;
                f.parent = rd64(v) & 0x0000FFFFFFFFFFFFull;
                uint8_t nsz = v[64];
                uint8_t nsp = v[65];
                if (nsp == 1 || nsp == 3 || f.name.empty()) {
                    std::wstring nm;
                    const uint16_t* w = (const uint16_t*)(v + 66);
                    for (uint8_t i = 0; i < nsz; ++i) nm += (wchar_t)w[i];
                    if (!nm.empty() && (f.name.empty() || nsp == 1 || nsp == 3)) f.name = nm;
                }
            } else if (type == 0x80) {
                if (nonres == 0) {
                    uint16_t voff = rd16(p + 20);
                    uint32_t vlen = rd32(p + 16);
                    f.data_resident = true;
                    f.size = vlen;
                    f.resident.assign(p + voff, p + voff + vlen);
                } else {
                    uint16_t run_off = rd16(p + 32);
                    f.size = rd64(p + 48);
                    f.data_resident = false;
                    std::vector<DataRun> runs;
                    parse_runs(p + run_off, p + len, runs);
                    if (out_mft_runs && recno == 0) *out_mft_runs = runs;
                    f.cluster_size = cluster_size;
                    for (auto& r : runs) {
                        f.runs.push_back({r.sparse ? ~0ull : r.lcn, r.clusters});
                    }
                }
            } else if (type == 0xB0 && recno == 6 && nonres == 1) {
                uint16_t run_off = rd16(p + 32);
                std::vector<DataRun> runs;
                parse_runs(p + run_off, p + len, runs);
                for (auto& r : runs) {
                    if (r.sparse) continue;
                    std::vector<uint8_t> cl;
                    if (read_clusters(r.lcn, r.clusters, cl)) {
                        bitmap.insert(bitmap.end(), cl.begin(), cl.begin() + (size_t)(r.clusters * cluster_size));
                    }
                }
            }
            p += len;
        }
        return !f.name.empty() || recno < 16;
    }

    bool load() {
        std::vector<uint8_t> rec0;
        if (!load_mft_record(0, rec0, nullptr)) {
            uint64_t backup = 0;
            if (total_sectors > 1) backup = (total_sectors - 1) * bytes_per_sec;
            std::vector<uint8_t> boot2(src->aligned_len(512));
            if (src->read(part_off + backup, boot2.data(), (uint32_t)boot2.size()) &&
                memcmp(boot2.data() + 3, "NTFS    ", 8) == 0) {
                mft_cluster = rd64(boot2.data() + 0x30);
            }
            if (!load_mft_record(0, rec0, nullptr)) return false;
        }
        std::vector<DataRun> mft_runs;
        NtfsFile mf;
        parse_record(0, rec0.data(), mf, &mft_runs);
        if (mft_runs.empty()) {
            mft_runs.push_back({mft_cluster, 16, false});
        }
        uint64_t recno = 0;
        const uint64_t max_rec = 200000;
        uint64_t total_rec_est = 0;
        for (const auto& r : mft_runs) {
            if (total_rec_est >= max_rec) break;
            total_rec_est += r.clusters * cluster_size / rec_size;
        }
        if (total_rec_est > max_rec) total_rec_est = max_rec;
        if (!total_rec_est) total_rec_est = 1;
        for (const auto& r : mft_runs) {
            if (recno >= max_rec) break;
            if (g_ntfs_stop && *g_ntfs_stop) return !files.empty();
            uint64_t run_bytes = r.clusters * cluster_size;
            uint64_t nrec_run = run_bytes / rec_size;
            if (r.sparse) {
                recno += nrec_run;
                continue;
            }
            uint64_t done = 0;
            while (done < run_bytes && recno < max_rec) {
                if (g_ntfs_stop && *g_ntfs_stop) return !files.empty();
                uint64_t chunk = run_bytes - done;
                if (chunk > 64ull * 1024) chunk = 64ull * 1024;
                chunk = (chunk / rec_size) * rec_size;
                if (!chunk) break;
                uint64_t lcn = r.lcn + done / cluster_size;
                uint64_t ncl = (chunk + cluster_size - 1) / cluster_size;
                std::vector<uint8_t> buf;
                if (!read_clusters(lcn, ncl, buf)) break;
                for (uint64_t off = 0; off + rec_size <= chunk && recno < max_rec; off += rec_size, recno++) {
                    uint8_t* rec = buf.data() + (size_t)off;
                    apply_fixup(rec, rec_size);
                    NtfsFile f;
                    if (!parse_record(recno, rec, f, nullptr)) continue;
                    if (f.name.empty() && recno >= 16) continue;
                    files[recno] = std::move(f);
                }
                done += chunk;
                if (g_ntfs_progress) g_ntfs_progress(recno, total_rec_est, L"tree", g_ntfs_progress_user);
            }
        }
        return !files.empty();
    }

    std::wstring path_of(uint64_t rec) {
        std::vector<std::wstring> parts;
        uint64_t cur = rec;
        for (int g = 0; g < 64; ++g) {
            auto it = files.find(cur);
            if (it == files.end()) break;
            if (cur == 5) break;
            if (!it->second.name.empty()) parts.push_back(it->second.name);
            uint64_t par = it->second.parent;
            if (par == cur) break;
            cur = par;
        }
        std::wstring p = L"/";
        for (int i = (int)parts.size() - 1; i >= 0; --i) {
            if (p.size() > 1) p += L"/";
            p += parts[i];
        }
        return p;
    }

    bool read_file_content(const NtfsFile& f, std::vector<uint8_t>& out) {
        if (f.data_resident) {
            out = f.resident;
            return true;
        }
        if (f.size > 512ull * 1024 * 1024) return false;
        out.assign((size_t)f.size, 0);
        uint64_t written = 0;
        for (auto& run : f.runs) {
            uint64_t bytes = run.second * cluster_size;
            if (run.first == ~0ull) {
                written += bytes;
                if (written >= f.size) break;
                continue;
            }
            std::vector<uint8_t> cl;
            if (!read_clusters(run.first, run.second, cl)) return false;
            uint64_t take = bytes;
            if (written + take > f.size) take = f.size - written;
            memcpy(out.data() + written, cl.data(), (size_t)take);
            written += take;
            if (written >= f.size) break;
        }
        return true;
    }
};

void dump_file_json(NtfsVol& vol, uint64_t rec, std::wstring& o, int depth, int* nodes) {
    if (depth > 2) return;
    if (nodes && *nodes > 400) return;
    auto it = vol.files.find(rec);
    if (it == vol.files.end()) return;
    if (nodes) (*nodes)++;
    const NtfsFile& f = it->second;
    o += L"{\"name\":\"" + json_escape(f.name.empty() ? (L"$" + std::to_wstring(rec)) : f.name) + L"\",";
    o += L"\"path\":\"" + json_escape(vol.path_of(rec)) + L"\",";
    o += L"\"rec\":" + std::to_wstring(rec) + L",";
    o += L"\"is_dir\":" + std::wstring(f.is_dir ? L"true" : L"false") + L",";
    o += L"\"in_use\":" + std::wstring(f.in_use ? L"true" : L"false") + L",";
    o += L"\"deleted\":" + std::wstring(f.deleted ? L"true" : L"false") + L",";
    o += L"\"size\":" + std::to_wstring(f.size) + L",";
    o += L"\"has_children\":" + std::wstring(f.is_dir ? L"true" : L"false") + L",\"children\":[";
    bool first = true;
    if (f.is_dir && depth == 0) {
        int n = 0;
        for (auto& kv : vol.files) {
            if (kv.second.parent != rec) continue;
            if (kv.first == rec) continue;
            if (kv.first < 16 && kv.first != 5) continue;
            if (!first) o += L",";
            first = false;
            dump_file_json(vol, kv.first, o, 1, nodes);
            if (++n >= 400) break;
        }
    }
    o += L"]}";
}

std::wstring fs_magic(Source& src, uint64_t lba) {
    uint32_t alen = src.aligned_len(512);
    std::vector<uint8_t> b(alen);
    if (!src.read(lba * src.sector, b.data(), alen)) return L"";
    if (memcmp(b.data() + 3, "NTFS    ", 8) == 0) return L"NTFS";
    if (memcmp(b.data() + 3, "-FVE-FS-", 8) == 0) return L"BitLocker";
    if (memcmp(b.data() + 3, "EXFAT   ", 8) == 0) return L"exFAT";
    if (b[0x52] == 'F' && b[0x53] == 'A' && b[0x54] == 'T') return L"FAT32";
    if (memcmp(b.data(), "EFI PART", 8) == 0) return L"GPT";
    return L"";
}

std::wstring diagnose_source(const wchar_t* source, int deep) {
    Source src;
    if (!src.open(source)) {
        return L"{\"ok\":false,\"error\":\"open_failed\"}";
    }
    std::wstring o = L"{\"ok\":true,\"readonly\":true,\"path\":\"" + json_escape(src.path) + L"\",";
    o += L"\"size_bytes\":" + std::to_wstring(src.size) + L",";
    o += L"\"geometry_size_bytes\":" + std::to_wstring(src.geom_size) + L",";
    o += L"\"sector_size\":" + std::to_wstring(src.sector) + L",";
    o += L"\"phys_sector_size\":" + std::to_wstring(src.phys_sector) + L",";
    o += L"\"model\":\"" + json_escape(src.model) + L"\",";
    o += L"\"bus\":\"" + json_escape(src.bus) + L"\",";
    if (!src.is_image) {
        o += L"\"letters\":\"" + json_escape(letters_for_physical(drive_num_from_path(src.path))) + L"\",";
    } else {
        o += L"\"letters\":\"\",";
    }
    o += L"\"smart_failing\":" + std::to_wstring(src.smart_fail) + L",";
    o += L"\"smart\":" + collect_smart_json(src) + L",";

    uint32_t alen = src.aligned_len(src.sector * 2);
    std::vector<uint8_t> head(alen);
    src.read(0, head.data(), alen);
    uint64_t alt = 0, part_lba = 0, last_usable = 0;
    uint32_t pcount = 0, psize = 0, crc_ok = 0;
    bool gpt = parse_gpt_header(head.data() + src.sector, src.sector, &alt, &part_lba, &pcount, &psize, &last_usable, &crc_ok);
    o += L"\"gpt_primary\":" + std::wstring(gpt ? L"true" : L"false") + L",";
    o += L"\"gpt_primary_crc_ok\":" + std::to_wstring(crc_ok) + L",";
    o += L"\"gpt_last_usable_lba\":" + std::to_wstring(last_usable) + L",";
    o += L"\"gpt_alternate_lba\":" + std::to_wstring(alt) + L",";
    uint64_t disk_lbas = src.sector ? src.size / src.sector : 0;
    o += L"\"disk_lbas\":" + std::to_wstring(disk_lbas) + L",";
    int mismatch = 0;
    if (gpt && last_usable + 34 != disk_lbas && last_usable + 1 != disk_lbas) mismatch = 1;
    if (gpt && src.geom_size && src.size && src.geom_size != src.size) mismatch = 1;
    o += L"\"size_mismatch\":" + std::wstring(mismatch ? L"true" : L"false") + L",";

    bool backup_ok = false;
    uint32_t bcrc = 0;
    if (disk_lbas > 1) {
        std::vector<uint8_t> tail(src.aligned_len(src.sector));
        if (src.read((disk_lbas - 1) * src.sector, tail.data(), (uint32_t)tail.size())) {
            uint64_t a2, pl, lu;
            uint32_t pc, ps;
            backup_ok = parse_gpt_header(tail.data(), src.sector, &a2, &pl, &pc, &ps, &lu, &bcrc);
        }
    }
    o += L"\"gpt_backup\":" + std::wstring(backup_ok ? L"true" : L"false") + L",";
    o += L"\"gpt_backup_crc_ok\":" + std::to_wstring(bcrc) + L",";

    std::vector<GptPart> parts;
    if (gpt) read_gpt_parts(src, part_lba, pcount, psize, parts);
    if (parts.empty() && head[510] == 0x55 && head[511] == 0xAA) {
        for (int i = 0; i < 4; ++i) {
            const uint8_t* e = head.data() + 446 + i * 16;
            uint32_t lba = rd32(e + 8);
            uint32_t num = rd32(e + 12);
            if (!num) continue;
            GptPart p;
            p.first = lba;
            p.last = (uint64_t)lba + num - 1;
            p.index = (int)parts.size();
            p.name = L"MBR";
            parts.push_back(p);
        }
    }

    o += L"\"partitions\":[";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) o += L",";
        std::wstring fs = fs_magic(src, parts[i].first);
        o += L"{\"index\":" + std::to_wstring(parts[i].index);
        o += L",\"first_lba\":" + std::to_wstring(parts[i].first);
        o += L",\"last_lba\":" + std::to_wstring(parts[i].last);
        o += L",\"name\":\"" + json_escape(parts[i].name) + L"\"";
        o += L",\"role\":\"" + json_escape(parts[i].role) + L"\"";
        o += L",\"fs\":\"" + json_escape(fs) + L"\"";
        if (fs == L"BitLocker" && !src.is_image) {
            wchar_t* vj = nullptr;
            qdr_find_volume_json(src.path.c_str(), parts[i].index, &vj);
            if (vj) {
                std::wstring vp;
                json_get_str(vj, L"volume", vp);
                bool unl = json_get_true(vj, L"unlocked");
                o += L",\"volume\":\"" + json_escape(vp) + L"\"";
                o += L",\"unlocked\":" + std::wstring(unl ? L"true" : L"false");
                qdr_free_json(vj);
            }
        }
        o += L"}";
    }
    o += L"]";

    if (deep) {
        o += L",\"lost_candidates\":[";
        bool first = true;
        uint64_t step = src.sector;
        uint32_t chunk = src.aligned_len(1024 * 1024);
        std::vector<uint8_t> buf(chunk);
        uint64_t scanned = 0;
        while (scanned < src.size) {
            uint32_t n = chunk;
            if (scanned + n > src.size) n = src.aligned_len(1);
            if (scanned + n > src.size) break;
            if (!src.read(scanned, buf.data(), n)) {
                scanned += n;
                continue;
            }
            for (uint32_t i = 0; i + 8 < n; i += (uint32_t)step) {
                if (memcmp(buf.data() + i, "EFI PART", 8) == 0 ||
                    (i + 11 < n && memcmp(buf.data() + i + 3, "NTFS    ", 8) == 0) ||
                    memcmp(buf.data() + i, "FILE", 4) == 0) {
                    if (!first) o += L",";
                    first = false;
                    const wchar_t* kind = L"FILE";
                    if (memcmp(buf.data() + i, "EFI PART", 8) == 0) kind = L"GPT";
                    else if (i + 11 < n && memcmp(buf.data() + i + 3, "NTFS    ", 8) == 0) kind = L"NTFS";
                    o += L"{\"offset\":" + std::to_wstring(scanned + i) + L",\"kind\":\"" + kind + L"\"}";
                }
            }
            scanned += n;
        }
        o += L"]";
    }
    o += L"}";
    return o;
}

bool try_ntfs_at(Source& src, uint64_t byte_off, NtfsVol& vol) {
    vol = NtfsVol();
    if (!vol.probe(src, byte_off)) return false;
    return vol.load();
}

bool is_volume_source(const std::wstring& p) {
    if (p.find(L"Volume{") != std::wstring::npos) return true;
    if (p.size() >= 6 && p.rfind(L"\\\\.\\", 0) == 0 && p[5] == L':') return true;
    if (p.size() == 2 && p[1] == L':') return true;
    return false;
}

bool open_ntfs_via_volume(Source& live, const wchar_t* source, int part_index, NtfsVol& vol,
                          std::wstring* vol_path, bool* locked) {
    if (locked) *locked = false;
    if (vol_path) vol_path->clear();
    std::wstring vp;
    if (is_volume_source(source)) {
        vp = source;
    } else {
        wchar_t* j = nullptr;
        qdr_find_volume_json(source, part_index, &j);
        if (!j) return false;
        json_get_str(j, L"volume", vp);
        bool unlocked = json_get_true(j, L"unlocked");
        qdr_free_json(j);
        if (vp.empty()) return false;
        if (!unlocked) {
            if (vol_path) *vol_path = vp;
            if (locked) *locked = true;
            return false;
        }
    }
    if (vol_path) *vol_path = vp;
    if (!live.open(vp)) return false;
    return try_ntfs_at(live, 0, vol);
}

bool open_ntfs(Source& src, int part_index, NtfsVol& vol, std::vector<GptPart>& parts) {
    if (is_volume_source(src.path)) {
        parts.clear();
        GptPart whole;
        whole.first = 0;
        whole.last = src.sector ? src.size / src.sector - 1 : 0;
        parts.push_back(whole);
        return try_ntfs_at(src, 0, vol);
    }
    uint32_t alen = src.aligned_len(src.sector * 2);
    std::vector<uint8_t> head(alen);
    src.read(0, head.data(), alen);
    uint64_t alt, pl, lu;
    uint32_t pc, ps, crc;
    if (parse_gpt_header(head.data() + src.sector, src.sector, &alt, &pl, &pc, &ps, &lu, &crc)) {
        read_gpt_parts(src, pl, pc, ps, parts);
    }
    if (parts.empty() && head[510] == 0x55 && head[511] == 0xAA) {
        for (int i = 0; i < 4; ++i) {
            const uint8_t* e = head.data() + 446 + i * 16;
            uint32_t lba = rd32(e + 8);
            uint32_t num = rd32(e + 12);
            if (!num) continue;
            GptPart p;
            p.first = lba;
            p.last = (uint64_t)lba + num - 1;
            p.index = (int)parts.size();
            parts.push_back(p);
        }
    }
    if (parts.empty()) {
        GptPart whole;
        whole.first = 0;
        whole.last = src.sector ? src.size / src.sector - 1 : 0;
        parts.push_back(whole);
    }

    auto load_index = [&](int idx) -> bool {
        if (idx < 0 || idx >= (int)parts.size()) return false;
        return try_ntfs_at(src, parts[idx].first * src.sector, vol);
    };

    if (part_index >= 0) return load_index(part_index);

    int best = -1;
    uint64_t best_sz = 0;
    for (int i = 0; i < (int)parts.size(); ++i) {
        NtfsVol probe;
        uint64_t off = parts[i].first * src.sector;
        if (!probe.probe(src, off)) continue;
        uint64_t sz = (parts[i].last >= parts[i].first)
                          ? (parts[i].last - parts[i].first + 1) * src.sector
                          : 0;
        if (best < 0 || sz > best_sz) {
            best = i;
            best_sz = sz;
        }
    }
    if (best >= 0) return load_index(best);
    return try_ntfs_at(src, 0, vol);
}

int copy_one(NtfsVol& vol, const NtfsFile& f, const std::wstring& dest_base, const std::wstring& rel,
             uint64_t* copied, uint64_t* failed, uint64_t* partial) {
    std::wstring dest = dest_base;
    if (!dest.empty() && dest.back() != L'\\' && dest.back() != L'/') dest += L'\\';
    std::wstring relw = rel;
    for (auto& c : relw) if (c == L'/') c = L'\\';
    if (!relw.empty() && relw[0] == L'\\') relw.erase(0, 1);
    dest += relw;
    if (f.is_dir) {
        CreateDirectoryW(dest.c_str(), nullptr);
        (*copied)++;
        return 1;
    }
    std::vector<uint8_t> data;
    if (!vol.read_file_content(f, data)) {
        (*failed)++;
        return 0;
    }
    size_t slash = dest.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        CreateDirectoryW(dest.substr(0, slash).c_str(), nullptr);
        std::wstring acc;
        for (size_t i = dest_base.size(); i < slash; ++i) {
            if (dest[i] == L'\\' || dest[i] == L'/') CreateDirectoryW(dest.substr(0, i).c_str(), nullptr);
        }
        CreateDirectoryW(dest.substr(0, slash).c_str(), nullptr);
    }
    HANDLE out = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        (*failed)++;
        return 0;
    }
    DWORD wr = 0;
    BOOL ok = WriteFile(out, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(out);
    if (!ok) {
        (*failed)++;
        return 0;
    }
    if (wr < data.size()) (*partial)++;
    else (*copied)++;
    return 1;
}

void copy_tree(NtfsVol& vol, uint64_t rec, const std::wstring& dest, const std::wstring& prefix,
               volatile int* stop, uint64_t* copied, uint64_t* failed, uint64_t* partial) {
    if (stop && *stop) return;
    auto it = vol.files.find(rec);
    if (it == vol.files.end()) return;
    copy_one(vol, it->second, dest, prefix, copied, failed, partial);
    if (!it->second.is_dir) return;
    for (auto& kv : vol.files) {
        if (kv.second.parent != rec || kv.first == rec) continue;
        if (kv.first < 16 && kv.first != 5) continue;
        std::wstring child = prefix;
        if (child.empty() || child == L"/") child = L"/";
        if (child.back() != L'/') child += L'/';
        child += kv.second.name;
        copy_tree(vol, kv.first, dest, child, stop, copied, failed, partial);
    }
}

uint64_t find_path(NtfsVol& vol, const std::wstring& want) {
    std::wstring w = want;
    for (auto& c : w) if (c == L'\\') c = L'/';
    if (w.empty() || w == L"/") return 5;
    for (auto& kv : vol.files) {
        if (vol.path_of(kv.first) == w) return kv.first;
        if (kv.second.name == w) return kv.first;
    }
    return (uint64_t)-1;
}

struct CarveSig {
    const char* type;
    const uint8_t* magic;
    size_t mlen;
    const uint8_t* footer;
    size_t flen;
    uint32_t max_size;
};

static const uint8_t MAG_JPG[] = {0xFF, 0xD8, 0xFF};
static const uint8_t FOO_JPG[] = {0xFF, 0xD9};
static const uint8_t MAG_PNG[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
static const uint8_t FOO_PNG[] = {0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
static const uint8_t MAG_PDF[] = {'%', 'P', 'D', 'F'};
static const uint8_t FOO_PDF[] = {'%', '%', 'E', 'O', 'F'};
static const uint8_t MAG_ZIP[] = {'P', 'K', 0x03, 0x04};
static const uint8_t MAG_GIF[] = {'G', 'I', 'F', '8'};

int run_carve(Source& src, uint64_t start, uint64_t end, const std::wstring& dest, volatile int* stop,
              void (*progress)(unsigned long long, unsigned long long, const wchar_t*, void*), void* user) {
    CreateDirectoryW(dest.c_str(), nullptr);
    int count = 0;
    uint32_t chunk = src.aligned_len(1024 * 1024);
    std::vector<uint8_t> buf(chunk + 16);
    uint64_t off = start;
    int seq = 0;
    while (off < end) {
        if (stop && *stop) break;
        uint32_t n = chunk;
        if (off + n > end) n = (uint32_t)(end - off);
        n = src.aligned_len(n);
        if (!src.read(off, buf.data(), n)) {
            off += src.sector;
            continue;
        }
        if (progress) progress(off - start, end - start, L"carve", user);
        for (uint32_t i = 0; i + 8 < n; ++i) {
            const char* type = nullptr;
            const uint8_t* foot = nullptr;
            size_t flen = 0;
            uint32_t maxs = 8 * 1024 * 1024;
            if (memcmp(buf.data() + i, MAG_JPG, 3) == 0) {
                type = "jpeg"; foot = FOO_JPG; flen = 2; maxs = 16 * 1024 * 1024;
            } else if (memcmp(buf.data() + i, MAG_PNG, 8) == 0) {
                type = "png"; foot = FOO_PNG; flen = 8; maxs = 32 * 1024 * 1024;
            } else if (memcmp(buf.data() + i, MAG_PDF, 4) == 0) {
                type = "pdf"; foot = FOO_PDF; flen = 5; maxs = 32 * 1024 * 1024;
            } else if (memcmp(buf.data() + i, MAG_ZIP, 4) == 0) {
                type = "zip"; maxs = 32 * 1024 * 1024;
            } else if (memcmp(buf.data() + i, MAG_GIF, 4) == 0) {
                type = "gif"; maxs = 8 * 1024 * 1024;
            }
            if (!type) continue;
            uint64_t foff = off + i;
            uint32_t take = maxs;
            if (foff + take > src.size) take = (uint32_t)(src.size - foff);
            std::vector<uint8_t> file(src.aligned_len(take));
            if (!src.read(foff, file.data(), (uint32_t)file.size())) continue;
            uint32_t used = take;
            if (foot) {
                for (uint32_t k = (uint32_t)flen; k + flen < take; ++k) {
                    if (memcmp(file.data() + k, foot, flen) == 0) {
                        used = k + (uint32_t)flen;
                        break;
                    }
                }
            }
            std::wstring dir = dest + L"\\carved\\" + utf8_to_wide(type, strlen(type));
            CreateDirectoryW((dest + L"\\carved").c_str(), nullptr);
            CreateDirectoryW(dir.c_str(), nullptr);
            wchar_t name[MAX_PATH];
            swprintf(name, MAX_PATH, L"%ls\\%05d.%s", dir.c_str(), ++seq, type);
            HANDLE out = CreateFileW(name, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (out == INVALID_HANDLE_VALUE) continue;
            DWORD wr = 0;
            WriteFile(out, file.data(), used, &wr, nullptr);
            CloseHandle(out);
            count++;
            i += 4096;
        }
        off += n;
    }
    return count;
}

bool write_at(HANDLE h, uint64_t off, const void* data, uint32_t len) {
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)off;
    if (!SetFilePointerEx(h, li, nullptr, FILE_BEGIN)) return false;
    DWORD wr = 0;
    if (!WriteFile(h, data, len, &wr, nullptr)) return false;
    return wr == len;
}

void gpt_fill_header(uint8_t* sec, uint32_t sector, uint64_t my, uint64_t alt,
                     uint64_t first_u, uint64_t last_u, const uint8_t* guid,
                     uint64_t part_lba, uint32_t pcount, uint32_t psize, uint32_t part_crc) {
    memset(sec, 0, sector);
    memcpy(sec, "EFI PART", 8);
    wr32(sec + 8, 0x00010000);
    wr32(sec + 12, 92);
    wr64(sec + 24, my);
    wr64(sec + 32, alt);
    wr64(sec + 40, first_u);
    wr64(sec + 48, last_u);
    memcpy(sec + 56, guid, 16);
    wr64(sec + 72, part_lba);
    wr32(sec + 80, pcount);
    wr32(sec + 84, psize);
    wr32(sec + 88, part_crc);
    wr32(sec + 16, 0);
    wr32(sec + 16, crc32_ieee(sec, 92));
}

void gpt_fill_pmbr(uint8_t* sec, uint32_t sector, uint64_t disk_lbas) {
    memset(sec, 0, sector);
    sec[0x1C2] = 0xEE;
    wr32(sec + 0x1C6, 1);
    uint64_t sz = disk_lbas > 1 ? disk_lbas - 1 : 0;
    wr32(sec + 0x1CA, sz > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)sz);
    sec[510] = 0x55;
    sec[511] = 0xAA;
}

std::wstring repair_gpt_work(const wchar_t* source, int do_write) {
    Source src;
    if (!src.open(source)) return L"{\"ok\":false,\"error\":\"open_failed\"}";
    uint32_t sec = src.sector ? src.sector : 512;
    uint64_t n = sec ? src.size / sec : 0;
    if (n < 64) return L"{\"ok\":false,\"error\":\"disk_too_small\"}";

    uint32_t two = src.aligned_len(sec * 2);
    std::vector<uint8_t> head(two);
    if (!src.read(0, head.data(), two)) return L"{\"ok\":false,\"error\":\"read_head\"}";

    uint64_t alt_p = 0, pl_p = 0, lu_p = 0, alt_b = 0, pl_b = 0, lu_b = 0;
    uint32_t pc_p = 0, ps_p = 0, crc_p = 0, pc_b = 0, ps_b = 0, crc_b = 0;
    bool prim = parse_gpt_header(head.data() + sec, sec, &alt_p, &pl_p, &pc_p, &ps_p, &lu_p, &crc_p);

    std::vector<uint8_t> tail(src.aligned_len(sec));
    bool back = false;
    if (src.read((n - 1) * sec, tail.data(), (uint32_t)tail.size())) {
        back = parse_gpt_header(tail.data(), sec, &alt_b, &pl_b, &pc_b, &ps_b, &lu_b, &crc_b);
    }
    if (!back && prim && alt_p && alt_p != n - 1 && alt_p < n) {
        std::vector<uint8_t> mid(src.aligned_len(sec));
        if (src.read(alt_p * sec, mid.data(), (uint32_t)mid.size())) {
            back = parse_gpt_header(mid.data(), sec, &alt_b, &pl_b, &pc_b, &ps_b, &lu_b, &crc_b);
            if (back) tail = mid;
        }
    }

    const uint8_t* good_hdr = nullptr;
    uint32_t pcount = 128, psize = 128;
    uint64_t old_pl = 2;
    if (prim && crc_p) {
        good_hdr = head.data() + sec;
        pcount = pc_p;
        psize = ps_p;
        old_pl = pl_p;
    } else if (back && crc_b) {
        good_hdr = tail.data();
        pcount = pc_b;
        psize = ps_b;
        old_pl = pl_b;
    }
    if (!good_hdr || !pcount || !psize || pcount > 128) {
        return L"{\"ok\":false,\"error\":\"no_valid_gpt_header\"}";
    }
    uint8_t disk_guid[16];
    memcpy(disk_guid, good_hdr + 56, 16);

    uint32_t array_bytes = pcount * psize;
    uint32_t array_al = src.aligned_len(array_bytes);
    std::vector<uint8_t> array(array_al, 0);
    bool array_ok = src.read(old_pl * sec, array.data(), array_al);
    uint32_t stored_arr_crc = rd32(good_hdr + 88);
    uint32_t calc_arr_crc = crc32_ieee(array.data(), array_bytes);
    if (!array_ok || calc_arr_crc != stored_arr_crc) {
        uint64_t other_pl = (good_hdr == head.data() + sec) ? pl_b : pl_p;
        if (other_pl && other_pl < n) {
            std::vector<uint8_t> array2(array_al, 0);
            if (src.read(other_pl * sec, array2.data(), array_al) &&
                crc32_ieee(array2.data(), array_bytes) == stored_arr_crc) {
                array.swap(array2);
                calc_arr_crc = stored_arr_crc;
            }
        }
    }
    if (crc32_ieee(array.data(), array_bytes) != stored_arr_crc && stored_arr_crc != calc_arr_crc) {
        calc_arr_crc = crc32_ieee(array.data(), array_bytes);
    }

    uint32_t entry_sectors = (array_bytes + sec - 1) / sec;
    if (!entry_sectors) entry_sectors = 32;
    uint64_t first_u = 2 + entry_sectors;
    uint64_t last_u = (n - 1) - 1 - entry_sectors;
    uint64_t new_alt = n - 1;
    uint64_t new_pl = 2;
    uint64_t bak_pl = new_alt - entry_sectors;

    bool need = !crc_p || !crc_b || (lu_p && lu_p != last_u) || (alt_p && alt_p != new_alt) || (pl_p && pl_p != new_pl);
    std::wstring o = L"{\"ok\":true,\"need_repair\":" + std::wstring(need ? L"true" : L"false");
    o += L",\"disk_lbas\":" + std::to_wstring(n);
    o += L",\"primary_crc_ok\":" + std::to_wstring(crc_p);
    o += L",\"backup_crc_ok\":" + std::to_wstring(crc_b);
    o += L",\"old_last_usable\":" + std::to_wstring(lu_p ? lu_p : lu_b);
    o += L",\"new_last_usable\":" + std::to_wstring(last_u);
    o += L",\"old_alternate\":" + std::to_wstring(alt_p ? alt_p : alt_b);
    o += L",\"new_alternate\":" + std::to_wstring(new_alt);
    o += L",\"is_image\":" + std::wstring(src.is_image ? L"true" : L"false");

    if (!do_write) {
        o += L",\"wrote\":false}";
        return o;
    }
    if (env_test_mode() && !src.is_image) {
        o += L",\"error\":\"test_mode_blocks_physical_write\"}";
        return o;
    }
    if (!need) {
        o += L",\"error\":\"already_consistent\"}";
        return o;
    }

    HANDLE hw = CreateFileW(src.path.c_str(), GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (hw == INVALID_HANDLE_VALUE) {
        o += L",\"error\":\"write_open_failed\"}";
        return o;
    }

    std::vector<uint8_t> pmbr(sec), phdr(sec), bhdr(sec);
    gpt_fill_pmbr(pmbr.data(), sec, n);
    gpt_fill_header(phdr.data(), sec, 1, new_alt, first_u, last_u, disk_guid, new_pl, pcount, psize, calc_arr_crc);
    gpt_fill_header(bhdr.data(), sec, new_alt, 1, first_u, last_u, disk_guid, bak_pl, pcount, psize, calc_arr_crc);

    bool w1 = write_at(hw, 0, pmbr.data(), sec);
    bool w2 = write_at(hw, sec, phdr.data(), sec);
    bool w3 = write_at(hw, new_pl * sec, array.data(), array_al);
    bool w4 = write_at(hw, bak_pl * sec, array.data(), array_al);
    bool w5 = write_at(hw, new_alt * sec, bhdr.data(), sec);
    CloseHandle(hw);
    bool all = w1 && w2 && w3 && w4 && w5;
    o += L",\"wrote\":" + std::wstring(all ? L"true" : L"false");
    if (!all) o += L",\"error\":\"write_failed\"";
    o += L"}";
    return o;
}

std::unique_ptr<Source> g_hold_disk;
std::unique_ptr<Source> g_hold_vol;
NtfsVol g_hold_ntfs;
std::wstring g_hold_k1, g_hold_k2, g_hold_volume;

int hold_load(const wchar_t* source, int part, std::wstring* err_json) {
    std::wstring k = std::wstring(source ? source : L"") + L"#" + std::to_wstring(part);
    if (!g_hold_ntfs.files.empty() && (k == g_hold_k1 || k == g_hold_k2)) return 0;
    g_hold_disk.reset(new Source());
    g_hold_vol.reset(new Source());
    g_hold_ntfs = NtfsVol();
    g_hold_k1.clear();
    g_hold_k2.clear();
    g_hold_volume.clear();
    if (!g_hold_disk->open(source)) {
        *err_json = L"{\"ok\":false,\"error\":\"open_failed\"}";
        return -1;
    }
    std::vector<GptPart> parts;
    if (open_ntfs(*g_hold_disk, part, g_hold_ntfs, parts)) {
        g_hold_k1 = k;
        g_hold_k2 = g_hold_disk->path + L"#" + std::to_wstring(part);
        return 0;
    }
    std::wstring vpath;
    bool locked = false;
    if (open_ntfs_via_volume(*g_hold_vol, source, part, g_hold_ntfs, &vpath, &locked)) {
        g_hold_volume = vpath;
        g_hold_k1 = k;
        g_hold_k2 = vpath + L"#0";
        return 0;
    }
    if (part >= 0 && part < (int)parts.size() && fs_magic(*g_hold_disk, parts[part].first) == L"BitLocker") {
        *err_json = L"{\"ok\":false,\"error\":\"bitlocker_locked\",\"volume\":\"" + json_escape(vpath) + L"\"}";
        return -1;
    }
    if (g_hold_disk->is_image) {
        wchar_t* aj = nullptr;
        if (qdr_bitlocker_attach_image(source, &aj) == 0 && aj) {
            std::wstring phys;
            json_get_str(aj, L"path", phys);
            qdr_free_json(aj);
            if (!phys.empty()) {
                g_hold_disk.reset(new Source());
                if (g_hold_disk->open(phys) && open_ntfs(*g_hold_disk, part, g_hold_ntfs, parts)) {
                    g_hold_k1 = k;
                    g_hold_k2 = phys + L"#" + std::to_wstring(part);
                    return 0;
                }
                g_hold_vol.reset(new Source());
                if (open_ntfs_via_volume(*g_hold_vol, phys.c_str(), part, g_hold_ntfs, &vpath, &locked)) {
                    g_hold_volume = vpath;
                    g_hold_k1 = k;
                    g_hold_k2 = vpath + L"#0";
                    return 0;
                }
            }
        } else if (aj) qdr_free_json(aj);
    }
    *err_json = L"{\"ok\":false,\"error\":\"ntfs_not_found\"}";
    return -1;
}

}  // namespace

void qdr_set_test_mode(int enabled) { g_test_mode = enabled ? 1 : 0; }

void qdr_free_json(wchar_t* p) { free(p); }

int qdr_list_disks_json(wchar_t** json_out) {
    std::wstring o = L"{\"ok\":true,\"items\":[";
    bool first = true;
    if (!env_test_mode()) {
        for (int i = 0; i < 32; ++i) {
            std::wstring p = L"\\\\.\\PhysicalDrive" + std::to_wstring(i);
            Source s;
            if (!s.open(p)) continue;
            if (!first) o += L",";
            first = false;
            o += L"{\"index\":" + std::to_wstring(i);
            o += L",\"path\":\"" + json_escape(p) + L"\"";
            o += L",\"size_bytes\":" + std::to_wstring(s.size);
            o += L",\"geometry_size_bytes\":" + std::to_wstring(s.geom_size);
            o += L",\"sector_size\":" + std::to_wstring(s.sector);
            o += L",\"phys_sector_size\":" + std::to_wstring(s.phys_sector);
            o += L",\"model\":\"" + json_escape(s.model) + L"\"";
            o += L",\"bus\":\"" + json_escape(s.bus) + L"\"";
            o += L",\"letters\":\"" + json_escape(letters_for_physical(i)) + L"\"";
            o += L",\"smart_failing\":" + std::to_wstring(s.smart_fail) + L"}";
        }
    }
    o += L"]}";
    *json_out = dup_json(o);
    return *json_out ? 0 : -1;
}

int qdr_diagnose_json(const wchar_t* source, int deep, wchar_t** json_out) {
    if (!source) return -1;
    std::wstring o = diagnose_source(source, deep);
    *json_out = dup_json(o);
    return *json_out ? 0 : -1;
}

int qdr_repair_gpt(const wchar_t* source, int do_write, wchar_t** json_out) {
    if (!source) return -1;
    std::wstring o = repair_gpt_work(source, do_write);
    *json_out = dup_json(o);
    return o.find(L"\"ok\":true") != std::wstring::npos && o.find(L"\"error\"") == std::wstring::npos ? 0 : -1;
}

int qdr_tree_json(const wchar_t* source, int partition_index, const wchar_t* ntfs_path,
                  volatile int* stop_flag,
                  void (*progress)(unsigned long long, unsigned long long, const wchar_t*, void*),
                  void* user, wchar_t** json_out) {
    g_ntfs_stop = stop_flag;
    g_ntfs_progress = progress;
    g_ntfs_progress_user = user;
    std::wstring err;
    int rc = hold_load(source, partition_index, &err);
    g_ntfs_stop = nullptr;
    g_ntfs_progress = nullptr;
    g_ntfs_progress_user = nullptr;
    if (rc != 0) {
        *json_out = dup_json(err);
        return -1;
    }
    if (stop_flag && *stop_flag) {
        *json_out = dup_json(L"{\"ok\":false,\"error\":\"cancelled\"}");
        return -1;
    }
    std::wstring want = ntfs_path && ntfs_path[0] ? ntfs_path : L"/";
    uint64_t rec = find_path(g_hold_ntfs, want);
    if (rec == (uint64_t)-1) rec = g_hold_ntfs.files.count(5) ? 5 : 0;
    std::wstring o = L"{\"ok\":true,\"partition\":" + std::to_wstring(partition_index);
    o += L",\"path\":\"" + json_escape(want) + L"\"";
    if (!g_hold_volume.empty()) o += L",\"volume\":\"" + json_escape(g_hold_volume) + L"\"";
    o += L",\"root\":";
    int nodes = 0;
    dump_file_json(g_hold_ntfs, rec, o, 0, &nodes);
    o += L"}";
    *json_out = dup_json(o);
    return 0;
}

int qdr_image(const wchar_t* source, const wchar_t* dest, volatile int* stop_flag,
              void (*progress)(unsigned long long, unsigned long long, const wchar_t*, void*),
              void* user, wchar_t** json_out) {
    Source src;
    if (!src.open(source)) {
        *json_out = dup_json(L"{\"ok\":false,\"error\":\"open_failed\"}");
        return -1;
    }
    HANDLE out = CreateFileW(dest, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        *json_out = dup_json(L"{\"ok\":false,\"error\":\"dest_open_failed\"}");
        return -1;
    }
    uint32_t chunk = src.aligned_len(4 * 1024 * 1024);
    std::vector<uint8_t> buf(chunk);
    uint64_t off = 0;
    uint64_t bad = 0;
    std::wstring map = std::wstring(dest) + L".badlba.txt";
    FILE* mf = _wfopen(map.c_str(), L"w");
    while (off < src.size) {
        if (stop_flag && *stop_flag) break;
        uint32_t n = chunk;
        if (off + n > src.size) n = src.aligned_len((uint32_t)(src.size - off));
        if (off + n > src.size) n = (uint32_t)(src.size - off);
        bool ok = src.read(off, buf.data(), n);
        if (!ok) {
            memset(buf.data(), 0, n);
            bad++;
            if (mf) fwprintf(mf, L"%llu\n", (unsigned long long)(off / (src.sector ? src.sector : 512)));
        }
        DWORD wr = 0;
        WriteFile(out, buf.data(), n, &wr, nullptr);
        off += n;
        if (progress) progress(off, src.size, L"image", user);
    }
    if (mf) fclose(mf);
    CloseHandle(out);
    std::wstring o = L"{\"ok\":true,\"path\":\"" + json_escape(dest) + L"\",\"bad_lbas\":" +
                     std::to_wstring(bad) + L"}";
    *json_out = dup_json(o);
    return 0;
}

int qdr_copy_out(const wchar_t* source, int partition_index, const wchar_t* ntfs_path,
                 const wchar_t* dest_dir, volatile int* stop_flag,
                 void (*progress)(unsigned long long, unsigned long long, const wchar_t*, void*),
                 void* user, wchar_t** json_out) {
    Source src;
    if (!src.open(source)) {
        *json_out = dup_json(L"{\"ok\":false,\"error\":\"open_failed\"}");
        return -1;
    }
    NtfsVol vol;
    std::vector<GptPart> parts;
    Source live;
    g_ntfs_stop = stop_flag;
    g_ntfs_progress = progress;
    g_ntfs_progress_user = user;
    bool found = open_ntfs(src, partition_index, vol, parts);
    std::wstring vpath;
    bool locked = false;
    if (!found) found = open_ntfs_via_volume(live, source, partition_index, vol, &vpath, &locked);
    g_ntfs_stop = nullptr;
    g_ntfs_progress = nullptr;
    g_ntfs_progress_user = nullptr;
    if (!found) {
        if (locked && !vpath.empty()) {
            *json_out = dup_json(L"{\"ok\":false,\"error\":\"bitlocker_locked\",\"volume\":\"" +
                                 json_escape(vpath) + L"\"}");
            return -1;
        }
        *json_out = dup_json(L"{\"ok\":false,\"error\":\"ntfs_not_found\"}");
        return -1;
    }
    if (stop_flag && *stop_flag) {
        *json_out = dup_json(L"{\"ok\":false,\"error\":\"cancelled\"}");
        return -1;
    }
    uint64_t rec = find_path(vol, ntfs_path ? ntfs_path : L"/");
    if (rec == (uint64_t)-1) rec = 5;
    CreateDirectoryW(dest_dir, nullptr);
    uint64_t copied = 0, failed = 0, partial = 0;
    auto it = vol.files.find(rec);
    std::wstring prefix = it == vol.files.end() ? L"/" : vol.path_of(rec);
    copy_tree(vol, rec, dest_dir, prefix, stop_flag, &copied, &failed, &partial);
    std::wstring o = L"{\"ok\":true,\"copied\":" + std::to_wstring(copied) +
                     L",\"failed\":" + std::to_wstring(failed) +
                     L",\"partial\":" + std::to_wstring(partial) + L"}";
    *json_out = dup_json(o);
    return 0;
}

int qdr_carve(const wchar_t* source, int partition_index, const wchar_t* dest_dir,
              volatile int* stop_flag,
              void (*progress)(unsigned long long, unsigned long long, const wchar_t*, void*),
              void* user, wchar_t** json_out) {
    Source src;
    if (!src.open(source)) {
        *json_out = dup_json(L"{\"ok\":false,\"error\":\"open_failed\"}");
        return -1;
    }
    uint64_t start = 0, end = src.size;
    std::vector<GptPart> parts;
    uint32_t alen = src.aligned_len(src.sector * 2);
    std::vector<uint8_t> head(alen);
    src.read(0, head.data(), alen);
    uint64_t alt, pl, lu;
    uint32_t pc, ps, crc;
    if (parse_gpt_header(head.data() + src.sector, src.sector, &alt, &pl, &pc, &ps, &lu, &crc)) {
        read_gpt_parts(src, pl, pc, ps, parts);
        if (partition_index >= 0 && partition_index < (int)parts.size()) {
            start = parts[partition_index].first * src.sector;
            end = (parts[partition_index].last + 1) * src.sector;
            if (end > src.size) end = src.size;
        }
    }
    int n = run_carve(src, start, end, dest_dir, stop_flag, progress, user);
    std::wstring o = L"{\"ok\":true,\"count\":" + std::to_wstring(n) +
                     L",\"dest\":\"" + json_escape(dest_dir) + L"\"}";
    *json_out = dup_json(o);
    return 0;
}
