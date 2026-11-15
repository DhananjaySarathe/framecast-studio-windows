param(
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [Parameter(Mandatory=$true)][string]$QtDir,
    [string]$Version = "0.1.3"
)
$ErrorActionPreference = "Stop"
$build = (Resolve-Path $BuildDir).Path
$qt = (Resolve-Path $QtDir).Path
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$package = Join-Path $build "package"
if (Test-Path $package) { Remove-Item -Recurse -Force $package }
New-Item -ItemType Directory -Force $package | Out-Null
foreach ($name in @("FramecastStudio.exe", "framecast-engine.exe")) {
    Copy-Item (Join-Path $build "Release/$name") $package
}
Copy-Item (Join-Path $build "Release/*.dll") $package
& (Join-Path $qt "bin/windeployqt.exe") --release --no-translations --no-compiler-runtime (Join-Path $package "FramecastStudio.exe")
if ($LASTEXITCODE) { throw "Qt deployment failed: $LASTEXITCODE" }
# Deploy the MSVC CRT beside the executables so a per-user install needs no elevated
# redistributable installation and does not depend on Visual Studio being present.
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$crt = Get-ChildItem (Join-Path $visualStudio "VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT") -Directory | Sort-Object FullName -Descending | Select-Object -First 1
if (!$crt) { throw "Redistributable MSVC CRT directory not found." }
Copy-Item (Join-Path $crt.FullName "*.dll") $package -Force
if (!(Test-Path (Join-Path $package "msvcp140.dll")) -or !(Test-Path (Join-Path $package "vcruntime140.dll"))) { throw "Compiler runtime deployment is incomplete." }
Copy-Item (Join-Path $repo "LICENSE") $package
Copy-Item (Join-Path $repo "assets/Framecast.ico") $package
Copy-Item (Join-Path $repo "README.md") $package
# Keep dependency notices and the exact source-build manifest with the developer package.
$notices = Join-Path $package "licenses"
New-Item -ItemType Directory -Force $notices | Out-Null
$share = Join-Path $build "vcpkg_installed/x64-windows/share"
Get-ChildItem $share -Directory | ForEach-Object {
    $notice = Join-Path $_.FullName "copyright"
    if (Test-Path $notice) { Copy-Item $notice (Join-Path $notices ($_.Name + ".txt")) }
}
Copy-Item (Join-Path $repo "licenses/Qt-6.8.3") (Join-Path $notices "Qt") -Recurse
$qtSbom = Join-Path $qt "sbom/qtbase-6.8.3.spdx.json"
if (Test-Path $qtSbom) { Copy-Item $qtSbom (Join-Path $notices "Qt") }
if (!(Test-Path (Join-Path $notices "Qt/LGPL-3.0-only.txt"))) { throw "Qt license notices are missing." }
Copy-Item (Join-Path $repo "vcpkg.json") $notices
@"
Framecast Studio Windows $Version
Source: https://github.com/DhananjaySarathe/framecast-studio-windows
Build commit: $env:GITHUB_SHA
Qt: 6.8.3, dynamically linked. Source: https://download.qt.io/archive/qt/6.8/6.8.3/submodules/
FFmpeg and encoder dependencies: built from the pinned vcpkg manifest beside this file.
Build recipes: https://github.com/microsoft/vcpkg/tree/4702e381dbbd9b54d3f7e1eb565cd8981c1814cd/ports
"@ | Set-Content (Join-Path $notices "BUILD-SOURCES.txt") -Encoding utf8
$compiler = Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6/ISCC.exe"
if (!(Test-Path $compiler)) { throw "Inno Setup 6 compiler is not installed." }
$installerDir = Join-Path $build "installer"
New-Item -ItemType Directory -Force $installerDir | Out-Null
& $compiler "/DAppVersion=$Version" "/DPackageDir=$package" "/DOutputDir=$installerDir" (Join-Path $repo "installer/framecast.iss")
if ($LASTEXITCODE) { throw "Installer compilation failed: $LASTEXITCODE" }
Compress-Archive -Path (Join-Path $package "*") -DestinationPath (Join-Path $installerDir "Framecast-Studio-Windows-$Version-x64-Portable.zip") -Force
Get-ChildItem $installerDir -File | Where-Object { $_.Extension -in @(".exe", ".zip") } | ForEach-Object {
    $hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $($_.Name)"
} | Set-Content (Join-Path $installerDir "SHA256SUMS.txt") -Encoding ascii
