[CmdletBinding()]
param(
    [string]$Python = "python",
    [string]$Address = "25:00:35:00:03:57",
    [Nullable[int]]$Channel = $null,
    [int]$Width = 384,
    [switch]$BmpOnly,
    [ValidateSet("floyd-steinberg", "threshold")]
    [string]$Dither = "floyd-steinberg",
    [int]$Threshold = 160,
    [int]$FeedLines = 3,
    [string]$PreviewPath = ""
)

Add-Type -AssemblyName System.Windows.Forms

$dialog = New-Object System.Windows.Forms.OpenFileDialog
$dialog.Title = if($BmpOnly) { "Select a BMP image to print" } else { "Select an image to print" }
$dialog.Filter = if($BmpOnly) {
    "Bitmap Files|*.bmp|All Files|*.*"
} else {
    "Image Files|*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp|All Files|*.*"
}
$dialog.Multiselect = $false

if($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
    Write-Host "No image selected."
    exit 1
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$bridgeScript = Join-Path $scriptDir "walkprint_bridge.py"

$arguments = @(
    $bridgeScript,
    "image",
    $dialog.FileName,
    "--address", $Address,
    "--width", $Width,
    "--dither", $Dither,
    "--threshold", $Threshold,
    "--feed-lines", $FeedLines
)

if($Channel -ne $null) {
    $arguments += @("--channel", $Channel)
}

if($PreviewPath -and $PreviewPath.Trim().Length -gt 0) {
    $arguments += @("--preview", $PreviewPath)
}

& $Python @arguments
exit $LASTEXITCODE
