param(
    [switch]$Test,
    [switch]$MediaTest,
    [switch]$Package,
    [switch]$ConfigureOnly,
    [switch]$SkipConfigure,
    [string]$BuildDir
)
. (Join-Path $PSScriptRoot "common.ps1")
Assert-Windows
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (!$BuildDir) { $BuildDir = Join-Path $repo "build/windows" }
$config = Join-Path $repo ".deps/toolchain.json"
if (!(Test-Path $config)) { throw "Run .\setup.cmd first." }
$tools = Get-Content $config -Raw | ConvertFrom-Json
$env:PATH = (Join-Path $tools.QtDir "bin") + ";" + $env:PATH
$env:QT_PLUGIN_PATH = Join-Path $tools.QtDir "plugins"
if (!$SkipConfigure) {
    Invoke-Checked -File cmake -Arguments @("-S", $repo, "-B", $BuildDir, "-A", "x64", "-T", "v143", "-DCMAKE_TOOLCHAIN_FILE=$($tools.VcpkgRoot)/scripts/buildsystems/vcpkg.cmake", "-DVCPKG_OVERLAY_TRIPLETS=$repo/triplets", "-DVCPKG_PLATFORM_TOOLSET=v143", "-DVCPKG_TARGET_TRIPLET=x64-windows", "-DFRAMECAST_BUILD_UI=ON", "-DCMAKE_PREFIX_PATH=$($tools.QtDir)")
}
if ($ConfigureOnly) { return }
Invoke-Checked -File cmake -Arguments @("--build", $BuildDir, "--config", "Release", "--parallel")
if ($Test) { Invoke-Checked -File ctest -Arguments @("--test-dir", $BuildDir, "-C", "Release", "--output-on-failure", "--output-junit", "test-results.xml") }
if ($MediaTest) {
    foreach ($command in @("ffmpeg", "ffprobe")) {
        if (!(Get-Command $command -ErrorAction SilentlyContinue)) { throw "Install Gyan.FFmpeg with WinGet, reopen your terminal and retry. See README.md." }
    }
    Invoke-Checked -File python -Arguments @((Join-Path $repo "tests/verify_media.py"), (Join-Path $BuildDir "Release/framecast-engine.exe"), "--network")
}
if ($Package) { & (Join-Path $PSScriptRoot "package.ps1") -BuildDir $BuildDir -QtDir $tools.QtDir }
Write-Host "Studio: $(Join-Path $BuildDir 'Release/FramecastStudio.exe')"
