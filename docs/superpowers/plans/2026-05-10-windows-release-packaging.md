# Windows Release Packaging Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Windows x64 release packaging system for `rizzytos-auto-edit`, including release staging, Inno Setup installer generation, portable ZIP, checksums, and tag-based GitHub Release publishing.

**Architecture:** `prepare-release.ps1` converts CMake outputs into an OBS-compatible `release/` tree. `build-installer.ps1` wraps release preparation and Inno Setup. `.github/workflows/release.yml` owns all `v*` tag release work, while `push.yaml` remains branch CI only.

**Tech Stack:** PowerShell 7, CMake Visual Studio generator, Inno Setup 6, GitHub Actions Windows runner, Chocolatey, softprops/action-gh-release, OBS plugin template CMake presets.

---

## File Map

Create:

- `scripts/prepare-release.ps1` — stages plugin DLL, worker, data files, optional FFmpeg, and optional PDB symbols into `release/`.
- `scripts/build-installer.ps1` — runs release staging, locates `ISCC.exe`, compiles `installer/rizzytos-auto-edit.iss`, and reports the installer path.
- `installer/rizzytos-auto-edit.iss` — Inno Setup installer definition for OBS x64 plugin layout and clean uninstall.
- `.github/workflows/release.yml` — only workflow that runs on tags `v*`, builds production artifacts, and publishes GitHub Releases.
- `docs/release.md` — local and CI release instructions.

Modify:

- `.github/workflows/push.yaml` — remove tag trigger and remove release publishing job.
- `.gitignore` — allow new tracked directories and ignore generated release outputs, installer temporaries, and FFmpeg downloads.

Do not modify:

- CMake target definitions unless the implementation proves the documented build output paths are wrong.
- Existing worker/plugin source files.

---

### Task 1: Make `push.yaml` CI-Only

**Files:**
- Modify: `.github/workflows/push.yaml`

- [ ] **Step 1: Edit push triggers**

Replace the `on.push` section so it only reacts to branches:

```yaml
on:
  push:
    branches:
      - master
      - main
      - 'release/**'
```

Remove the existing `tags:` block from `push.yaml`.

- [ ] **Step 2: Remove the release publishing job**

Delete the entire `create-release:` job from `.github/workflows/push.yaml`, including these release-specific steps:

```yaml
create-release:
  name: Create Release 🛫
```

The remaining jobs should be:

```yaml
jobs:
  check-format:
  build-project:
```

- [ ] **Step 3: Validate no release publication remains**

Run:

```bash
rg -n "create-release|action-gh-release|github.ref_type == 'tag'|tags:|Release 🛫|CHECKSUMS|upload.*release" .github/workflows/push.yaml
```

Expected: no output.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/push.yaml
git commit -m "ci: keep push workflow branch-only"
```

---

### Task 2: Update `.gitignore` for Packaging Files

**Files:**
- Modify: `.gitignore`

- [ ] **Step 1: Allow tracked packaging directories**

Add these exceptions near the existing tracked directory exceptions:

```gitignore
!/installer
!/scripts
```

- [ ] **Step 2: Ignore generated build and packaging outputs**

Add this generated-output block near the bottom:

```gitignore

# Generated build and release outputs
/build/
/build_*/
/release/
/dist/
/installer/output/
/installer/*.tmp
/installer/*.log

# Downloaded third-party binaries and archives
/ffmpeg/
/ffmpeg-*/
/ffmpeg*.zip
/ffmpeg*.7z
```

- [ ] **Step 3: Validate ignore rules**

Run:

```bash
git check-ignore -v build/ release/ dist/ ffmpeg.zip || true
git check-ignore -v scripts/prepare-release.ps1 installer/rizzytos-auto-edit.iss && exit 1 || true
```

Expected: generated paths are ignored; tracked script/installer paths are not ignored.

- [ ] **Step 4: Commit**

```bash
git add .gitignore
git commit -m "chore: ignore release packaging outputs"
```

---

### Task 3: Add `prepare-release.ps1`

**Files:**
- Create: `scripts/prepare-release.ps1`

- [ ] **Step 1: Create the script with strict parameters**

Create `scripts/prepare-release.ps1` with:

```powershell
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $BuildDir,

    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Config = 'Release',

    [string] $ReleaseDir = 'release',

    [string] $FfmpegDir,

    [switch] $IncludeSymbols
)

