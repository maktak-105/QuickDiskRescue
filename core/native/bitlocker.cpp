#define WIN32_LEAN_AND_MEAN
#define _WIN32_DCOM
#include <windows.h>
#include <winioctl.h>
#include <objbase.h>
#include <oleauto.h>
#include <initguid.h>
#include <wbemidl.h>
#include <virtdisk.h>
#ifndef VIRTUAL_STORAGE_TYPE_DEVICE_VHDX
#define VIRTUAL_STORAGE_TYPE_DEVICE_VHDX 3
#endif

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <cstdio>

#include "engine.h"

namespace {

std::wstring json_esc(const std::wstring& in) {
    std::wstring o;
    for (wchar_t c : in) {
        if (c == L'\\') o += L"\\\\";
        else if (c == L'"') o += L"\\\"";
        else o += c;
    }
    return o;
}

wchar_t* dupj(const std::wstring& s) {
    size_t n = s.size() + 1;
    wchar_t* p = (wchar_t*)malloc(n * sizeof(wchar_t));
    if (!p) return nullptr;
    memcpy(p, s.c_str(), n * sizeof(wchar_t));
    return p;
}

uint32_t rd32l(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}
uint64_t rd64l(const uint8_t* p) {
    return (uint64_t)rd32l(p) | ((uint64_t)rd32l(p + 4) << 32);
}

std::wstring trim_slash(std::wstring p) {
    while (!p.empty() && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    return p;
}

bool volume_reads_ntfs(const std::wstring& vol) {
    std::wstring p = trim_slash(vol);
    if (p.size() == 2 && p[1] == L':') p = L"\\\\.\\" + p;
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    uint8_t b[512] = {0};
    DWORD br = 0;
    BOOL ok = ReadFile(h, b, 512, &br, nullptr);
    CloseHandle(h);
    return ok && br >= 11 && memcmp(b + 3, "NTFS    ", 8) == 0;
}

int disk_num_from_source(const wchar_t* source) {
    if (!source) return -1;
    std::wstring p = source;
    size_t i = p.find(L"PhysicalDrive");
    if (i != std::wstring::npos) return _wtoi(p.c_str() + i + 13);
    HANDLE h = CreateFileW(p.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return -1;
    STORAGE_DEVICE_NUMBER num{};
    DWORD br = 0;
    int n = -1;
    if (DeviceIoControl(h, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &num, sizeof(num), &br, nullptr))
        n = (int)num.DeviceNumber;
    CloseHandle(h);
    return n;
}

bool gpt_part_offset(const wchar_t* source, int part_index, uint64_t* start, uint64_t* length, uint32_t* sector) {
    HANDLE h = CreateFileW(source, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    uint32_t sec = 512;
    DISK_GEOMETRY_EX geom{};
    DWORD br = 0;
    if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &geom, sizeof(geom), &br, nullptr) &&
        geom.Geometry.BytesPerSector)
        sec = geom.Geometry.BytesPerSector;
    std::vector<uint8_t> buf(sec * 34, 0);
    LARGE_INTEGER z{};
    SetFilePointerEx(h, z, nullptr, FILE_BEGIN);
    DWORD got = 0;
    ReadFile(h, buf.data(), (DWORD)buf.size(), &got, nullptr);
    CloseHandle(h);
    if (got < sec * 2) return false;
    const uint8_t* hdr = buf.data() + sec;
    if (memcmp(hdr, "EFI PART", 8) != 0) return false;
    uint64_t pl = rd64l(hdr + 72);
    uint32_t pc = rd32l(hdr + 80);
    uint32_t ps = rd32l(hdr + 84);
    if (!pc || !ps || part_index < 0 || (uint32_t)part_index >= pc || ps < 128) return false;
    HANDLE h2 = CreateFileW(source, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, 0, nullptr);
    if (h2 == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER off;
    off.QuadPart = (LONGLONG)(pl * sec + (uint64_t)part_index * ps);
    SetFilePointerEx(h2, off, nullptr, FILE_BEGIN);
    std::vector<uint8_t> e(ps, 0);
    DWORD g2 = 0;
    ReadFile(h2, e.data(), ps, &g2, nullptr);
    CloseHandle(h2);
    if (g2 < 128) return false;
    uint64_t first = rd64l(e.data() + 32);
    uint64_t last = rd64l(e.data() + 40);
    if (last < first) return false;
    *start = first * sec;
    *length = (last - first + 1) * sec;
    *sector = sec;
    return true;
}

bool extents_match(HANDLE h, int disk, uint64_t start, uint64_t length) {
    DWORD br = 0;
    DWORD bytes = (DWORD)(sizeof(VOLUME_DISK_EXTENTS) + 16 * sizeof(DISK_EXTENT));
    std::vector<uint8_t> buf(bytes);
    auto* ext = (VOLUME_DISK_EXTENTS*)buf.data();
    if (!DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, ext, bytes, &br, nullptr))
        return false;
    for (DWORD i = 0; i < ext->NumberOfDiskExtents; ++i) {
        const DISK_EXTENT& e = ext->Extents[i];
        if ((int)e.DiskNumber != disk) continue;
        if (!start) return true;
        uint64_t s = (uint64_t)e.StartingOffset.QuadPart;
        uint64_t l = (uint64_t)e.ExtentLength.QuadPart;
        if (s + 2ull * 1048576 >= start && start + 2ull * 1048576 >= s) {
            if (!length || l + 2ull * 1048576 >= length) return true;
        }
    }
    return false;
}

std::wstring find_volume(int disk, uint64_t start, uint64_t length, int part_index) {
    if (disk < 0) return L"";
    std::wstring ntfs_hit, part_hit, disk_hit;
    int on_disk = 0;
    wchar_t letters[512];
    if (GetLogicalDriveStringsW(511, letters)) {
        for (wchar_t* p = letters; *p; p += wcslen(p) + 1) {
            wchar_t path[8] = {L'\\', L'\\', L'.', L'\\', p[0], L':', 0};
            HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) continue;
            STORAGE_DEVICE_NUMBER num{};
            DWORD br = 0;
            bool same = DeviceIoControl(h, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &num, sizeof(num), &br, nullptr) &&
                        (int)num.DeviceNumber == disk;
            bool by_part = same && ((int)num.PartitionNumber == part_index + 1 || (int)num.PartitionNumber == part_index);
            bool by_off = same && extents_match(h, disk, start, length);
            CloseHandle(h);
            if (!same) continue;
            on_disk++;
            std::wstring wp = path;
            if (volume_reads_ntfs(wp)) ntfs_hit = wp;
            if (by_part || by_off) part_hit = wp;
            disk_hit = wp;
        }
    }
    if (!ntfs_hit.empty() && (part_hit.empty() || ntfs_hit == part_hit || on_disk == 1)) return ntfs_hit;
    if (!part_hit.empty()) return part_hit;
    if (on_disk == 1 && !disk_hit.empty()) return disk_hit;
    if (!ntfs_hit.empty()) return ntfs_hit;

    wchar_t name[MAX_PATH];
    HANDLE find = FindFirstVolumeW(name, MAX_PATH);
    if (find == INVALID_HANDLE_VALUE) return disk_hit;
    std::wstring hit;
    do {
        std::wstring open = trim_slash(name);
        HANDLE h = CreateFileW(open.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        STORAGE_DEVICE_NUMBER num{};
        DWORD br = 0;
        bool by_num = DeviceIoControl(h, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &num, sizeof(num), &br, nullptr) &&
                      (int)num.DeviceNumber == disk &&
                      ((int)num.PartitionNumber == part_index + 1 || (int)num.PartitionNumber == part_index);
        bool by_off = extents_match(h, disk, start, length);
        CloseHandle(h);
        if (by_off || by_num) {
            hit = open;
            if (volume_reads_ntfs(open)) break;
        }
    } while (FindNextVolumeW(find, name, MAX_PATH));
    FindVolumeClose(find);
    if (!hit.empty()) return hit;
    return disk_hit;
}

std::wstring letter_for_volume(const std::wstring& vol) {
    wchar_t paths[512];
    DWORD n = 0;
    std::wstring with = vol;
    if (with.empty() || with.back() != L'\\') with += L'\\';
    if (!GetVolumePathNamesForVolumeNameW(with.c_str(), paths, 511, &n) || !paths[0]) return L"";
    return paths;
}

std::wstring normalize_rp(const wchar_t* rp) {
    std::wstring o;
    if (!rp) return o;
    for (const wchar_t* p = rp; *p; ++p) {
        if (*p >= L'0' && *p <= L'9') o += *p;
        else if (*p == L'-' || *p == L' ') continue;
    }
    if (o.size() != 48) return o;
    std::wstring dashed;
    for (int i = 0; i < 8; ++i) {
        if (i) dashed += L'-';
        dashed += o.substr((size_t)i * 6, 6);
    }
    return dashed;
}

bool run_process(const std::wstring& cmd, DWORD* code) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring mut = cmd;
    BOOL ok = CreateProcessW(nullptr, &mut[0], nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) return false;
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD c = 1;
    GetExitCodeProcess(pi.hProcess, &c);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (code) *code = c;
    return true;
}

bool manage_bde_unlock(const std::wstring& vol, const std::wstring& rp, const std::wstring& bek, const std::wstring& pw) {
    std::wstring target = vol;
    std::wstring let = letter_for_volume(vol);
    if (!let.empty()) target = let;
    else if (target.empty() || target.back() != L'\\') target += L'\\';
    std::wstring cmd;
    if (!rp.empty()) {
        cmd = L"manage-bde.exe -unlock \"" + target + L"\" -recoverypassword " + rp;
    } else if (!bek.empty()) {
        cmd = L"manage-bde.exe -unlock \"" + target + L"\" -recoverykey \"" + bek + L"\"";
    } else if (!pw.empty()) {
        return false;
    } else {
        return false;
    }
    DWORD code = 1;
    if (!run_process(cmd, &code)) return false;
    return code == 0 || volume_reads_ntfs(vol);
}

std::wstring wmi_escape_id(const std::wstring& id) {
    std::wstring o;
    for (wchar_t c : id) {
        if (c == L'\\') o += L"\\\\";
        else o += c;
    }
    return o;
}

bool wmi_exec_unlock(const std::wstring& device_id, const wchar_t* method, const wchar_t* arg_name, const wchar_t* arg_val) {
    IWbemLocator* loc = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (void**)&loc);
    if (FAILED(hr) || !loc) return false;
    IWbemServices* svc = nullptr;
    BSTR ns = SysAllocString(L"ROOT\\CIMV2\\Security\\MicrosoftVolumeEncryption");
    hr = loc->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
    SysFreeString(ns);
    loc->Release();
    if (FAILED(hr) || !svc) return false;
    CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                      RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    std::wstring matched;
    IEnumWbemClassObject* en = nullptr;
    BSTR q = SysAllocString(L"WQL");
    BSTR qs = SysAllocString(L"SELECT DeviceID FROM Win32_EncryptableVolume");
    hr = svc->ExecQuery(q, qs, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &en);
    SysFreeString(q);
    SysFreeString(qs);
    if (SUCCEEDED(hr) && en) {
        IWbemClassObject* row = nullptr;
        ULONG got = 0;
        while (en->Next(3000, 1, &row, &got) == S_OK && got) {
            VARIANT v;
            VariantInit(&v);
            if (SUCCEEDED(row->Get(L"DeviceID", 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR && v.bstrVal) {
                std::wstring did = v.bstrVal;
                if (did.find(device_id) != std::wstring::npos || device_id.find(did) != std::wstring::npos)
                    matched = did;
            }
            VariantClear(&v);
            row->Release();
            if (!matched.empty()) break;
        }
        en->Release();
    }
    if (matched.empty()) {
        matched = device_id;
        if (matched.empty() || matched.back() != L'\\') matched += L'\\';
    }

    IWbemClassObject* cls = nullptr;
    BSTR cname = SysAllocString(L"Win32_EncryptableVolume");
    svc->GetObject(cname, 0, nullptr, &cls, nullptr);
    SysFreeString(cname);
    IWbemClassObject* in_sig = nullptr;
    BSTR mname = SysAllocString(method);
    if (cls) cls->GetMethod(mname, 0, &in_sig, nullptr);
    IWbemClassObject* in_params = nullptr;
    if (in_sig) in_sig->SpawnInstance(0, &in_params);
    if (in_params && arg_name && arg_val) {
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_BSTR;
        v.bstrVal = SysAllocString(arg_val);
        in_params->Put(arg_name, 0, &v, 0);
        VariantClear(&v);
    }
    std::wstring path = L"Win32_EncryptableVolume.DeviceID=\"" + wmi_escape_id(matched) + L"\"";
    BSTR objp = SysAllocString(path.c_str());
    IWbemClassObject* outp = nullptr;
    hr = svc->ExecMethod(objp, mname, 0, nullptr, in_params, &outp, nullptr);
    SysFreeString(objp);
    SysFreeString(mname);
    bool ok = false;
    if (SUCCEEDED(hr) && outp) {
        VARIANT rv;
        VariantInit(&rv);
        if (SUCCEEDED(outp->Get(L"ReturnValue", 0, &rv, nullptr, nullptr))) {
            uint32_t code = 1;
            if (rv.vt == VT_I4) code = (uint32_t)rv.lVal;
            else if (rv.vt == VT_UI4) code = rv.ulVal;
            ok = (code == 0);
        }
        VariantClear(&rv);
        outp->Release();
    }
    if (in_params) in_params->Release();
    if (in_sig) in_sig->Release();
    if (cls) cls->Release();
    svc->Release();
    return ok;
}

bool wmi_unlock(const std::wstring& vol, const std::wstring& rp, const std::wstring& bek, const std::wstring& pw) {
    (void)bek;
    std::wstring id = vol;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool uninit = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE) uninit = false;
    bool ok = false;
    if (!rp.empty()) ok = wmi_exec_unlock(id, L"UnlockWithNumericalPassword", L"NumericalPassword", rp.c_str());
    if (!ok && !pw.empty()) ok = wmi_exec_unlock(id, L"UnlockWithPassphrase", L"Passphrase", pw.c_str());
    if (uninit) CoUninitialize();
    return ok;
}

HANDLE g_vhd = INVALID_HANDLE_VALUE;

}  // namespace

