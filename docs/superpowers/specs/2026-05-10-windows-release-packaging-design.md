# Windows Release Packaging Design

**Project:** rizzytos-auto-edit
**Date:** 2026-05-10
**Status:** Approved design, pending implementation plan

## Goal

Add a Windows x64 packaging and installer system for the OBS plugin `rizzytos-auto-edit`.

The release pipeline must produce:

- `dist/RizzytosAutoEdit-Setup-x64.exe`
- `dist/RizzytosAutoEdit-portable-x64.zip`
- `dist/checksums.txt`

The GitHub Release publishing path must live only in `.github/workflows/release.yml`, triggered by tags matching `v*`. The existing `push.yaml` workflow must remain normal CI only and must not publish releases or assets under any condition.

## Installation Layout

The installer and portable ZIP both represent this OBS Windows layout:

```text
C:\Program Files\obs-studio\
├─ obs-plugins\
│  └─ 64bit\
│     └─ rizzytos-auto-edit.dll
└─ data\
   └─ obs-plugins\
      └─ rizzytos-auto-edit\
         ├─ rizzytos-worker.exe
         ├─ ffmpeg.exe
         ├─ ffprobe.exe
         ├─ presets/
         ├─ assets/
         ├─ locale/
         └─ other plugin data files
```

`ffmpeg.exe` and `ffprobe.exe` are included when available. Missing FFmpeg inputs should warn during local release preparation, not fail.

## Files

Create:

- `installer/rizzytos-auto-edit.iss`
- `scripts/prepare-release.ps1`
- `scripts/build-installer.ps1`
- `.github/workflows/release.yml`
- `docs/release.md`

Modify:

- `.github/workflows/push.yaml`
- `.gitignore`

## Release Preparation Script

`scripts/prepare-release.ps1` is the deterministic layout script. It receives:

- `BuildDir`
- `Config`, default `Release`
- `ReleaseDir`, default `release`
- optional `FfmpegDir`
- optional `IncludeSymbols`

It deletes and recreates `ReleaseDir`, then creates:

- `release/obs-plugins/64bit/`
- `release/data/obs-plugins/rizzytos-auto-edit/`

It copies required build outputs:

- `<BuildDir>/rundir/<Config>/rizzytos-auto-edit.dll` to `release/obs-plugins/64bit/rizzytos-auto-edit.dll`
- `<BuildDir>/<Config>/rizzytos-worker.exe` to `release/data/obs-plugins/rizzytos-auto-edit/rizzytos-worker.exe`

It copies plugin data if present:

- `<BuildDir>/rundir/<Config>/rizzytos-auto-edit/` to `release/data/obs-plugins/rizzytos-auto-edit/`

It copies optional FFmpeg binaries when `FfmpegDir` is supplied:

- `<FfmpegDir>/ffmpeg.exe`
- `<FfmpegDir>/ffprobe.exe`

If `FfmpegDir` is omitted, it emits a clear warning. If `FfmpegDir` is supplied but either executable is missing, it fails with a clear error.

PDB files are not copied by default. `-IncludeSymbols` enables copying available `.pdb` files for the plugin and worker into their matching release folders. This keeps public artifacts free of debug symbols unless explicitly requested.

The script must not contain absolute developer-machine paths.

## Inno Setup Installer

`installer/rizzytos-auto-edit.iss` consumes the `release/` directory and produces:

```text
dist/RizzytosAutoEdit-Setup-x64.exe
```

Installer requirements:

