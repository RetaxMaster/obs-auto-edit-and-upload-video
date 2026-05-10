# Release Guide

## Local Windows Build

Prerequisites:

- Windows x64
- Visual Studio 2022 with C++ workload
- CMake 3.28+
- PowerShell 7
- Inno Setup 6
- FFmpeg Windows binaries if local packages should include `ffmpeg.exe` and `ffprobe.exe`

Configure and build:

```powershell
cmake --preset windows-x64 `
  -DRIZZYTOS_CLIENT_ID="your-client-id" `
  -DRIZZYTOS_CLIENT_SECRET="your-client-secret" `
  -DBUILD_TRANSLATIONS=OFF `
  -DBUILD_SHARED_LIBS=OFF

cmake --build --preset windows-x64 --config Release --parallel
```

Prepare the release folder:

```powershell
scripts/prepare-release.ps1 -BuildDir build_x64 -Config Release -FfmpegDir C:\path\to\ffmpeg\bin
```

The expected release layout is:

```text
release/
├─ obs-plugins/
│  └─ 64bit/
│     └─ rizzytos-auto-edit.dll
└─ data/
   └─ obs-plugins/
      └─ rizzytos-auto-edit/
         ├─ rizzytos-worker.exe
         ├─ ffmpeg.exe
         ├─ ffprobe.exe
         └─ locale/
```

`FfmpegDir` is optional for local packaging. If omitted, the script warns and creates a release without `ffmpeg.exe` or `ffprobe.exe`.

## Build Installer

Install Inno Setup 6, then run:

```powershell
scripts/build-installer.ps1 -BuildDir build_x64 -Config Release -FfmpegDir C:\path\to\ffmpeg\bin -Version 1.0.0
```

Output:

- `dist/RizzytosAutoEdit-Setup-x64.exe`

Debug symbols are not packaged by default. Pass `-IncludeSymbols` only for private/debuggable builds.

## Create a GitHub Release

Create and push a `v*` tag:

```powershell
git tag -a v1.0.0 -m "Release v1.0.0"
git push origin v1.0.0
```

The `Release` workflow uploads:

- `RizzytosAutoEdit-Setup-x64.exe`
- `RizzytosAutoEdit-portable-x64.zip`
- `checksums.txt`

`push.yaml` is CI-only. GitHub Release publishing is owned by `.github/workflows/release.yml`.

## GitHub Secrets

Configure repository secrets in GitHub under Settings > Secrets and variables > Actions:

- `RIZZYTOS_CLIENT_ID`
- `RIZZYTOS_CLIENT_SECRET`

These values are currently compiled into the binary. That is acceptable for internal testing, but public distribution should move to PKCE or a backend OAuth flow.

## FFmpeg and Licenses

Release builds include `ffmpeg.exe` and `ffprobe.exe` when available. Verify the license of the selected FFmpeg build before distributing artifacts.

The release workflow writes `THIRD-PARTY-NOTICES.txt` into the plugin data folder. Keep that file current with the FFmpeg source and license notes for the build being distributed.

## Installing or Updating

Close OBS Studio before installing, updating, or uninstalling. The installer blocks installation when `obs64.exe` is running.

The uninstaller removes only:

- `obs-plugins\64bit\rizzytos-auto-edit.dll`
- `data\obs-plugins\rizzytos-auto-edit\`

It does not remove OBS Studio or shared OBS directories.
