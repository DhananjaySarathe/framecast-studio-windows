param([string]$BuildDir = "build/windows", [string]$ReleaseVersion)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $BuildDir | Out-Null
$build = (Resolve-Path $BuildDir).Path
$temporaryRoot = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [IO.Path]::GetTempPath() }
$id = [Guid]::NewGuid().ToString("N")
$installation = Join-Path $temporaryRoot "Framecast Installer Test $id"
$evidence = Join-Path $build "installer-test/$id"
New-Item -ItemType Directory -Force $evidence | Out-Null
$helper = Join-Path $PSScriptRoot "install-windows.ps1"
try {
    if ($ReleaseVersion) {
        & (Join-Path $PSScriptRoot "../install.cmd") -Version $ReleaseVersion -InstallDirectory $installation -Silent -LogPath (Join-Path $evidence "install.log")
        if ($LASTEXITCODE) { throw "Release install command failed: $LASTEXITCODE" }
    } else {
        $setup = Get-ChildItem (Join-Path $build "installer/*-Setup.exe") | Select-Object -First 1
        if (!$setup) { throw "Installer not found." }
        $badChecksum = Join-Path $evidence "wrong-SHA256SUMS.txt"
        (('0' * 64) + '  ' + $setup.Name) | Set-Content $badChecksum -Encoding ascii
        $rejected = $false
        try { & $helper -InstallerPath $setup.FullName -ChecksumPath $badChecksum -InstallDirectory $installation -Silent }
        catch { if ($_.Exception.Message -notlike "*checksum mismatch*") { throw }; $rejected = $true }
        if (!$rejected -or (Test-Path (Join-Path $installation "FramecastStudio.exe"))) { throw "An invalid installer checksum was not rejected before execution." }
        Write-Host "PASS altered installer checksum is rejected before execution"
        & $helper -InstallerPath $setup.FullName -ChecksumPath (Join-Path $build "installer/SHA256SUMS.txt") -InstallDirectory $installation -Silent -LogPath (Join-Path $evidence "install.log")
    }
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
Write-Host "PASS installer, installed studio and engine, and uninstall; evidence: $evidence"
