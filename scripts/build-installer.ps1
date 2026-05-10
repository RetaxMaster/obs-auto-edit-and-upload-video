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

$ProjectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$PrepareScript = Join-Path $PSScriptRoot 'prepare-release.ps1'
$DistDir = Join-Path $ProjectRoot 'dist'
$InstallerScript = Join-Path $ProjectRoot 'installer/rizzytos-auto-edit.iss'

$IsccCandidates = @(
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe',
    (Join-Path $env:LOCALAPPDATA 'Programs/Inno Setup 6/ISCC.exe')
)

$Iscc = $IsccCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $Iscc) {
    throw 'ISCC.exe was not found. Install Inno Setup 6 from https://jrsoftware.org/isinfo.php and run this script again.'
}

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

& $Iscc "/DMyAppVersion=$Version" $InstallerScript

if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup failed with exit code $LASTEXITCODE"
}

$InstallerPath = Join-Path $DistDir 'RizzytosAutoEdit-Setup-x64.exe'
if (-not (Test-Path -LiteralPath $InstallerPath)) {
    throw "Installer was not created: $InstallerPath"
}

Write-Host "Installer created: $InstallerPath"