int qdr_find_volume_json(const wchar_t* source, int partition_index, wchar_t** json_out) {
    if (!source) {
        *json_out = dupj(L"{\"ok\":false,\"error\":\"no_source\"}");
        return -1;
    }
    uint64_t start = 0, len = 0;
    uint32_t sec = 512;
    int disk = disk_num_from_source(source);
    gpt_part_offset(source, partition_index, &start, &len, &sec);
    if (disk < 0) {
        *json_out = dupj(L"{\"ok\":false,\"error\":\"no_disk\"}");
        return -1;
    }
    std::wstring vol = find_volume(disk, start, len, partition_index);
    bool unlocked = !vol.empty() && volume_reads_ntfs(vol);
    std::wstring o = L"{\"ok\":true,\"volume\":\"" + json_esc(vol) + L"\"";
    o += L",\"unlocked\":" + std::wstring(unlocked ? L"true" : L"false");
    o += L",\"disk\":" + std::to_wstring(disk);
    o += L",\"start\":" + std::to_wstring(start) + L"}";
    *json_out = dupj(o);
    return 0;
}

int qdr_bitlocker_unlock(const wchar_t* volume, const wchar_t* recovery_password,
                         const wchar_t* bek_path, const wchar_t* passphrase, wchar_t** json_out) {
    if (!volume || !volume[0]) {
        *json_out = dupj(L"{\"ok\":false,\"error\":\"no_volume\"}");
        return -1;
    }
    std::wstring vol = volume;
    std::wstring rp = normalize_rp(recovery_password);
    std::wstring bek = bek_path ? bek_path : L"";
    std::wstring pw = passphrase ? passphrase : L"";
    if (volume_reads_ntfs(vol)) {
        *json_out = dupj(L"{\"ok\":true,\"unlocked\":true,\"method\":\"already\"}");
        return 0;
    }
    std::wstring method;
    if (wmi_unlock(vol, rp, bek, pw)) method = L"wmi";
    if (method.empty() && manage_bde_unlock(vol, rp, bek, pw)) method = L"manage-bde";
    bool unlocked = volume_reads_ntfs(vol);
    if (!unlocked && method.empty()) {
        *json_out = dupj(L"{\"ok\":false,\"error\":\"unlock_failed\",\"unlocked\":false}");
        return -1;
    }
    if (!unlocked) {
        *json_out = dupj(L"{\"ok\":false,\"error\":\"still_locked\",\"method\":\"" + json_esc(method) + L"\"}");
        return -1;
    }
    std::wstring o = L"{\"ok\":true,\"unlocked\":true,\"method\":\"" + json_esc(method) + L"\"}";
    *json_out = dupj(o);
    return 0;
}

