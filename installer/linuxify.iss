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
ArchitecturesInstallIn64BitMode=x64

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "addtopath"; Description: "Add Linuxify to system PATH"; GroupDescription: "System Integration:"
Name: "addtoterminal"; Description: "Add Linuxify to Windows Terminal"; GroupDescription: "System Integration:"
Name: "installcron"; Description: "Install Cron Daemon (task scheduler)"; GroupDescription: "System Integration:"
Name: "installwslproxy"; Description: "Install WSL Kernel Proxy (enables extended kernel access)"; GroupDescription: "System Integration:"
Name: "defaultshell"; Description: "Set Linuxify as default system shell (replaces CMD)"; GroupDescription: "System Integration:"; Flags: unchecked
Name: "installheader"; Description: "Install linuxify.hpp to system include paths (MinGW/MSVC)"; GroupDescription: "Developer Tools:"

[Files]
; Main executables
Source: "{#SourcePath}\linuxify.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourcePath}\lino.exe"; DestDir: "{app}"; Flags: ignoreversion

; Assets (icons)
Source: "{#SourcePath}\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs

; Database files - all .lin files
Source: "{#SourcePath}\linuxdb\*"; DestDir: "{app}\linuxdb"; Flags: ignoreversion

; Custom commands (lvc, etc.)
Source: "{#SourcePath}\cmds\snode.exe"; DestDir: "{app}\cmds"; Flags: ignoreversion
Source: "{#SourcePath}\cmds\*"; DestDir: "{app}\cmds"; Flags: ignoreversion recursesubdirs createallsubdirs

; Bundled C++ Toolchain (MinGW-w64)
Source: "{#SourcePath}\toolchain\*"; DestDir: "{app}\toolchain"; Flags: ignoreversion recursesubdirs createallsubdirs

; Lino syntax highlighting plugins
Source: "{#SourcePath}\plugins\*"; DestDir: "{app}\plugins"; Flags: ignoreversion recursesubdirs createallsubdirs

; Linuxify API header for developers
Source: "{#SourcePath}\linuxify.hpp"; DestDir: "{app}"; Flags: ignoreversion
; Install to bundled MinGW include path
Source: "{#SourcePath}\linuxify.hpp"; DestDir: "{app}\toolchain\compiler\mingw64\include"; Tasks: installheader; Flags: ignoreversion
; Install to system MinGW if exists
Source: "{#SourcePath}\linuxify.hpp"; DestDir: "{code:GetMinGWIncludePath}"; Tasks: installheader; Flags: ignoreversion skipifsourcedoesntexist; Check: MinGWExists
; Install to MSVC include if exists  
Source: "{#SourcePath}\linuxify.hpp"; DestDir: "{code:GetMSVCIncludePath}"; Tasks: installheader; Flags: ignoreversion skipifsourcedoesntexist; Check: MSVCExists

; WSL Proxy DLL and sources (stored with app, installed to System32 via code)
Source: "{#SourcePath}\cmds-src\wsl_proxy\wslapi.dll"; DestDir: "{app}\wsl_proxy"; Tasks: installwslproxy; Flags: ignoreversion
Source: "{#SourcePath}\cmds-src\wsl_proxy\wslapi.cpp"; DestDir: "{app}\wsl_proxy"; Flags: ignoreversion
Source: "{#SourcePath}\cmds-src\wsl_proxy\wslapi.def"; DestDir: "{app}\wsl_proxy"; Flags: ignoreversion
Source: "{#SourcePath}\cmds-src\wsl_proxy\lxss_kernel.hpp"; DestDir: "{app}\wsl_proxy"; Flags: ignoreversion

[Dirs]
Name: "{app}\cmds"
Name: "{app}\linuxdb"
Name: "{app}\linuxdb\nodes"
Name: "{app}\toolchain"
Name: "{app}\toolchain\compiler"
Name: "{app}\plugins"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\assets\linux_penguin_animal_9362.ico"
Name: "{group}\Windux"; Filename: "{app}\cmds\windux.exe"; IconFilename: "{app}\assets\linux_penguin_animal_9362.ico"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\assets\linux_penguin_animal_9362.ico"; Tasks: desktopicon
Name: "{autodesktop}\Windux"; Filename: "{app}\cmds\windux.exe"; IconFilename: "{app}\assets\linux_penguin_animal_9362.ico"; Tasks: desktopicon

[Registry]
; Add Linuxify and toolchain to PATH
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app};{app}\toolchain\compiler\mingw64\bin"; Tasks: addtopath; Check: NeedsAddPath('{app}')
; Set CC and CXX environment variables for IDE auto-detection
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: string; ValueName: "CC"; ValueData: "{app}\toolchain\compiler\mingw64\bin\gcc.exe"; Tasks: addtopath
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: string; ValueName: "CXX"; ValueData: "{app}\toolchain\compiler\mingw64\bin\g++.exe"; Tasks: addtopath

; Register crond to run at SYSTEM boot (not just user login)
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "LinuxifyCrond"; ValueData: """{app}\cmds\crond.exe"""; Tasks: installcron; Flags: uninsdeletevalue

; Set Linuxify as default system shell (ComSpec)
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: string; ValueName: "ComSpec"; ValueData: "{app}\linuxify.exe"; Tasks: defaultshell
; Replace "Open command window here" with Linuxify
Root: HKCR; Subkey: "Directory\Background\shell\cmd\command"; ValueType: string; ValueName: ""; ValueData: """{app}\linuxify.exe"""; Tasks: defaultshell
Root: HKCR; Subkey: "Directory\shell\cmd\command"; ValueType: string; ValueName: ""; ValueData: """{app}\linuxify.exe"""; Tasks: defaultshell

