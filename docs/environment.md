# Development environment

## Runtime

Windows 10 / 11 x64, C++17, MinGW-w64, WebView2 SDK at `C:\tools\webview2\build\native\include`.

## Build

```powershell
cd QuickDiskRescue
build.bat
```

Outputs: `dist\QuickDiskRescue.exe` and `QuickDiskRescue_cli.exe`.

GUI requires administrator rights. CLI is asInvoker. `QUICKAPPSTEST=1` never opens PhysicalDrive.
