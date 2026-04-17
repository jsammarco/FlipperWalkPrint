param(
    [string]$SourceDir = (Split-Path -Parent $MyInvocation.MyCommand.Path),
    [string]$FapPath,
    [string]$MountPath,
    [string]$QFlipperPath,
    [string]$DeviceAppDir = "/ext/apps/Tools",
    [switch]$BuildIfMissing
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Write-Step {
    param([string]$Message)
    Write-Host "[deploy] $Message"
}

function Find-QFlipper {
    $candidates = @(
        $env:QFLIPPER_BIN,
        (Get-Command qFlipper.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue),
        (Get-Command qFlipper -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue),
        "C:\Program Files\qFlipper\qFlipper.exe",
        "C:\Program Files (x86)\qFlipper\qFlipper.exe",
        (Join-Path $env:LOCALAPPDATA "Programs\qFlipper\qFlipper.exe")
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

    foreach($candidate in $candidates) {
        if(Test-Path $candidate -PathType Leaf) {
            return (Resolve-FullPath $candidate)
        }
    }

    return $null
}

$SourceDir = Resolve-FullPath $SourceDir
if([string]::IsNullOrWhiteSpace($FapPath)) {
    $FapPath = Join-Path $SourceDir "dist\walkprint.fap"
} else {
    $FapPath = Resolve-FullPath $FapPath
}

if(!(Test-Path $FapPath -PathType Leaf)) {
    if($BuildIfMissing) {
        Write-Step "Missing $FapPath; invoking build.ps1"
        & (Join-Path $SourceDir "build.ps1")
    } else {
        throw "FAP file not found: $FapPath. Re-run with -BuildIfMissing or build first."
    }
}

if(!(Test-Path $FapPath -PathType Leaf)) {
    throw "FAP file still not found after build attempt: $FapPath"
}

if(-not [string]::IsNullOrWhiteSpace($MountPath)) {
    $MountPath = Resolve-FullPath $MountPath
    if(!(Test-Path $MountPath -PathType Container)) {
        throw "MountPath does not exist or is not a directory: $MountPath"
    }

    $targetDir = Join-Path $MountPath "apps\Tools"
    if(!(Test-Path $targetDir -PathType Container)) {
        New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
    }

    $targetFap = Join-Path $targetDir "WalkPrint.fap"
    Copy-Item -Path $FapPath -Destination $targetFap -Force
    Write-Step "Copied WalkPrint.fap to $targetFap"
    return
}

if([string]::IsNullOrWhiteSpace($QFlipperPath)) {
    $QFlipperPath = Find-QFlipper
} else {
    $QFlipperPath = Resolve-FullPath $QFlipperPath
}

if([string]::IsNullOrWhiteSpace($QFlipperPath)) {
    throw "qFlipper was not found. Pass -MountPath or -QFlipperPath."
}

Write-Step "Using qFlipper at $QFlipperPath"

& $QFlipperPath cli storage mkdir "/ext/apps" | Out-Host
& $QFlipperPath cli storage mkdir $DeviceAppDir | Out-Host
& $QFlipperPath cli storage send $FapPath "$DeviceAppDir/WalkPrint.fap" | Out-Host

if($LASTEXITCODE -ne 0) {
    throw "qFlipper CLI upload failed with exit code $LASTEXITCODE"
}

Write-Step "Uploaded WalkPrint.fap to $DeviceAppDir/WalkPrint.fap"