$ErrorActionPreference = 'Stop'

function Resolve-RequiredPath {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Path,
        [Parameter(Mandatory = $true)]
        [string] $Description
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Description not found: $Path"
    }

    return (Resolve-Path -LiteralPath $Path).Path
}

function Copy-RequiredFile {
    param(
        [string] $Source,
        [string] $Destination,
        [string] $Description
    )

    $ResolvedSource = Resolve-RequiredPath -Path $Source -Description $Description
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    Copy-Item -LiteralPath $ResolvedSource -Destination $Destination -Force
    Write-Host "Copied $Description -> $Destination"
}

function Copy-OptionalFile {
    param(
        [string] $Source,
        [string] $Destination,
        [string] $Description
    )

    if (Test-Path -LiteralPath $Source) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
        Copy-Item -LiteralPath $Source -Destination $Destination -Force
        Write-Host "Copied $Description -> $Destination"
    } else {
        Write-Warning "$Description not found, skipping: $Source"
    }
}
```

- [ ] **Step 2: Add release layout creation and required copies**

Add:

```powershell
$ProjectRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')
$ResolvedBuildDir = Resolve-RequiredPath -Path $BuildDir -Description 'Build directory'

if ([System.IO.Path]::IsPathRooted($ReleaseDir)) {
    $ResolvedReleaseDir = $ReleaseDir
} else {
    $ResolvedReleaseDir = Join-Path $ProjectRoot $ReleaseDir
}

if (Test-Path -LiteralPath $ResolvedReleaseDir) {
    Remove-Item -LiteralPath $ResolvedReleaseDir -Recurse -Force
}

$PluginBinDir = Join-Path $ResolvedReleaseDir 'obs-plugins/64bit'
$PluginDataDir = Join-Path $ResolvedReleaseDir 'data/obs-plugins/rizzytos-auto-edit'
New-Item -ItemType Directory -Force -Path $PluginBinDir, $PluginDataDir | Out-Null

$PluginDll = Join-Path $ResolvedBuildDir "rundir/$Config/rizzytos-auto-edit.dll"
$WorkerExe = Join-Path $ResolvedBuildDir "$Config/rizzytos-worker.exe"
$BuiltDataDir = Join-Path $ResolvedBuildDir "rundir/$Config/rizzytos-auto-edit"

Copy-RequiredFile -Source $PluginDll -Destination (Join-Path $PluginBinDir 'rizzytos-auto-edit.dll') -Description 'plugin DLL'
Copy-RequiredFile -Source $WorkerExe -Destination (Join-Path $PluginDataDir 'rizzytos-worker.exe') -Description 'worker executable'

if (Test-Path -LiteralPath $BuiltDataDir) {
    Copy-Item -LiteralPath (Join-Path $BuiltDataDir '*') -Destination $PluginDataDir -Recurse -Force
    Write-Host "Copied plugin data -> $PluginDataDir"
} else {
    Write-Warning "Plugin data directory not found, continuing without data: $BuiltDataDir"
}
```

- [ ] **Step 3: Add FFmpeg and symbol handling**

Add:

```powershell
if ($FfmpegDir) {
    $ResolvedFfmpegDir = Resolve-RequiredPath -Path $FfmpegDir -Description 'FFmpeg directory'
    Copy-RequiredFile -Source (Join-Path $ResolvedFfmpegDir 'ffmpeg.exe') -Destination (Join-Path $PluginDataDir 'ffmpeg.exe') -Description 'ffmpeg.exe'
    Copy-RequiredFile -Source (Join-Path $ResolvedFfmpegDir 'ffprobe.exe') -Destination (Join-Path $PluginDataDir 'ffprobe.exe') -Description 'ffprobe.exe'
} else {
    Write-Warning 'FfmpegDir was not provided. ffmpeg.exe and ffprobe.exe will not be included.'
}