- `AppName=Rizzytos Auto Edit`
- default install directory `{pf}\obs-studio`
- allow the user to change the installation directory
- x64 architecture mode
- version passed through an Inno define when available, defaulting to `1.0.0`
- basic publisher and uninstall metadata from the project
- install `release/obs-plugins/64bit/rizzytos-auto-edit.dll` into `{app}\obs-plugins\64bit\`
- install all `release/data/obs-plugins/rizzytos-auto-edit/*` into `{app}\data\obs-plugins\rizzytos-auto-edit\`
- block installation if `obs64.exe` is running
- uninstall only this plugin's files:
  - `{app}\obs-plugins\64bit\rizzytos-auto-edit.dll`
  - `{app}\data\obs-plugins\rizzytos-auto-edit`

The uninstaller must not remove OBS itself or shared OBS directories.

## Installer Build Script

`scripts/build-installer.ps1` receives:

- `BuildDir`
- `Config`, default `Release`
- optional `FfmpegDir`
- optional `Version`
- optional `IncludeSymbols`

It runs `prepare-release.ps1`, creates `dist/`, finds `ISCC.exe`, and invokes Inno Setup.

Search paths for `ISCC.exe`:

- `C:\Program Files (x86)\Inno Setup 6\ISCC.exe`
- `C:\Program Files\Inno Setup 6\ISCC.exe`

If Inno Setup is missing, it fails with a clear message telling the user to install Inno Setup 6. On success, it prints the final installer path.

## GitHub Actions Release Workflow

`.github/workflows/release.yml` is the only release-publishing workflow.

Trigger:

```yaml
on:
  push:
    tags:
      - 'v*'
```

Permissions:

```yaml
permissions:
  contents: write
```

The workflow runs on Windows and performs:

1. Checkout.
2. Configure CMake for Windows x64 production build.
3. Pass OAuth values from GitHub Actions Secrets:
   - `RIZZYTOS_CLIENT_ID`
   - `RIZZYTOS_CLIENT_SECRET`
4. Keep QtKeychain-related build flags if still needed:
   - `-DBUILD_TRANSLATIONS=OFF`
   - `-DBUILD_SHARED_LIBS=OFF`
5. Build `Release`, including both plugin and `rizzytos-worker`.
6. Download FFmpeg from a configurable workflow variable or environment value.
7. Extract and stage `ffmpeg.exe` and `ffprobe.exe`.
8. Add `THIRD-PARTY-NOTICES.txt` to the plugin data release folder with FFmpeg license/source notes.
9. Install Inno Setup, using Chocolatey when available.
10. Run `scripts/build-installer.ps1`.
11. Create `dist/RizzytosAutoEdit-portable-x64.zip` from `release/`.
12. Generate `dist/checksums.txt` with SHA256 hashes for the installer and ZIP.
13. Upload the installer, ZIP, and checksums as workflow artifacts.
14. Publish the same files to the GitHub Release for the tag.

The workflow must include comments noting that compiling OAuth client values into a distributable binary is not ideal for public distribution, and that this should move to PKCE or a backend OAuth flow before external public releases.

## Existing Push Workflow

`.github/workflows/push.yaml` currently reacts to tags and includes a `create-release` job. The implementation must remove or disable release publishing from `push.yaml`.

Target state:

- `push.yaml` handles normal branch CI only.
- `push.yaml` does not trigger on tags.
- `push.yaml` does not create GitHub Releases.
- `push.yaml` does not upload release assets.
- `release.yml` is the only workflow that handles tags `v*` and release assets.

## Documentation

`docs/release.md` will document:

- local Windows build prerequisites
- how to run CMake locally
- how to run `scripts/prepare-release.ps1`
- how to install Inno Setup and run `scripts/build-installer.ps1`
- expected files in `release/` and `dist/`
- how to create and push a release tag:

```powershell
git tag -a v1.0.0 -m "Release v1.0.0"
git push origin v1.0.0
```

- GitHub Release assets produced
- where to configure `RIZZYTOS_CLIENT_ID` and `RIZZYTOS_CLIENT_SECRET`
- FFmpeg licensing and third-party notices
- the requirement to close OBS before installing or updating

## Error Handling

Required missing files fail fast:

- plugin DLL
- worker executable

Optional missing files warn:

- plugin data directory
- FFmpeg directory when not supplied
- PDBs when `-IncludeSymbols` is set but symbols are unavailable

Supplied but invalid FFmpeg inputs fail because they indicate a packaging configuration error.

## Acceptance Criteria

- `scripts/prepare-release.ps1` creates the correct `release/` structure locally.
- `scripts/build-installer.ps1` generates `dist/RizzytosAutoEdit-Setup-x64.exe` when Inno Setup is installed.
- The installer installs:
  - `rizzytos-auto-edit.dll` in `obs-plugins/64bit/`
  - `rizzytos-worker.exe` in `data/obs-plugins/rizzytos-auto-edit/`
  - `ffmpeg.exe` and `ffprobe.exe` in plugin data when available
  - plugin data directories such as `presets/`, `assets/`, and `locale/`
- The installer cancels when OBS is open.
- Uninstall removes only this plugin's DLL and data folder.
- `.github/workflows/release.yml` builds the production artifacts on tags `v*`.
- `.github/workflows/release.yml` publishes installer, portable ZIP, and checksums to GitHub Releases.
- `.github/workflows/push.yaml` cannot publish releases or release assets.
- `.gitignore` excludes build, release, dist, installer temporaries, and downloaded FFmpeg artifacts.