; Open in Windux Context Menu (Directory Background - right-click in folder empty space)
Root: HKCU; Subkey: "Software\Classes\Directory\Background\shell\Windux"; ValueType: string; ValueName: ""; ValueData: "Open in Windux"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Directory\Background\shell\Windux"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\cmds\windux.exe"""
Root: HKCU; Subkey: "Software\Classes\Directory\Background\shell\Windux\command"; ValueType: string; ValueName: ""; ValueData: """{app}\cmds\windux.exe"" ""%V"""

; Open in Windux Context Menu (Directory - right-click on a folder)
Root: HKCU; Subkey: "Software\Classes\Directory\shell\Windux"; ValueType: string; ValueName: ""; ValueData: "Open in Windux"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Directory\shell\Windux"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\cmds\windux.exe"""
Root: HKCU; Subkey: "Software\Classes\Directory\shell\Windux\command"; ValueType: string; ValueName: ""; ValueData: """{app}\cmds\windux.exe"" ""%V"""

; Open in Windux Context Menu (Desktop Background - right-click on desktop)
Root: HKCU; Subkey: "Software\Classes\DesktopBackground\shell\Windux"; ValueType: string; ValueName: ""; ValueData: "Open in Windux"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\DesktopBackground\shell\Windux"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\cmds\windux.exe"""
Root: HKCU; Subkey: "Software\Classes\DesktopBackground\shell\Windux\command"; ValueType: string; ValueName: ""; ValueData: """{app}\cmds\windux.exe"" ""%V"""

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
; Start crond daemon after installation
Filename: "{app}\cmds\crond.exe"; Tasks: installcron; Flags: nowait runhidden postinstall

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

function MinGWExists(): boolean;
begin
  Result := DirExists('C:\msys64\mingw64\include') or 
            DirExists('C:\mingw64\include') or
            DirExists('C:\MinGW\include');
end;

function GetMinGWIncludePath(Param: string): string;
begin
  if DirExists('C:\msys64\mingw64\include') then
    Result := 'C:\msys64\mingw64\include'
  else if DirExists('C:\mingw64\include') then
    Result := 'C:\mingw64\include'
  else if DirExists('C:\MinGW\include') then
    Result := 'C:\MinGW\include'
  else
    Result := '';
end;

function MSVCExists(): boolean;
var
  VSPath: string;
begin
  Result := False;
  if RegQueryStringValue(HKEY_LOCAL_MACHINE, 
    'SOFTWARE\Microsoft\VisualStudio\SxS\VS7', '17.0', VSPath) then
    Result := True
  else if RegQueryStringValue(HKEY_LOCAL_MACHINE,
    'SOFTWARE\Microsoft\VisualStudio\SxS\VS7', '16.0', VSPath) then
    Result := True;
end;

function GetMSVCIncludePath(Param: string): string;
var
  VSPath: string;
begin
  Result := '';
  if RegQueryStringValue(HKEY_LOCAL_MACHINE,
    'SOFTWARE\Microsoft\VisualStudio\SxS\VS7', '17.0', VSPath) then
    Result := VSPath + 'VC\Tools\MSVC\include'
  else if RegQueryStringValue(HKEY_LOCAL_MACHINE,
    'SOFTWARE\Microsoft\VisualStudio\SxS\VS7', '16.0', VSPath) then
    Result := VSPath + 'VC\Tools\MSVC\include';
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
var
  ProxyDll, OrigDll, BackupDll: string;
  ErrorCode: Integer;
begin
  if CurStep = ssPostInstall then
  begin
    if IsTaskSelected('addtoterminal') then
      AddWindowsTerminalProfile();
    
    // Install WSL Kernel Proxy
    if IsTaskSelected('installwslproxy') then
    begin
      ProxyDll := ExpandConstant('{app}\wsl_proxy\wslapi.dll');
      OrigDll := ExpandConstant('{sys}\wslapi.dll');
      BackupDll := ExpandConstant('{sys}\wslapi_orig.dll');
      
      // Only proceed if original wslapi.dll exists (WSL is installed)
      if FileExists(OrigDll) then
      begin
        // Backup original if not already backed up
        if not FileExists(BackupDll) then
        begin
          FileCopy(OrigDll, BackupDll, False);
        end;
        
        // Try to copy proxy DLL to System32
        if FileCopy(ProxyDll, OrigDll, False) then
          Log('WSL Proxy installed successfully')
        else
          Log('WSL Proxy installation failed - file may be in use');
      end;
    end;
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
    
    // Remove crond from system startup registry
    RegDeleteValue(HKEY_LOCAL_MACHINE,
      'Software\Microsoft\Windows\CurrentVersion\Run',
      'LinuxifyCrond');
    
    // Restore original wslapi.dll if we installed proxy
    begin
      if FileExists(ExpandConstant('{sys}\wslapi_orig.dll')) then
      begin
        FileCopy(ExpandConstant('{sys}\wslapi_orig.dll'), 
                 ExpandConstant('{sys}\wslapi.dll'), False);
        DeleteFile(ExpandConstant('{sys}\wslapi_orig.dll'));
      end;
    end;
  end;
end;