if ($IncludeSymbols) {
    Copy-OptionalFile -Source (Join-Path $ResolvedBuildDir "rundir/$Config/rizzytos-auto-edit.pdb") -Destination (Join-Path $PluginBinDir 'rizzytos-auto-edit.pdb') -Description 'plugin PDB'
    Copy-OptionalFile -Source (Join-Path $ResolvedBuildDir "$Config/rizzytos-worker.pdb") -Destination (Join-Path $PluginDataDir 'rizzytos-worker.pdb') -Description 'worker PDB'
}

Write-Host "Release prepared at: $ResolvedReleaseDir"
```

- [ ] **Step 4: Run PowerShell parser check**

Run:

```bash
pwsh -NoProfile -Command '$null = [System.Management.Automation.Language.Parser]::ParseFile("scripts/prepare-release.ps1", [ref]$null, [ref]$errors); if ($errors.Count) { $errors | Format-List; exit 1 }'
```

Expected: exit code `0`.

- [ ] **Step 5: Commit**

```bash
git add scripts/prepare-release.ps1
git commit -m "build: add release staging script"
```

---

### Task 4: Add Inno Setup Installer Script

**Files:**
- Create: `installer/rizzytos-auto-edit.iss`

- [ ] **Step 1: Create installer metadata and setup section**

Create `installer/rizzytos-auto-edit.iss` with:

```ini
#define MyAppName "Rizzytos Auto Edit"
#define MyAppPublisher "RetaxMaster"
#define MyAppVersion GetEnv("RIZZYTOS_VERSION")
#if MyAppVersion == ""
#define MyAppVersion "1.0.0"
#endif

[Setup]
AppId={{9D1A4BC2-54F1-4D7E-9D9A-9F6F441AF7D1}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
UninstallDisplayName={#MyAppName}
DefaultDirName={pf}\obs-studio
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=RizzytosAutoEdit-Setup-x64
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
WizardStyle=modern
```

- [ ] **Step 2: Add files and uninstall cleanup**

Add:

```ini
[Files]
Source: "..\release\obs-plugins\64bit\rizzytos-auto-edit.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
Source: "..\release\data\obs-plugins\rizzytos-auto-edit\*"; DestDir: "{app}\data\obs-plugins\rizzytos-auto-edit"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: files; Name: "{app}\obs-plugins\64bit\rizzytos-auto-edit.dll"
Type: filesandordirs; Name: "{app}\data\obs-plugins\rizzytos-auto-edit"
```

- [ ] **Step 3: Add OBS running check**

Add:

```pascal
[Code]
function IsObsRunning(): Boolean;
var
  WMIService: Variant;
  Processes: Variant;
begin
  Result := False;
  try
    WMIService := GetObject('winmgmts:\\.\root\CIMV2');
    Processes := WMIService.ExecQuery('SELECT ProcessId FROM Win32_Process WHERE Name = "obs64.exe"');
    Result := Processes.Count > 0;
  except
    Result := False;
  end;
end;

function InitializeSetup(): Boolean;
begin
  if IsObsRunning() then begin
    MsgBox('OBS Studio is currently running. Close OBS before installing or updating Rizzytos Auto Edit.', mbError, MB_OK);
    Result := False;
  end else begin
    Result := True;
  end;
end;
```

- [ ] **Step 4: Commit**

```bash
git add installer/rizzytos-auto-edit.iss
git commit -m "build: add windows installer script"
```

---

### Task 5: Add `build-installer.ps1`

**Files:**
- Create: `scripts/build-installer.ps1`

- [ ] **Step 1: Create wrapper parameters and ISCC lookup**

Create `scripts/build-installer.ps1` with:

```powershell
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $BuildDir,

    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Config = 'Release',

    [string] $FfmpegDir,

    [string] $Version = '1.0.0',

    [switch] $IncludeSymbols
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')
$PrepareScript = Join-Path $PSScriptRoot 'prepare-release.ps1'
$DistDir = Join-Path $ProjectRoot 'dist'
$InstallerScript = Join-Path $ProjectRoot 'installer/rizzytos-auto-edit.iss'

$IsccCandidates = @(
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe'
)

$Iscc = $IsccCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $Iscc) {
    throw 'ISCC.exe was not found. Install Inno Setup 6 from https://jrsoftware.org/isinfo.php and run this script again.'
}
```

- [ ] **Step 2: Run release preparation and compile installer**

Add:

```powershell
$PrepareArgs = @{
    BuildDir = $BuildDir
    Config = $Config
    ReleaseDir = 'release'
}

if ($FfmpegDir) {
    $PrepareArgs.FfmpegDir = $FfmpegDir
}

if ($IncludeSymbols) {
    $PrepareArgs.IncludeSymbols = $true
}

& $PrepareScript @PrepareArgs

New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

$env:RIZZYTOS_VERSION = $Version
& $Iscc $InstallerScript

if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup failed with exit code $LASTEXITCODE"
}

