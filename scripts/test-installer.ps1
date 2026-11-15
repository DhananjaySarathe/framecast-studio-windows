param([Parameter(Mandatory=$true)][string]$BuildDir)
$ErrorActionPreference = "Stop"
$build = (Resolve-Path $BuildDir).Path
$setup = Get-ChildItem (Join-Path $build "installer/*-Setup.exe") | Select-Object -First 1
if (!$setup) { throw "Installer not found." }
$temporaryRoot = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [IO.Path]::GetTempPath() }
$installation = Join-Path $temporaryRoot "Framecast Installer Test"
$evidence = Join-Path $build "installer-test"
New-Item -ItemType Directory -Force $evidence | Out-Null
try {
    & (Join-Path $PSScriptRoot "install-windows.ps1") -InstallerPath $setup.FullName -ChecksumPath (Join-Path $build "installer/SHA256SUMS.txt") -InstallDirectory $installation -Silent -LogPath (Join-Path $evidence "install.log")
    python (Join-Path $PSScriptRoot "../tests/installed_smoke.py") --installation $installation --evidence $evidence
    if ($LASTEXITCODE) { throw "Installed app smoke test failed: $LASTEXITCODE" }
} finally {
    $uninstaller = Join-Path $installation "unins000.exe"
    if (Test-Path $uninstaller) {
        $process = Start-Process $uninstaller -ArgumentList @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/LOG=`"$evidence/uninstall.log`"") -PassThru -Wait
        if ($process.ExitCode) { throw "Uninstaller failed: $($process.ExitCode)" }
        if (Test-Path (Join-Path $installation "FramecastStudio.exe")) { throw "Uninstaller left the application executable behind." }
    }
}
Write-Host "PASS installer, installed studio and engine, and uninstall"
