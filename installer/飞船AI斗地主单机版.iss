#define AppName "飞船AI斗地主单机版"
#define AppPublisher "飞船AI斗地主单机版"
#define AppExeName "FourPlayerDoudizhu.exe"

#ifndef AppVersion
  #error AppVersion must be provided by the build script
#endif

#ifndef SourceDir
  #error SourceDir must be provided by the build script
#endif

#ifndef OutputDir
  #error OutputDir must be provided by the build script
#endif

[Setup]
AppId={{6D733A47-91F6-4B19-A70D-48F9D8231C81}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir={#OutputDir}
OutputBaseFilename={#AppName}-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=yes
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExeName}
VersionInfoVersion={#AppVersion}.0
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} 安装程序
VersionInfoCompany={#AppPublisher}
VersionInfoCopyright=
SetupMutex=FeichuanAiDoudizhuInstallerMutex

[Languages]
Name: "chinesesimp"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "vc_redist.x64.exe"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[InstallDelete]
Type: files; Name: "{userdesktop}\四人斗地主.lnk"
Type: files; Name: "{commondesktop}\四人斗地主.lnk"
Type: files; Name: "{app}\docs\飞船AI斗地主单机版使用说明.txt"
Type: files; Name: "{app}\resources\docs\飞船AI斗地主单机版使用说明.txt"
Type: files; Name: "{app}\nvdaControllerClient64.dll"
Type: files; Name: "{app}\licenses\NVDA-Controller-Client-LGPL-2.1.txt"
Type: files; Name: "{app}\Qt6Sql.dll"
Type: files; Name: "{app}\Qt6Concurrent.dll"
Type: filesandordirs; Name: "{app}\sqldrivers"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "正在安装 Microsoft Visual C++ 运行库……"; Flags: waituntilterminated runhidden
Filename: "{app}\{#AppExeName}"; Description: "启动 {#AppName}"; Flags: nowait postinstall skipifsilent