int qdr_bitlocker_attach_image(const wchar_t* image, wchar_t** json_out) {
    if (!image) {
        *json_out = dupj(L"{\"ok\":false,\"error\":\"no_image\"}");
        return -1;
    }
    std::wstring p = image;
    size_t dot = p.find_last_of(L'.');
    std::wstring ext = dot == std::wstring::npos ? L"" : p.substr(dot);
    for (auto& c : ext) if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
    if (ext != L".vhd" && ext != L".vhdx" && ext != L".iso") {
        *json_out = dupj(L"{\"ok\":false,\"error\":\"not_vhd\"}");
        return -1;
    }
    VIRTUAL_STORAGE_TYPE type{};
    type.DeviceId = (ext == L".iso") ? VIRTUAL_STORAGE_TYPE_DEVICE_ISO :
                    (ext == L".vhdx") ? VIRTUAL_STORAGE_TYPE_DEVICE_VHDX : VIRTUAL_STORAGE_TYPE_DEVICE_VHD;
    type.VendorId = VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT;
    OPEN_VIRTUAL_DISK_PARAMETERS op{};
    op.Version = OPEN_VIRTUAL_DISK_VERSION_1;
    HANDLE vh = INVALID_HANDLE_VALUE;
    DWORD err = OpenVirtualDisk(&type, image, VIRTUAL_DISK_ACCESS_ATTACH_RO, OPEN_VIRTUAL_DISK_FLAG_NONE, &op, &vh);
    if (err != ERROR_SUCCESS) {
        *json_out = dupj(L"{\"ok\":false,\"error\":\"open_vhd\",\"win\":" + std::to_wstring(err) + L"}");
        return -1;
    }
    ATTACH_VIRTUAL_DISK_PARAMETERS ap{};
    ap.Version = ATTACH_VIRTUAL_DISK_VERSION_1;
    err = AttachVirtualDisk(vh, nullptr,
                            (ATTACH_VIRTUAL_DISK_FLAG)(ATTACH_VIRTUAL_DISK_FLAG_READ_ONLY | ATTACH_VIRTUAL_DISK_FLAG_PERMANENT_LIFETIME),
                            0, &ap, nullptr);
    if (err != ERROR_SUCCESS) {
        CloseHandle(vh);
        *json_out = dupj(L"{\"ok\":false,\"error\":\"attach_vhd\",\"win\":" + std::to_wstring(err) + L"}");
        return -1;
    }
    if (g_vhd != INVALID_HANDLE_VALUE) CloseHandle(g_vhd);
    g_vhd = vh;
    wchar_t phys[MAX_PATH] = {0};
    ULONG psz = MAX_PATH;
    GetVirtualDiskPhysicalPath(vh, &psz, phys);
    std::wstring o = L"{\"ok\":true,\"attached\":true,\"path\":\"" + json_esc(phys) + L"\"}";
    *json_out = dupj(o);
    return 0;
}