$InstallerPath = Join-Path $DistDir 'RizzytosAutoEdit-Setup-x64.exe'
if (-not (Test-Path -LiteralPath $InstallerPath)) {
    throw "Installer was not created: $InstallerPath"
}

Write-Host "Installer created: $InstallerPath"
```

- [ ] **Step 3: Run parser check**

Run:

```bash
pwsh -NoProfile -Command '$null = [System.Management.Automation.Language.Parser]::ParseFile("scripts/build-installer.ps1", [ref]$null, [ref]$errors); if ($errors.Count) { $errors | Format-List; exit 1 }'
```

Expected: exit code `0`.

- [ ] **Step 4: Commit**

```bash
git add scripts/build-installer.ps1
git commit -m "build: add installer build wrapper"
```

---

### Task 6: Add Dedicated Release Workflow

**Files:**
- Create: `.github/workflows/release.yml`

- [ ] **Step 1: Add trigger, permissions, and defaults**

Create `.github/workflows/release.yml` with:

```yaml
name: Release

on:
  push:
    tags:
      - 'v*'

permissions:
  contents: write

env:
  FFMPEG_URL: https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip

jobs:
  windows-release:
    name: Windows x64 Release
    runs-on: windows-2022
    defaults:
      run:
        shell: pwsh
```

- [ ] **Step 2: Add checkout and CMake configure**

Add these steps:

```yaml
    steps:
      - name: Checkout
        uses: actions/checkout@v4
        with:
          submodules: recursive
          fetch-depth: 0

      - name: Configure CMake
        env:
          RIZZYTOS_CLIENT_ID: ${{ secrets.RIZZYTOS_CLIENT_ID }}
          RIZZYTOS_CLIENT_SECRET: ${{ secrets.RIZZYTOS_CLIENT_SECRET }}
        run: |
          # These OAuth values are compiled into the binary for now.
          # That is not ideal for public distribution; migrate to PKCE or a backend OAuth flow before external public releases.
          cmake --preset windows-ci-x64 `
            -DRIZZYTOS_CLIENT_ID="$env:RIZZYTOS_CLIENT_ID" `
            -DRIZZYTOS_CLIENT_SECRET="$env:RIZZYTOS_CLIENT_SECRET" `
            -DBUILD_TRANSLATIONS=OFF `
            -DBUILD_SHARED_LIBS=OFF
