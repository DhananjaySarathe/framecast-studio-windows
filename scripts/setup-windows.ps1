param([string]$DependencyRoot)
. (Join-Path $PSScriptRoot "common.ps1")
Assert-Windows
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (!$DependencyRoot) { $DependencyRoot = Join-Path $repo ".deps" }
New-Item -ItemType Directory -Force $DependencyRoot | Out-Null
$deps = (Resolve-Path $DependencyRoot).Path
foreach ($command in @("git", "cmake", "python")) {
    if (!(Get-Command $command -ErrorAction SilentlyContinue)) { throw "Missing $command. See README.md: Build from source." }
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
if (!(Test-Path $vswhere)) { throw "Install Visual Studio 2022 Build Tools with the C++ desktop workload; see README.md." }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw "The Visual Studio C++ desktop tools were not found. See README.md." }
$venv = Join-Path $deps "python"
$python = Join-Path $venv "Scripts/python.exe"
if (!(Test-Path $python)) { Invoke-Checked -File python -Arguments @("-m", "venv", $venv) }
Invoke-Checked -File $python -Arguments @("-m", "pip", "install", "aqtinstall==3.3.0")
$qtRoot = Join-Path $deps "Qt"
$qt = Join-Path $qtRoot "6.8.3/msvc2022_64"
if (!(Test-Path (Join-Path $qt "bin/windeployqt.exe"))) {
    Invoke-Checked -File $python -Arguments @("-m", "aqt", "install-qt", "windows", "desktop", "6.8.3", "win64_msvc2022_64", "--outputdir", $qtRoot, "--archives", "qtbase")
}
$baseline = "4702e381dbbd9b54d3f7e1eb565cd8981c1814cd"
$vcpkg = Join-Path $deps "vcpkg"
$newCheckout = !(Test-Path (Join-Path $vcpkg ".git"))
if ($newCheckout) {
    # CI may restore only ignored dependency downloads before Git is initialized.
    if ((Test-Path $vcpkg) -and (Get-ChildItem $vcpkg -Force | Where-Object { $_.Name -ne "downloads" } | Select-Object -First 1)) { throw "$vcpkg contains files other than the cached downloads directory." }
    Invoke-Checked -File git -Arguments @("init", $vcpkg)
    Invoke-Checked -File git -Arguments @("-C", $vcpkg, "remote", "add", "origin", "https://github.com/microsoft/vcpkg.git")
}
$origin = & git -C $vcpkg remote get-url origin
if ($LASTEXITCODE -or $origin -ne "https://github.com/microsoft/vcpkg.git") { throw "The dependency checkout must use the official microsoft/vcpkg repository." }
if (!$newCheckout) {
    $dirty = & git -C $vcpkg status --porcelain
    if ($LASTEXITCODE -or $dirty) { throw "The dependency checkout has local changes. Preserve them before running setup." }
}
Invoke-Checked -File git -Arguments @("-C", $vcpkg, "fetch", "--depth=1", "origin", $baseline)
Invoke-Checked -File git -Arguments @("-C", $vcpkg, "checkout", "--detach", $baseline)
if (!(Test-Path (Join-Path $vcpkg "vcpkg.exe"))) {
    Invoke-Checked -File (Join-Path $vcpkg "bootstrap-vcpkg.bat") -Arguments @("-disableMetrics")
}
New-Item -ItemType Directory -Force (Join-Path $repo ".deps") | Out-Null
@{ QtDir = $qt; VcpkgRoot = $vcpkg; Baseline = $baseline } | ConvertTo-Json | Set-Content (Join-Path $repo ".deps/toolchain.json") -Encoding utf8
Write-Host "Setup complete. Run .\build.cmd -Test -Package"
