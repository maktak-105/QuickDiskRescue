# QuickDiskRescue

Recover folders from disks Windows will not mount. The source is never written.

Version: **v1.0.2**

Implementation: **C++17 + WebView2**. No Python in the shipped app.

## Using the binary release

Download the distribution ZIP from GitHub Releases:

- [Latest releases](https://github.com/maktak-105/QuickDiskRescue/releases)
- [QuickDiskRescue v1.0.2](https://github.com/maktak-105/QuickDiskRescue/releases/tag/v1.0.2)
- [Direct download of QuickDiskRescue-binary.zip](https://github.com/maktak-105/QuickDiskRescue/releases/download/v1.0.2/QuickDiskRescue-binary.zip)

The ZIP contains all distribution files in one flat folder:

- `QuickDiskRescue.exe` - GUI version (self-contained embedded HTML)
- `QuickDiskRescue_cli.exe` - command-line version
- `WebView2Loader.dll` - WebView2 loader
- `readme.txt` / `readme_jp.txt` - distribution documentation
- `history.txt` / `history_jp.txt` - update history
- `LICENSE.txt` / `LICENSE_jp.txt` - MIT License files

### Integrity verification (SHA-256)

Official SHA-256 checksums for the distribution ZIP and binaries are automatically computed during the CI (GitHub Actions) build and published as `SHA256SUMS.txt` on each release page. Verify the downloaded package with PowerShell:

```powershell
Get-FileHash .\QuickDiskRescue-binary.zip -Algorithm SHA256
```

## Use

1. Run `QuickDiskRescue.exe` elevated
2. Pick a physical disk or open an image
3. Diagnose, then rescue a folder to another drive
4. Prefer imaging first

See `README_jp.md` for CLI examples.
## CLI Usage

```powershell
.\dist\QuickDiskRescue_cli.exe list
.\dist\QuickDiskRescue_cli.exe diagnose --source \\.\PhysicalDrive1
.\dist\QuickDiskRescue_cli.exe tree --source disk.img --partition 0
.\dist\QuickDiskRescue_cli.exe copy --source disk.img --from /Users --out E:\rescued
.\dist\QuickDiskRescue_cli.exe image --source \\.\PhysicalDrive1 --out E:\disk.img
.\dist\QuickDiskRescue_cli.exe carve --source disk.img --out E:\carved
```

## Build from source

```powershell
winget install --id BrechtSanders.WinLibs.MCF.UCRT --exact --source winget
scripts\build.bat
# → dist\QuickDiskRescue.exe
```

## Repository layout

```
QuickDiskRescue/
├── src/
│   ├── app/              GUI host & Windows resources (main_gui.cpp, .rc, .ico, .manifest)
│   ├── cli/              CLI entry point & resource (main_cli.cpp, .rc)
│   ├── engine/           Disk rescue & bitlocker engine (engine.cpp, engine.h, bitlocker.cpp)
│   └── ui/               UI source files (index.html, css/, js/, img/)
├── proto/tests/          Python test runners and fixture generators
├── scripts/              build.py, build.bat, bundle_html.py
├── docs/                 specification, environment, and version information
│   └── distribution/     packaged readme / history / LICENSE
├── dist/                 flat build output (not in git except .gitkeep)
└── .github/workflows/    CI and release workflows
```

MIT License. Author: maktak-105