```

- [ ] **Step 3: Add build, FFmpeg download, and notices**

Add:

```yaml
      - name: Build Release
        run: cmake --build --preset windows-x64 --config Release --parallel -- /consoleLoggerParameters:Summary /noLogo

      - name: Download FFmpeg
        run: |
          $DownloadDir = Join-Path $env:RUNNER_TEMP 'ffmpeg-download'
          $ExtractDir = Join-Path $env:RUNNER_TEMP 'ffmpeg-extract'
          New-Item -ItemType Directory -Force -Path $DownloadDir, $ExtractDir | Out-Null
          $ZipPath = Join-Path $DownloadDir 'ffmpeg.zip'
          Invoke-WebRequest -Uri $env:FFMPEG_URL -OutFile $ZipPath -UseBasicParsing
          Expand-Archive -Path $ZipPath -DestinationPath $ExtractDir -Force
          $FfmpegExe = Get-ChildItem -Path $ExtractDir -Recurse -Filter ffmpeg.exe | Select-Object -First 1
          $FfprobeExe = Get-ChildItem -Path $ExtractDir -Recurse -Filter ffprobe.exe | Select-Object -First 1
          if (-not $FfmpegExe -or -not $FfprobeExe) {
            throw 'Downloaded FFmpeg archive did not contain ffmpeg.exe and ffprobe.exe'
          }
          $StageDir = Join-Path $env:RUNNER_TEMP 'ffmpeg-stage'
          New-Item -ItemType Directory -Force -Path $StageDir | Out-Null
          Copy-Item $FfmpegExe.FullName (Join-Path $StageDir 'ffmpeg.exe') -Force
          Copy-Item $FfprobeExe.FullName (Join-Path $StageDir 'ffprobe.exe') -Force
          "FFMPEG_DIR=$StageDir" >> $env:GITHUB_ENV

      - name: Create third-party notices
        run: |
          @'
          Rizzytos Auto Edit bundles FFmpeg executables for video processing.

          FFmpeg project: https://ffmpeg.org/
          Windows build source: ${{ env.FFMPEG_URL }}

          Review the license terms of the selected FFmpeg build before public distribution.
          '@ | Set-Content -Path THIRD-PARTY-NOTICES.txt -Encoding UTF8
          $BuiltDataDir = 'build_x64/rundir/Release/rizzytos-auto-edit'
          New-Item -ItemType Directory -Force -Path $BuiltDataDir | Out-Null
          Copy-Item THIRD-PARTY-NOTICES.txt (Join-Path $BuiltDataDir 'THIRD-PARTY-NOTICES.txt') -Force
