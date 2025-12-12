; Linuxify Installer Script for Inno Setup
; Download Inno Setup from: https://jrsoftware.org/isinfo.php

#define MyAppName "Linuxify"
#define MyAppVersion "1.0"
#define MyAppPublisher "Cortez"
#define MyAppURL "https://github.com/cortez/linuxify"
#define MyAppExeName "linuxify.exe"
#define SourcePath "C:\Users\patri\OneDrive\Documents\Projects\Linuxify"

[Setup]
AppId={{A8B9C0D1-E2F3-4567-8901-234567890ABC}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
OutputDir={#SourcePath}\installer\output
OutputBaseFilename=LinuxifySetup
SetupIconFile={#SourcePath}\assets\linux_penguin_animal_9362.ico
UninstallDisplayIcon={app}\assets\linux_penguin_animal_9362.ico
Compression=lzma
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ChangesEnvironment=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "addtopath"; Description: "Add Linuxify to system PATH"; GroupDescription: "System Integration:"
Name: "addtoterminal"; Description: "Add Linuxify to Windows Terminal"; GroupDescription: "System Integration:"

[Files]
; Main executables
Source: "{#SourcePath}\linuxify.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourcePath}\nano.exe"; DestDir: "{app}"; Flags: ignoreversion

; Assets (icons)
Source: "{#SourcePath}\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs

; Database files - all .lin files
Source: "{#SourcePath}\linuxdb\*"; DestDir: "{app}\linuxdb"; Flags: ignoreversion

; Custom commands (lvc, etc.)
Source: "{#SourcePath}\cmds\*"; DestDir: "{app}\cmds"; Flags: ignoreversion recursesubdirs createallsubdirs

; Bundled C++ Toolchain (MinGW-w64)
Source: "{#SourcePath}\toolchain\*"; DestDir: "{app}\toolchain"; Flags: ignoreversion recursesubdirs createallsubdirs

; Nano syntax highlighting plugins
Source: "{#SourcePath}\plugins\*"; DestDir: "{app}\plugins"; Flags: ignoreversion recursesubdirs createallsubdirs

[Dirs]
Name: "{app}\cmds"
Name: "{app}\linuxdb"
Name: "{app}\toolchain"
Name: "{app}\toolchain\compiler"
Name: "{app}\plugins"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\assets\linux_penguin_animal_9362.ico"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\assets\linux_penguin_animal_9362.ico"; Tasks: desktopicon

[Registry]
; Add Linuxify and toolchain to PATH
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app};{app}\toolchain\compiler\mingw64\bin"; Tasks: addtopath; Check: NeedsAddPath('{app}')
; Set CC and CXX environment variables for IDE auto-detection
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: string; ValueName: "CC"; ValueData: "{app}\toolchain\compiler\mingw64\bin\gcc.exe"; Tasks: addtopath
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: string; ValueName: "CXX"; ValueData: "{app}\toolchain\compiler\mingw64\bin\g++.exe"; Tasks: addtopath

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE,
    'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
    'Path', OrigPath)
  then begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Param + ';', ';' + OrigPath + ';') = 0;
end;

procedure AddWindowsTerminalProfile();
var
  SettingsPath: string;
  SettingsContent: AnsiString;
  ProfileEntry: string;
  InsertPos: Integer;
  AppPath: string;
begin
  SettingsPath := ExpandConstant('{localappdata}\Packages\Microsoft.WindowsTerminal_8wekyb3d8bbwe\LocalState\settings.json');
  AppPath := ExpandConstant('{app}');
  
  // Replace backslashes with double backslashes for JSON
  StringChangeEx(AppPath, '\', '\\', True);
  
  if FileExists(SettingsPath) then
  begin
    if LoadStringFromFile(SettingsPath, SettingsContent) then
    begin
      if Pos('Linuxify', SettingsContent) = 0 then
      begin
        InsertPos := Pos('"list":', SettingsContent);
        if InsertPos > 0 then
        begin
          InsertPos := Pos('[', Copy(SettingsContent, InsertPos, Length(SettingsContent))) + InsertPos;
          
          ProfileEntry := #13#10 + 
            '            {' + #13#10 +
            '                "name": "Linuxify",' + #13#10 +
            '                "commandline": "' + AppPath + '\\linuxify.exe",' + #13#10 +
            '                "icon": "' + AppPath + '\\assets\\linux_penguin_animal_9362.ico",' + #13#10 +
            '                "startingDirectory": "%USERPROFILE%"' + #13#10 +
            '            },';
          
          Insert(ProfileEntry, SettingsContent, InsertPos);
          SaveStringToFile(SettingsPath, SettingsContent, False);
        end;
      end;
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if IsTaskSelected('addtoterminal') then
      AddWindowsTerminalProfile();
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Path: string;
  AppPath: string;
  P: Integer;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    if RegQueryStringValue(HKEY_LOCAL_MACHINE,
      'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
      'Path', Path) then
    begin
      AppPath := ExpandConstant('{app}');
      P := Pos(';' + AppPath, Path);
      if P > 0 then
      begin
        Delete(Path, P, Length(';' + AppPath));
        RegWriteStringValue(HKEY_LOCAL_MACHINE,
          'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
          'Path', Path);
      end;
    end;
  end;
end;
