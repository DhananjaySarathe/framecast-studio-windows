#ifndef AppVersion
  #define AppVersion "0.1.4"
#endif
#ifndef PackageDir
  #error PackageDir must point to the deployed studio directory
#endif
#ifndef OutputDir
  #define OutputDir "..\..\build\windows\installer"
#endif

[Setup]
AppId={{9FEDEBAA-C295-4CC0-9A92-7DDBB1BF5C91}
AppName=Framecast Studio
AppVersion={#AppVersion}
AppPublisher=Dhananjay Sarathe
AppPublisherURL=https://github.com/DhananjaySarathe/framecast-studio-windows
DefaultDirName={localappdata}\Programs\Framecast Studio
DefaultGroupName=Framecast Studio
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
OutputDir={#OutputDir}
OutputBaseFilename=Framecast-Studio-Windows-{#AppVersion}-x64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\FramecastStudio.exe
SetupIconFile={#PackageDir}\Framecast.ico
CloseApplications=yes
RestartApplications=no
LicenseFile={#PackageDir}\LICENSE

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Framecast Studio"; Filename: "{app}\FramecastStudio.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Framecast Studio"; Filename: "{app}\FramecastStudio.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\FramecastStudio.exe"; Description: "Open Framecast Studio"; Flags: nowait postinstall skipifsilent
