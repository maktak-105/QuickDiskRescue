#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <string>
#include "engine.h"

#define APP_VERSION L"1.0.1"

static std::string wide_to_utf8(const wchar_t* s) {
    if (!s) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    std::string o((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s, -1, &o[0], n, nullptr, nullptr);
    if (!o.empty() && o.back() == 0) o.pop_back();
    return o;
}

static void print_json(const wchar_t* s) {
    std::string u = wide_to_utf8(s);
    fwrite(u.data(), 1, u.size(), stdout);
    fputc('\n', stdout);
}

static void progress_cb(unsigned long long done, unsigned long long total, const wchar_t*, void*) {
    if (!total) return;
    fprintf(stderr, "\r%llu / %llu (%.1f%%)", done, total, 100.0 * (double)done / (double)total);
}

static void usage() {
    fprintf(stderr,
        "QuickDiskRescue_cli %ls\n"
        "Usage:\n"
        "  list\n"
        "  diagnose --source <PhysicalDriveN|image> [--deep]\n"
        "  tree --source <src> [--partition N]\n"
        "  copy --source <src> --from <ntfs-path> --out <dir> [--partition N]\n"
        "  image --source <src> --out <file>\n"
        "  carve --source <src> --out <dir> [--partition N]\n"
        "  repair-gpt --source <src> [--write]\n"
        "  unlock --source <src> [--partition N] [--recoverypassword RP] [--bek file] [--password PW]\n",
        APP_VERSION);
}

int run_cli(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    char tm[8];
    if (GetEnvironmentVariableA("QUICKAPPSTEST", tm, sizeof(tm)) && tm[0] == '1') {
        qdr_set_test_mode(1);
    }
    if (argc < 2) {
        usage();
        return 2;
    }
    std::wstring cmd = argv[1];
    std::wstring source, out, from, rp, bek, password;
    int part = -1, deep = 0, do_write = 0;
    for (int i = 2; i < argc; ++i) {
        std::wstring a = argv[i];
        auto need = [&](std::wstring& dst) {
            if (i + 1 < argc) dst = argv[++i];
        };
        if (a == L"--source" || a == L"--image" || a == L"--disk") need(source);
        else if (a == L"--out") need(out);
        else if (a == L"--from" || a == L"--path") need(from);
        else if (a == L"--partition" && i + 1 < argc) part = _wtoi(argv[++i]);
        else if (a == L"--deep") deep = 1;
        else if (a == L"--write") do_write = 1;
        else if (a == L"--recoverypassword" || a == L"--rp") need(rp);
        else if (a == L"--bek") need(bek);
        else if (a == L"--password") need(password);
    }

    wchar_t* json = nullptr;
    int rc = 0;
    if (cmd == L"list") {
        rc = qdr_list_disks_json(&json);
    } else if (cmd == L"diagnose") {
        if (source.empty()) { usage(); return 2; }
        rc = qdr_diagnose_json(source.c_str(), deep, &json);
    } else if (cmd == L"tree") {
        if (source.empty()) { usage(); return 2; }
        volatile int stop = 0;
        rc = qdr_tree_json(source.c_str(), part, from.empty() ? L"/" : from.c_str(), &stop, progress_cb, nullptr, &json);
    } else if (cmd == L"copy") {
        if (source.empty() || out.empty()) { usage(); return 2; }
        volatile int stop = 0;
        rc = qdr_copy_out(source.c_str(), part, from.empty() ? L"/" : from.c_str(), out.c_str(),
                          &stop, progress_cb, nullptr, &json);
        fprintf(stderr, "\n");
    } else if (cmd == L"image") {
        if (source.empty() || out.empty()) { usage(); return 2; }
        volatile int stop = 0;
        rc = qdr_image(source.c_str(), out.c_str(), &stop, progress_cb, nullptr, &json);
        fprintf(stderr, "\n");
    } else if (cmd == L"carve") {
        if (source.empty() || out.empty()) { usage(); return 2; }
        volatile int stop = 0;
        rc = qdr_carve(source.c_str(), part, out.c_str(), &stop, progress_cb, nullptr, &json);
        fprintf(stderr, "\n");
    } else if (cmd == L"repair-gpt") {
        if (source.empty()) { usage(); return 2; }
        rc = qdr_repair_gpt(source.c_str(), do_write, &json);
    } else if (cmd == L"unlock") {
        if (source.empty()) { usage(); return 2; }
        wchar_t* found = nullptr;
        qdr_find_volume_json(source.c_str(), part < 0 ? 0 : part, &found);
        std::wstring vol = source;
        if (found) {
            std::wstring f = found;
            qdr_free_json(found);
            size_t vp = f.find(L"\"volume\":\"");
            if (vp != std::wstring::npos) {
                vp += 10;
                size_t ve = f.find(L'"', vp);
                if (ve != std::wstring::npos) {
                    vol = f.substr(vp, ve - vp);
                    for (size_t i = 0; i + 1 < vol.size(); ++i)
                        if (vol[i] == L'\\' && vol[i + 1] == L'\\') vol.erase(i, 1);
                }
            }
        }
        rc = qdr_bitlocker_unlock(vol.c_str(), rp.c_str(), bek.c_str(), password.c_str(), &json);
    } else {
        usage();
        return 2;
    }
    if (json) {
        print_json(json);
        qdr_free_json(json);
    }
    return rc == 0 ? 0 : 1;
}

int main() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int rc = run_cli(argc, argv);
    LocalFree(argv);
    return rc;
}
