param(
    [string]$NameFilter = "walk,yhk,printer,thermal,receipt,cat",
    [switch]$IncludeUnknown,
    [switch]$WriteDefaultAddress,
    [string]$ConfigHeader = (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "walkprint_config.h"),
    [switch]$ListOnly
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Write-Step {
    param([string]$Message)
    Write-Host "[select-printer] $Message"
}

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class WalkPrintBluetoothNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct BLUETOOTH_FIND_RADIO_PARAMS {
        public int dwSize;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct SYSTEMTIME {
        public ushort wYear;
        public ushort wMonth;
        public ushort wDayOfWeek;
        public ushort wDay;
        public ushort wHour;
        public ushort wMinute;
        public ushort wSecond;
        public ushort wMilliseconds;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct BLUETOOTH_DEVICE_INFO {
        public int dwSize;
        public ulong Address;
        public uint ulClassofDevice;
        [MarshalAs(UnmanagedType.Bool)]
        public bool fConnected;
        [MarshalAs(UnmanagedType.Bool)]
        public bool fRemembered;
        [MarshalAs(UnmanagedType.Bool)]
        public bool fAuthenticated;
        public SYSTEMTIME stLastSeen;
        public SYSTEMTIME stLastUsed;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 248)]
        public string szName;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct BLUETOOTH_DEVICE_SEARCH_PARAMS {
        public int dwSize;
        [MarshalAs(UnmanagedType.Bool)]
        public bool fReturnAuthenticated;
        [MarshalAs(UnmanagedType.Bool)]
        public bool fReturnRemembered;
        [MarshalAs(UnmanagedType.Bool)]
        public bool fReturnUnknown;
        [MarshalAs(UnmanagedType.Bool)]
        public bool fReturnConnected;
        [MarshalAs(UnmanagedType.Bool)]
        public bool fIssueInquiry;
        public byte cTimeoutMultiplier;
        public IntPtr hRadio;
    }

    [DllImport("BluetoothAPIs.dll", SetLastError = true)]
    public static extern IntPtr BluetoothFindFirstRadio(
        ref BLUETOOTH_FIND_RADIO_PARAMS pbtfrp,
        out IntPtr phRadio);

    [DllImport("BluetoothAPIs.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool BluetoothFindNextRadio(
        IntPtr hFind,
        out IntPtr phRadio);

    [DllImport("BluetoothAPIs.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool BluetoothFindRadioClose(IntPtr hFind);

    [DllImport("BluetoothAPIs.dll", SetLastError = true)]
    public static extern IntPtr BluetoothFindFirstDevice(
        ref BLUETOOTH_DEVICE_SEARCH_PARAMS searchParams,
        ref BLUETOOTH_DEVICE_INFO deviceInfo);

    [DllImport("BluetoothAPIs.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool BluetoothFindNextDevice(
        IntPtr hFind,
        ref BLUETOOTH_DEVICE_INFO deviceInfo);

    [DllImport("BluetoothAPIs.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool BluetoothFindDeviceClose(IntPtr hFind);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool CloseHandle(IntPtr hObject);
}
"@

function Convert-BluetoothAddress {
    param([UInt64]$Address)

    $bytes = [BitConverter]::GetBytes($Address)
    $pairs = for($i = 5; $i -ge 0; $i--) {
        "{0:X2}" -f $bytes[$i]
    }
    return ($pairs -join ":")
}

function Get-DiscoveredBluetoothDevices {
    $radioParams = New-Object WalkPrintBluetoothNative+BLUETOOTH_FIND_RADIO_PARAMS
    $radioParams.dwSize = [Runtime.InteropServices.Marshal]::SizeOf([type]([WalkPrintBluetoothNative+BLUETOOTH_FIND_RADIO_PARAMS]))

    $radioHandle = [IntPtr]::Zero
    $radioFind = [WalkPrintBluetoothNative]::BluetoothFindFirstRadio([ref]$radioParams, [ref]$radioHandle)

    if($radioFind -eq [IntPtr]::Zero) {
        throw "No Bluetooth radio was found on this Windows machine."
    }

    $devices = New-Object System.Collections.Generic.List[object]

    try {
        do {
            $search = New-Object WalkPrintBluetoothNative+BLUETOOTH_DEVICE_SEARCH_PARAMS
            $search.dwSize = [Runtime.InteropServices.Marshal]::SizeOf([type]([WalkPrintBluetoothNative+BLUETOOTH_DEVICE_SEARCH_PARAMS]))
            $search.fReturnAuthenticated = $true
            $search.fReturnRemembered = $true
            $search.fReturnConnected = $true
            $search.fReturnUnknown = [bool]$IncludeUnknown
            $search.fIssueInquiry = $true
            $search.cTimeoutMultiplier = 2
            $search.hRadio = $radioHandle

            $info = New-Object WalkPrintBluetoothNative+BLUETOOTH_DEVICE_INFO
            $info.dwSize = [Runtime.InteropServices.Marshal]::SizeOf([type]([WalkPrintBluetoothNative+BLUETOOTH_DEVICE_INFO]))

            $deviceFind = [WalkPrintBluetoothNative]::BluetoothFindFirstDevice([ref]$search, [ref]$info)
            if($deviceFind -ne [IntPtr]::Zero) {
                try {
                    do {
                        $devices.Add([pscustomobject]@{
                                Name          = $info.szName
                                Address       = (Convert-BluetoothAddress $info.Address)
                                Connected     = $info.fConnected
                                Remembered    = $info.fRemembered
                                Authenticated = $info.fAuthenticated
                                ClassOfDevice = ("0x{0:X6}" -f $info.ulClassofDevice)
                            })
                    } while([WalkPrintBluetoothNative]::BluetoothFindNextDevice($deviceFind, [ref]$info))
                } finally {
                    [void][WalkPrintBluetoothNative]::BluetoothFindDeviceClose($deviceFind)
                }
            }
        } while([WalkPrintBluetoothNative]::BluetoothFindNextRadio($radioFind, [ref]$radioHandle))
    } finally {
        if($radioHandle -ne [IntPtr]::Zero) {
            [void][WalkPrintBluetoothNative]::CloseHandle($radioHandle)
        }
        [void][WalkPrintBluetoothNative]::BluetoothFindRadioClose($radioFind)
    }

    return $devices |
        Sort-Object Address -Unique
}

function Filter-PrinterCandidates {
    param(
        [Parameter(Mandatory = $true)][object[]]$Devices,
        [Parameter(Mandatory = $true)][string[]]$Keywords
    )

    $filtered = foreach($device in $Devices) {
        $name = [string]$device.Name
        if([string]::IsNullOrWhiteSpace($name)) {
            continue
        }

        foreach($keyword in $Keywords) {
            if($name.IndexOf($keyword, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
                $device
                break
            }
        }
    }

    if($filtered) {
        return $filtered
    }

    return $Devices
}

function Update-DefaultPrinterAddress {
    param(
        [Parameter(Mandatory = $true)][string]$HeaderPath,
        [Parameter(Mandatory = $true)][string]$Address
    )

    if(!(Test-Path $HeaderPath -PathType Leaf)) {
        throw "Config header not found: $HeaderPath"
    }

    $content = Get-Content $HeaderPath -Raw
    $pattern = '(#define\s+WALKPRINT_DEFAULT_PRINTER_ADDRESS\s+)"[^"]*"'
    if($content -notmatch $pattern) {
        throw "Could not find WALKPRINT_DEFAULT_PRINTER_ADDRESS in $HeaderPath"
    }

    $updated = [regex]::Replace($content, $pattern, ('$1"{0}"' -f $Address), 1)
    Set-Content -Path $HeaderPath -Value $updated -NoNewline
}

$keywords = $NameFilter.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ }
Write-Step "Scanning Windows Bluetooth devices"
$devices = Get-DiscoveredBluetoothDevices

if(-not $devices -or $devices.Count -eq 0) {
    throw "No Bluetooth devices were discovered."
}

$candidates = Filter-PrinterCandidates -Devices $devices -Keywords $keywords

Write-Host ""
Write-Host "Discovered devices:"
$index = 1
foreach($device in $candidates) {
    $flags = @()
    if($device.Connected) { $flags += "connected" }
    if($device.Remembered) { $flags += "remembered" }
    if($device.Authenticated) { $flags += "paired" }
    $flagText = if($flags.Count -gt 0) { $flags -join ", " } else { "unpaired" }

    Write-Host ("[{0}] {1}  {2}  ({3})" -f $index, $device.Name, $device.Address, $flagText)
    $index++
}

if($ListOnly) {
    return
}

$selection = Read-Host "Enter the printer number to use for WalkPrint"
if(-not ($selection -as [int])) {
    throw "Invalid selection: $selection"
}

$selectedIndex = [int]$selection
if($selectedIndex -lt 1 -or $selectedIndex -gt $candidates.Count) {
    throw "Selection out of range: $selectedIndex"
}

$chosen = $candidates[$selectedIndex - 1]
Write-Step ("Selected {0} at {1}" -f $chosen.Name, $chosen.Address)

if($WriteDefaultAddress) {
    Update-DefaultPrinterAddress -HeaderPath $ConfigHeader -Address $chosen.Address
    Write-Step "Updated WALKPRINT_DEFAULT_PRINTER_ADDRESS in $ConfigHeader"
} else {
    Write-Step "Use this address in the WalkPrint app settings: $($chosen.Address)"
}
