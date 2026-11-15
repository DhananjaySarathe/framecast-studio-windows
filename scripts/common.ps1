$ErrorActionPreference = "Stop"
function Invoke-Checked {
    param([Parameter(Mandatory=$true)][string]$File, [string[]]$Arguments)
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File failed with exit code $LASTEXITCODE" }
}
function Assert-Windows {
    if ($env:OS -ne "Windows_NT") { throw "Run this command on Windows 11 Intel/AMD x64." }
    if (![Environment]::Is64BitOperatingSystem) { throw "A 64-bit Windows installation is required." }
}
