; SPDX-License-Identifier: GPL-3.0-or-later
; Inno Setup script. Compile through scripts/package-windows.ps1, which passes the /D defines.
#ifndef AppVersion
#error AppVersion must be passed with /DAppVersion (the scripts read it from the top-level CMakeLists.txt)
#endif
#ifndef SourceDir
#error SourceDir must point at the assembled package folder
#endif
#ifndef OutDir
#define OutDir "."
#endif
#ifndef OutName
#define OutName "lvdsim-setup"
#endif

[Setup]
AppName=lvdsim
AppVersion={#AppVersion}
AppPublisher=lvdsim contributors
DefaultDirName={autopf}\lvdsim
DefaultGroupName=lvdsim
LicenseFile={#SourceDir}\licenses\lvdsim-GPL-3.0.txt
OutputDir={#OutDir}
OutputBaseFilename={#OutName}
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ChangesEnvironment=yes
UninstallDisplayName=lvdsim {#AppVersion}
UninstallDisplayIcon={app}\lvdsim-gui.exe

[Tasks]
Name: "addtopath"; Description: "Add lvdsim to the PATH (vlib, vmap, vcom, vlog, vsim and lvdsim-cli)"; GroupDescription: "Command line:"
Name: "desktopicon"; Description: "Create a desktop icon for lvdsim"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\lvdsim"; Filename: "{app}\lvdsim-gui.exe"
Name: "{group}\lvdsim README"; Filename: "{app}\README.txt"
Name: "{group}\Licenses"; Filename: "{app}\licenses"
Name: "{group}\Uninstall lvdsim"; Filename: "{uninstallexe}"
Name: "{autodesktop}\lvdsim"; Filename: "{app}\lvdsim-gui.exe"; Tasks: desktopicon

[Code]
// PATH handling. Only the install directory is put on PATH, never its nvcin subfolder: the bundled nvc uses the
// MSYS2 runtime, which must not be found ahead of the Qt MinGW runtime. The tools locate their own nvc relative to
// their executable. The pure string functions (PathEntryKey, PathNextEntry, PathContains, PathAppend, PathRemove)
// have no side effects; a mirror of them is unit tested outside this file (see docs/packaging.md).

const
  PathMarkerKey = 'Software\lvdsim';
  PathMarkerValue = 'PathEntryAdded';

{ Canonical form of one PATH entry for comparison: trimmed, quotes and trailing backslashes or slashes removed,
  lower case. Variable references such as %SystemRoot% are compared literally, never expanded. }
function PathEntryKey(const Entry: string): string;
var
  S: string;
begin
  S := Trim(Entry);
  if (Length(S) >= 2) and (S[1] = '"') and (S[Length(S)] = '"') then
    S := Trim(Copy(S, 2, Length(S) - 2));
  while (Length(S) > 0) and ((S[Length(S)] = '\') or (S[Length(S)] = '/')) do
    Delete(S, Length(S), 1);
  Result := Lowercase(S);
end;

{ Returns the next entry of List starting at position P (1 based) and advances
  P past the separator. P becomes Length(List) + 2 after the last entry. }
function PathNextEntry(const List: string; var P: Integer): string;
var
  Start: Integer;
begin
  Start := P;
  while (P <= Length(List)) and (List[P] <> ';') do
    P := P + 1;
  Result := Copy(List, Start, P - Start);
  P := P + 1;
end;

function PathContains(const Paths, Dir: string): Boolean;
var
  P: Integer;
  Want: string;
begin
  Result := False;
  Want := PathEntryKey(Dir);
  if Want = '' then
    Exit;
  P := 1;
  while P <= Length(Paths) do
    if PathEntryKey(PathNextEntry(Paths, P)) = Want then
    begin
      Result := True;
      Exit;
    end;
end;

function PathAppend(const Paths, Dir: string): string;
begin
  if Trim(Paths) = '' then
    Result := Dir
  else if Paths[Length(Paths)] = ';' then
    Result := Paths + Dir
  else
    Result := Paths + ';' + Dir;
end;

{ Removes every entry equal to Dir (all duplicates). Other entries are kept byte for byte, in order; a trailing
  separator disappears because no empty last token is produced. }
function PathRemove(const Paths, Dir: string): string;
var
  P: Integer;
  Want, Entry: string;
  First: Boolean;
begin
  Result := '';
  First := True;
  Want := PathEntryKey(Dir);
  P := 1;
  while P <= Length(Paths) do
  begin
    Entry := PathNextEntry(Paths, P);
    if (Want <> '') and (PathEntryKey(Entry) = Want) then
      Continue;
    if not First then
      Result := Result + ';';
    Result := Result + Entry;
    First := False;
  end;
end;

{ Per-user install writes HKCU\Environment, all-users (administrative) install writes the system PATH. }
function EnvRoot: Integer;
begin
  if IsAdminInstallMode then
    Result := HKEY_LOCAL_MACHINE
  else
    Result := HKEY_CURRENT_USER;
end;

function EnvSubKey: string;
begin
  if IsAdminInstallMode then
    Result := 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
  else
    Result := 'Environment';
end;

procedure AddAppToPath;
var
  Old, Dir: string;
begin
  Dir := ExpandConstant('{app}');
  if not RegQueryStringValue(EnvRoot, EnvSubKey, 'Path', Old) then
    Old := ''; { missing value: create it }
  if PathContains(Old, Dir) then
  begin
    Log('PATH already contains ' + Dir + ', left unchanged');
    Exit;
  end;
  { Old is the raw value: RegQueryStringValue does not expand %VAR% references, so they are written back as read.
    The value is always written as REG_EXPAND_SZ, the type Windows itself uses for Path. }
  if RegWriteExpandStringValue(EnvRoot, EnvSubKey, 'Path', PathAppend(Old, Dir)) then
  begin
    RegWriteStringValue(HKA, PathMarkerKey, PathMarkerValue, Dir);
    Log('Added ' + Dir + ' to PATH');
  end
  else
    Log('Could not write PATH');
end;

procedure RemoveAppFromPath;
var
  Old, Dir, Marked: string;
begin
  Dir := ExpandConstant('{app}');
  { Only undo what this installer did: the marker is written when the entry was added. }
  if not RegQueryStringValue(HKA, PathMarkerKey, PathMarkerValue, Marked) then
    Exit;
  if RegQueryStringValue(EnvRoot, EnvSubKey, 'Path', Old) and PathContains(Old, Dir) then
    if not RegWriteExpandStringValue(EnvRoot, EnvSubKey, 'Path', PathRemove(Old, Dir)) then
      Log('Could not write PATH');
  RegDeleteValue(HKA, PathMarkerKey, PathMarkerValue);
  RegDeleteKeyIfEmpty(HKA, PathMarkerKey);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('addtopath') then
    AddAppToPath;
end;

{ ChangesEnvironment=yes makes Setup and the uninstaller broadcast WM_SETTINGCHANGE after this runs. }
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveAppFromPath;
end;