```

- [ ] **Step 4: Add Inno install, installer build, ZIP, checksums, artifacts, and release publish**

Add:

```yaml
      - name: Install Inno Setup
        run: choco install innosetup --no-progress -y

      - name: Build installer
        run: |
          $Version = "${{ github.ref_name }}".TrimStart('v')
          scripts/build-installer.ps1 -BuildDir build_x64 -Config Release -FfmpegDir "$env:FFMPEG_DIR" -Version $Version

      - name: Create portable ZIP
        run: Compress-Archive -Path release/* -DestinationPath dist/RizzytosAutoEdit-portable-x64.zip -Force

      - name: Generate checksums
        run: |
          Get-FileHash dist/RizzytosAutoEdit-Setup-x64.exe, dist/RizzytosAutoEdit-portable-x64.zip -Algorithm SHA256 |
            ForEach-Object { "$($_.Hash)  $(Split-Path -Leaf $_.Path)" } |
            Set-Content -Path dist/checksums.txt -Encoding ASCII

      - name: Upload artifacts
        uses: actions/upload-artifact@v4
        with:
          name: rizzytos-auto-edit-windows-x64-${{ github.ref_name }}
          path: |
            dist/RizzytosAutoEdit-Setup-x64.exe
            dist/RizzytosAutoEdit-portable-x64.zip
            dist/checksums.txt

      - name: Publish GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          tag_name: ${{ github.ref_name }}
          name: Rizzytos Auto Edit ${{ github.ref_name }}
          files: |
            dist/RizzytosAutoEdit-Setup-x64.exe
            dist/RizzytosAutoEdit-portable-x64.zip
            dist/checksums.txt
```

- [ ] **Step 5: Validate release workflow ownership**

Run:

```bash
rg -n "tags:|action-gh-release|RizzytosAutoEdit|checksums|FFMPEG_URL" .github/workflows
```

Expected: tag trigger and release publishing appear only in `.github/workflows/release.yml`.

- [ ] **Step 6: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci: add dedicated windows release workflow"
```

---

### Task 7: Add Release Documentation

**Files:**
- Create: `docs/release.md`

- [ ] **Step 1: Document local build and release staging**

Create `docs/release.md` with sections:

```markdown
# Release Guide

## Local Windows Build

Prerequisites:

- Windows x64
- Visual Studio 2022 with C++ workload
- CMake 3.28+
- PowerShell 7
- Inno Setup 6
- FFmpeg Windows binaries if you want local packages to include `ffmpeg.exe` and `ffprobe.exe`

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
```

- [ ] **Step 2: Document installer, tag release, secrets, and FFmpeg notices**

Append:

```markdown
## Build Installer

```powershell
scripts/build-installer.ps1 -BuildDir build_x64 -Config Release -FfmpegDir C:\path\to\ffmpeg\bin -Version 1.0.0
```

Output:

- `dist/RizzytosAutoEdit-Setup-x64.exe`

## Create a GitHub Release

```powershell
git tag -a v1.0.0 -m "Release v1.0.0"
git push origin v1.0.0
```

The `Release` workflow uploads:

- `RizzytosAutoEdit-Setup-x64.exe`
- `RizzytosAutoEdit-portable-x64.zip`
- `checksums.txt`

## GitHub Secrets

Configure repository secrets in GitHub under Settings > Secrets and variables > Actions:

- `RIZZYTOS_CLIENT_ID`
- `RIZZYTOS_CLIENT_SECRET`

These values are currently compiled into the binary. That is acceptable for internal testing, but public distribution should move to PKCE or a backend OAuth flow.

## FFmpeg and Licenses

Release builds include `ffmpeg.exe` and `ffprobe.exe` when available. Verify the license of the selected FFmpeg build before distributing artifacts. Keep `THIRD-PARTY-NOTICES.txt` with the release assets.

## Installing or Updating

Close OBS Studio before installing, updating, or uninstalling. The installer blocks installation when `obs64.exe` is running.
```

- [ ] **Step 3: Commit**

```bash
git add docs/release.md
git commit -m "docs: add windows release guide"
```

---

### Task 8: End-to-End Validation

**Files:**
- Read: `.github/workflows/push.yaml`
- Read: `.github/workflows/release.yml`
- Read: `scripts/prepare-release.ps1`
- Read: `scripts/build-installer.ps1`
- Read: `installer/rizzytos-auto-edit.iss`
- Read: `docs/release.md`

- [ ] **Step 1: Validate YAML syntax with available tooling**

Run:

```bash
ruby -e 'require "yaml"; Dir[".github/workflows/*.y{a,}ml"].each { |f| YAML.load_file(f); puts f }'
```

Expected: each workflow file path prints with no exception.

- [ ] **Step 2: Validate PowerShell syntax**

Run:

```bash
pwsh -NoProfile -Command '$files = "scripts/prepare-release.ps1","scripts/build-installer.ps1"; foreach ($file in $files) { $null = [System.Management.Automation.Language.Parser]::ParseFile($file, [ref]$null, [ref]$errors); if ($errors.Count) { $errors | Format-List; exit 1 }; Write-Host "$file OK" }'
```

Expected:

```text
scripts/prepare-release.ps1 OK
scripts/build-installer.ps1 OK
```

- [ ] **Step 3: Test release staging with fake build outputs**

Run:

```bash
tmp="$(mktemp -d)"
mkdir -p "$tmp/build/rundir/Release/rizzytos-auto-edit" "$tmp/build/Release" "$tmp/ffmpeg"
touch "$tmp/build/rundir/Release/rizzytos-auto-edit.dll"
touch "$tmp/build/rundir/Release/rizzytos-auto-edit/example.dat"
touch "$tmp/build/Release/rizzytos-worker.exe"
touch "$tmp/ffmpeg/ffmpeg.exe" "$tmp/ffmpeg/ffprobe.exe"
pwsh -NoProfile -File scripts/prepare-release.ps1 -BuildDir "$tmp/build" -Config Release -ReleaseDir "$tmp/release" -FfmpegDir "$tmp/ffmpeg"
test -f "$tmp/release/obs-plugins/64bit/rizzytos-auto-edit.dll"
test -f "$tmp/release/data/obs-plugins/rizzytos-auto-edit/rizzytos-worker.exe"
test -f "$tmp/release/data/obs-plugins/rizzytos-auto-edit/ffmpeg.exe"
test -f "$tmp/release/data/obs-plugins/rizzytos-auto-edit/ffprobe.exe"
test -f "$tmp/release/data/obs-plugins/rizzytos-auto-edit/example.dat"
```

Expected: exit code `0`.

- [ ] **Step 4: Validate no PDBs copy by default**

Run:

```bash
tmp="$(mktemp -d)"
mkdir -p "$tmp/build/rundir/Release/rizzytos-auto-edit" "$tmp/build/Release"
touch "$tmp/build/rundir/Release/rizzytos-auto-edit.dll"
touch "$tmp/build/rundir/Release/rizzytos-auto-edit.pdb"
touch "$tmp/build/Release/rizzytos-worker.exe"
touch "$tmp/build/Release/rizzytos-worker.pdb"
pwsh -NoProfile -File scripts/prepare-release.ps1 -BuildDir "$tmp/build" -Config Release -ReleaseDir "$tmp/release"
test ! -f "$tmp/release/obs-plugins/64bit/rizzytos-auto-edit.pdb"
test ! -f "$tmp/release/data/obs-plugins/rizzytos-auto-edit/rizzytos-worker.pdb"
```

Expected: exit code `0`.

- [ ] **Step 5: Validate PDBs copy with `-IncludeSymbols`**

Run:

```bash
tmp="$(mktemp -d)"
mkdir -p "$tmp/build/rundir/Release/rizzytos-auto-edit" "$tmp/build/Release"
touch "$tmp/build/rundir/Release/rizzytos-auto-edit.dll"
touch "$tmp/build/rundir/Release/rizzytos-auto-edit.pdb"
touch "$tmp/build/Release/rizzytos-worker.exe"
touch "$tmp/build/Release/rizzytos-worker.pdb"
pwsh -NoProfile -File scripts/prepare-release.ps1 -BuildDir "$tmp/build" -Config Release -ReleaseDir "$tmp/release" -IncludeSymbols
test -f "$tmp/release/obs-plugins/64bit/rizzytos-auto-edit.pdb"
test -f "$tmp/release/data/obs-plugins/rizzytos-auto-edit/rizzytos-worker.pdb"
```

Expected: exit code `0`.

- [ ] **Step 6: Validate workflow separation**

Run:

```bash
rg -n "tags:|action-gh-release|create-release|checksums|RizzytosAutoEdit-Setup" .github/workflows/push.yaml && exit 1 || true
rg -n "tags:|action-gh-release|checksums|RizzytosAutoEdit-Setup" .github/workflows/release.yml
```

Expected: first command has no output; second command shows release-only lines.

- [ ] **Step 7: Commit validation fixes**

If validation required fixes, commit them:

```bash
git add .github/workflows/push.yaml .github/workflows/release.yml .gitignore scripts/prepare-release.ps1 scripts/build-installer.ps1 installer/rizzytos-auto-edit.iss docs/release.md
git commit -m "fix: complete windows release packaging validation"
```

If no files changed, do not create an empty commit.
