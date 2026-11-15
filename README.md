# Framecast Studio Windows

Windows 11 **Intel/AMD x64** desktop studio: screen/window and webcam capture, landscape + portrait recording, and RTMP/RTMPS broadcasting. This repository is the standalone Windows project. The [Mac application](https://github.com/DhananjaySarathe/framecast-studio) has its own repository.

**Status:** unsigned developer preview. The core studio is implemented; full Mac feature parity, physical laptop device checks and full-HD performance qualification remain in progress. See [implemented features and limitations](docs/ENGINE.md).

## Install on your laptop — no build tools required

1. Open [Windows downloads, v0.1.2](https://github.com/DhananjaySarathe/framecast-studio-windows/releases/tag/v0.1.2).
2. Download `Framecast-Studio-Windows-0.1.2-x64-Setup.exe` and run it. Windows may display an unknown-publisher warning because this preview is unsigned.
3. Open **Framecast Studio** from the Start menu. Choose **Add → select your sources → Start preview**. Use **Record** for local files or **Destinations** followed by **Start broadcast**.

The installer includes Qt, FFmpeg and the C++ runtimes. It installs into `%LOCALAPPDATA%\Programs\Framecast Studio` for your user account. The portable ZIP is an alternative: extract the entire ZIP into a folder and open `FramecastStudio.exe`.

To verify a downloaded installer in PowerShell:

```powershell
Get-FileHash .\Framecast-Studio-Windows-0.1.2-x64-Setup.exe -Algorithm SHA256
Get-Content .\SHA256SUMS.txt
```

Compare the matching filename's hash. The install helper below performs this comparison automatically.

## Install using a command

Without Git, use `curl.exe`, which ships with Windows 11. In PowerShell type `curl.exe` rather than `curl`, because `curl` there runs a different command:

```powershell
cd $HOME\Downloads
curl.exe -LO https://github.com/DhananjaySarathe/framecast-studio-windows/releases/download/v0.1.2/Framecast-Studio-Windows-0.1.2-x64-Setup.exe
Get-FileHash .\Framecast-Studio-Windows-0.1.2-x64-Setup.exe
.\Framecast-Studio-Windows-0.1.2-x64-Setup.exe
```

The hash should be `ae145b783495623cf3e88640b1dea327ff8efd12231e05d3086bea09d7b03415`. The release is a pre-release, so link to the exact version: GitHub's `/releases/latest` link skips pre-releases.

If you have Git installed, the install helper checks the hash for you. Open PowerShell:

```powershell
git clone https://github.com/DhananjaySarathe/framecast-studio-windows.git
cd framecast-studio-windows
.\install.cmd -Silent
```

`install.cmd` downloads the v0.1.2 release installer and checks its SHA-256 against the release checksum file before running it. Omit `-Silent` to use the installer wizard. No compiler, Python or Qt SDK is needed for this command. If Git is not installed, use the download above, or download the repository ZIP through **Code → Download ZIP** and run `install.cmd` from the extracted project folder.

To update an existing clone, close Framecast Studio, then run:

```powershell
git pull --ff-only
.\install.cmd -Silent
```

Uninstall through **Windows Settings → Apps → Installed apps → Framecast Studio → Uninstall**.

## Build from source

These commands are for developers; installation from the release above is easier for normal use.

Install prerequisites from PowerShell using WinGet. The C++ Build Tools installation may request administrator access and takes several GB:

```powershell
winget install --exact --id Git.Git
winget install --exact --id Python.Python.3.12
winget install --exact --id Kitware.CMake
winget install --exact --id Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install --exact --id JRSoftware.InnoSetup
# Required only for the optional media / RTMP verification suite:
winget install --exact --id Gyan.FFmpeg
```

Close and reopen your terminal after installation so the tools are on PATH. If Visual Studio 2022 with **Desktop development with C++** is already installed, use that installation. The compiler toolset is v143; the build runner also supports a newer Visual Studio installation with v143 installed. Microsoft documents the [C++ workload](https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-build-tools?view=visualstudio) and [installer command options](https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio?view=vs-2022).

```powershell
git clone https://github.com/DhananjaySarathe/framecast-studio-windows.git
cd framecast-studio-windows
.\setup.cmd
.\build.cmd -Test -Package
```

Skip the clone commands if you already cloned this repository. `setup.cmd` creates a project-local Python environment, installs Qt **6.8.3**, and checks out vcpkg at **4702e381dbbd9b54d3f7e1eb565cd8981c1814cd**. Dependencies and toolchain metadata are in `.deps/`; there is no requirement to clone the Mac project. The overlay triplet builds Release libraries only to avoid compiling an unused second copy of FFmpeg.

Outputs:

| Path | Purpose |
|---|---|
| `build/windows/Release/FramecastStudio.exe` | Developer studio executable |
| `build/windows/Release/framecast-engine.exe` | Command-line engine |
| `build/windows/installer/*-Setup.exe` | Installable Windows app |
| `build/windows/installer/*-Portable.zip` | Portable app with runtime dependencies |
| `build/windows/installer/SHA256SUMS.txt` | Package hashes |

Keep runtime DLLs and plugin folders together. For distribution use the installer or portable package.

## Run and test

```powershell
# Build and run the automated native/core/UI checks:
.\build.cmd -Test

# Also check media files, slow RTMP setup, receiver outage and recovery:
.\build.cmd -Test -MediaTest -Package

# Open the built studio with synthetic sources; no capture permissions required:
.\build\windows\Release\FramecastStudio.exe --synthetic

# Record landscape and portrait synthetic video for ten seconds:
.\build\windows\Release\framecast-engine.exe --synthetic --record .\local-output --seconds 10

# Record your first display, webcam and default microphone:
.\build\windows\Release\framecast-engine.exe --monitor 0 --camera 0 --mic --record .\local-output --seconds 30

# Package the current build and exercise installation / clean-path launch / uninstall:
.\build.cmd -Package
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-installer.ps1 -BuildDir .\build\windows
```

Synthetic tests open only loopback network listeners and generated media. The native capture test captures its own changing test window. Physical webcams, microphone latency, hotplug, HDR/multiple displays, laptop performance and a real streaming-service RTMPS endpoint require device testing. The pipeline's exact scope is explained in [validation notes](docs/VALIDATION.md).

## Folder layout

```text
framecast-studio-windows/
  CMakeLists.txt, vcpkg.json, triplets/   build configuration
  src/, include/                        Windows capture and shared media engine
  ui/                                   Qt desktop studio
  tests/                                native, UI and media acceptance checks
  scripts/                              setup, build, package and install helpers
  installer/, assets/, licenses/         app packaging and dependency notices
  docs/                                 architecture, limitations and validation
  .github/workflows/windows.yml         Windows build/test/package workflow
```

The source originated from the Windows-only implementation tested at Mac-repository commit `ebf791855edf971e965da02c0eab8fef376af98b`. Its migration provenance and original test run are recorded in `docs/VALIDATION.md`. New development and releases belong in this repository.
