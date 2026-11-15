param(
    [ValidatePattern('^\d+\.\d+\.\d+([.-][0-9A-Za-z.-]+)?$')][string]$Version = "0.1.2",
    [switch]$Silent,
    [string]$InstallerPath,
    [string]$ChecksumPath,
    [string]$InstallDirectory,
    [string]$LogPath
)
. (Join-Path $PSScriptRoot "common.ps1")
Assert-Windows
$ProgressPreference = "SilentlyContinue"
if ([Environment]::OSVersion.Version.Build -lt 22000) { throw "Windows 11 or later is required." }
if (!$InstallerPath) {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $download = Join-Path $env:LOCALAPPDATA "Framecast Studio/Downloads/$Version"
    New-Item -ItemType Directory -Force $download | Out-Null
    $name = "Framecast-Studio-Windows-$Version-x64-Setup.exe"
    $base = "https://github.com/DhananjaySarathe/framecast-studio-windows/releases/download/v$Version"
    $InstallerPath = Join-Path $download $name
    $ChecksumPath = Join-Path $download "SHA256SUMS.txt"
    Invoke-WebRequest -UseBasicParsing "$base/$name" -OutFile $InstallerPath
    Invoke-WebRequest -UseBasicParsing "$base/SHA256SUMS.txt" -OutFile $ChecksumPath
}
if (!$ChecksumPath) { $ChecksumPath = Join-Path (Split-Path -Parent $InstallerPath) "SHA256SUMS.txt" }
$InstallerPath = (Resolve-Path $InstallerPath).Path
$name = [IO.Path]::GetFileName($InstallerPath)
$expected = $null
foreach ($line in Get-Content $ChecksumPath) {
    if ($line -match '^(?<hash>[0-9a-fA-F]{64})\s+\*?(?<file>.+)$' -and $Matches.file -eq $name) { $expected = $Matches.hash; break }
}
if (!$expected) { throw "No checksum found for $name." }
$actual = (Get-FileHash $InstallerPath -Algorithm SHA256).Hash
if ($actual -ne $expected) { throw "Installer checksum mismatch. The installer has not been run." }
$arguments = @("/NORESTART", "/SP-")
if ($Silent) { $arguments += @("/VERYSILENT", "/SUPPRESSMSGBOXES") }
if ($InstallDirectory) { $arguments += "/DIR=`"$InstallDirectory`"" }
if ($LogPath) { $arguments += "/LOG=`"$LogPath`"" }
$process = Start-Process $InstallerPath -ArgumentList $arguments -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Installer exited with code $($process.ExitCode)." }
Write-Host "Framecast Studio is installed. Open it from the Start menu."
