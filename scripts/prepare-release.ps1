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

function Resolve-RepoPath {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Path,

        [Parameter(Mandatory = $true)]
        [string] $ProjectRoot
    )

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }

    return (Join-Path $ProjectRoot $Path)
}

function Copy-RequiredFile {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Source,

        [Parameter(Mandatory = $true)]
        [string] $Destination,

        [Parameter(Mandatory = $true)]
        [string] $Description
    )

    $ResolvedSource = Resolve-RequiredPath -Path $Source -Description $Description
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
    Copy-Item -LiteralPath $ResolvedSource -Destination $Destination -Force
    Write-Host "Copied $Description -> $Destination"
}

function Copy-OptionalFile {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Source,

        [Parameter(Mandatory = $true)]
        [string] $Destination,

        [Parameter(Mandatory = $true)]
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

$ProjectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$ResolvedBuildDir = Resolve-RequiredPath -Path (Resolve-RepoPath -Path $BuildDir -ProjectRoot $ProjectRoot) -Description 'Build directory'
$ResolvedReleaseDir = Resolve-RepoPath -Path $ReleaseDir -ProjectRoot $ProjectRoot

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

if (Test-Path -LiteralPath $BuiltDataDir) {
    Get-ChildItem -LiteralPath $BuiltDataDir -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $PluginDataDir -Recurse -Force
    }
    Write-Host "Copied plugin data -> $PluginDataDir"
} else {
    Write-Warning "Plugin data directory not found, continuing without data: $BuiltDataDir"
}

Copy-RequiredFile -Source $WorkerExe -Destination (Join-Path $PluginDataDir 'rizzytos-worker.exe') -Description 'worker executable'

if ($FfmpegDir) {
    $ResolvedFfmpegDir = Resolve-RequiredPath -Path (Resolve-RepoPath -Path $FfmpegDir -ProjectRoot $ProjectRoot) -Description 'FFmpeg directory'
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
