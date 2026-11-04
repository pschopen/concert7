; Concert7 - Windows installer (Inno Setup 6)
;
;   iscc /DAppVersion=0.1.0-beta /DArtefacts=<build>\Concert7_artefacts\Release /DOutputDir=<dist> packaging\windows\concert7.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Artefacts
  #define Artefacts "..\..\build\Concert7_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
AppId={{6F1C2B7E-3A54-4C1D-9E5B-C07A11C0FFEE}
AppName=Concert7
AppVersion={#AppVersion}
AppVerName=Concert7 {#AppVersion}
AppPublisher=pschopen
AppPublisherURL=https://github.com/pschopen/concert7
AppSupportURL=https://github.com/pschopen/concert7/issues
DefaultDirName={autopf}\Concert7
DefaultGroupName=Concert7
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
InfoBeforeFile=welcome.txt
OutputDir={#OutputDir}
OutputBaseFilename=Concert7-{#AppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Concert7

[Types]
Name: "full"; Description: "VST3 plugin and standalone app"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (Common Files\VST3)"; Types: full custom
Name: "standalone"; Description: "Standalone application"; Types: full

[Files]
Source: "{#Artefacts}\VST3\Concert7.vst3\*"; DestDir: "{commoncf64}\VST3\Concert7.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Artefacts}\Standalone\Concert7.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "..\..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Concert7"; Filename: "{app}\Concert7.exe"; Components: standalone
Name: "{group}\Uninstall Concert7"; Filename: "{uninstallexe}"
