from __future__ import annotations

import glob
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
APP = SRC / "app"
CLI = SRC / "cli"
ENGINE = SRC / "engine"
INTERMEDIATE = ROOT / "build" / "intermediate"
DIST = ROOT / "dist"
sys.path.insert(0, str(Path(__file__).resolve().parent))
import bundle_html


def find_compiler() -> Path | None:
    for name in ("g++", "clang++"):
        found = shutil.which(name)
        if found:
            return Path(found)
    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data:
        pattern = os.path.join(local_app_data, "Microsoft", "WinGet", "Packages", "BrechtSanders.WinLibs.MCF.UCRT_*", "mingw64", "bin")
        for folder in glob.glob(pattern):
            for name in ("g++.exe", "clang++.exe"):
                candidate = Path(folder) / name
                if candidate.is_file():
                    return candidate
    for candidate in (Path(r"C:\tools\llvm-mingw\bin\clang++.exe"), Path(r"C:\Program Files\LLVM\bin\clang++.exe"), Path(r"C:\msys64\ucrt64\bin\g++.exe"), Path(r"C:\msys64\mingw64\bin\g++.exe")):
        if candidate.is_file():
            return candidate
    return None


def run(command: list[str], label: str, cwd: Path | None = None) -> None:
    print(f"\n[{label}] {' '.join(command)}")
    result = subprocess.run(command, cwd=cwd or ROOT, text=True)
    if result.returncode:
        raise SystemExit(result.returncode)


def build() -> None:
    compiler = find_compiler()
    if not compiler:
        raise SystemExit("C++ compiler (g++ or clang++) was not found")
    compiler_dir = compiler.parent
    windres = next((p for p in (compiler_dir / "llvm-windres.exe", compiler_dir / "windres.exe") if p.is_file()), None)
    if not windres:
        found = shutil.which("windres") or shutil.which("llvm-windres")
        windres = Path(found) if found else None
    if not windres:
        raise SystemExit("Windows resource compiler (windres/llvm-windres) was not found")

    webview_include = Path(os.environ.get("WEBVIEW2_INCLUDE", r"C:\tools\webview2\build\native\include"))
    if not (webview_include / "WebView2.h").is_file():
        raise SystemExit(f"WebView2 SDK headers not found: {webview_include}")
    loader = Path(os.environ.get("WEBVIEW2_LOADER", str(webview_include.parent / "x64" / "WebView2Loader.dll")))
    if not loader.is_file():
        raise SystemExit(f"WebView2Loader.dll not found: {loader}")

    INTERMEDIATE.mkdir(parents=True, exist_ok=True)
    DIST.mkdir(parents=True, exist_ok=True)
    bundle_html.bundle(INTERMEDIATE)
    (INTERMEDIATE / "index_embed.html").write_bytes((INTERMEDIATE / "index.html").read_bytes())
    res_gui = INTERMEDIATE / "QuickDiskRescue_res.o"
    res_cli = INTERMEDIATE / "QuickDiskRescue_cli_res.o"
    run([str(windres), f"-I{APP}", f"-I{INTERMEDIATE}", str(APP / "QuickDiskRescue.rc"), "-O", "coff", "-o", str(res_gui)], "gui-resource", APP)
    run([str(windres), f"-I{APP}", f"-I{CLI}", str(CLI / "QuickDiskRescue_cli.rc"), "-O", "coff", "-o", str(res_cli)], "cli-resource", CLI)
    common = [str(compiler), "-O3", "-std=c++17", "-static", f"-I{ENGINE}"]
    engine_sources = [str(ENGINE / "engine.cpp"), str(ENGINE / "bitlocker.cpp")]
    libs = ["-lkernel32", "-lshell32", "-lole32", "-loleaut32", "-luuid", "-lwbemuuid", "-lvirtdisk", "-ladvapi32"]
    run(common + ["-mwindows", f"-I{webview_include}", f"-I{APP}", *engine_sources, str(APP / "main_gui.cpp"), str(res_gui), "-o", str(DIST / "QuickDiskRescue.exe"), "-luser32", "-lgdi32", "-ldwmapi", "-lcomctl32", *libs], "gui-build")
    run(common + [f"-I{CLI}", *engine_sources, str(CLI / "main_cli.cpp"), str(res_cli), "-o", str(DIST / "QuickDiskRescue_cli.exe"), *libs], "cli-build")
    shutil.copy2(loader, DIST / "WebView2Loader.dll")
    for path in (DIST / "QuickDiskRescue.exe", DIST / "QuickDiskRescue_cli.exe", DIST / "WebView2Loader.dll"):
        print(f"[ok] {path.relative_to(ROOT)} ({path.stat().st_size:,} bytes)")


if __name__ == "__main__":
    build()
