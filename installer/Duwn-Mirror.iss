; ===========================================================================
; Duwn Mirror — Official Inno Setup Packaging Script
; Target: Windows 10 22H2 / Windows 11 (64-bit)
; Modern Light Windows 11 Wizard Style
; ===========================================================================

#ifndef AppVersion
  #define AppVersion "1.1.3"
#endif

#define MyAppName "Duwn Mirror"
#define MyAppPublisher "Duwn Mirror Contributors"
#define MyAppURL "https://github.com/leduwn/Duwn-Mirror"
#define MyAppSupportURL "https://github.com/leduwn/Duwn-Mirror/issues"
#define MyAppUpdatesURL "https://github.com/leduwn/Duwn-Mirror/releases"
#define MyAppExeName "duwn-mirror.exe"
#define MyAppId "{{4E78B5D3-0941-4C58-8C64-44F4902F1DA9}}"

[Setup]
; Basic Application Identity
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#AppVersion}
AppVerName={#MyAppName} {#AppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppSupportURL}
AppUpdatesURL={#MyAppUpdatesURL}

; Directory and Architecture Defaults
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin

; Visual Styling — Modern Light Windows 11
WizardStyle=modern light windows11
WizardSizePercent=100,100
SetupIconFile=..\assets\app_icon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}

; Compression and Packaging
Compression=lzma2/max
SolidCompression=yes
OutputDir=.
OutputBaseFilename=Duwn-Mirror-Setup-{#AppVersion}-x64

; Application and Process Lifecycle Management
CloseApplications=yes
RestartApplications=no
CloseApplicationsFilter=duwn-mirror.exe,uxplay.exe

; Version Info Stamped into Setup Executable
VersionInfoVersion={#AppVersion}.0
VersionInfoProductVersion={#AppVersion}.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=Duwn Mirror Setup
VersionInfoProductName={#MyAppName}
VersionInfoCopyright=Copyright (C) 2026 {#MyAppPublisher}

[Languages]
Name: "vi"; MessagesFile: "compiler:Default.isl,Vietnamese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
en.WelcomeSubTitle=High-performance wireless AirPlay mirroring and streaming receiver for Windows. Designed for presentations, classroom teaching, app demos, screen recording, and livestreaming.
vi.WelcomeSubTitle=Ứng dụng nhận phản chiếu và truyền phát màn hình iPhone/iPad không dây trên Windows, phục vụ trình chiếu, giảng dạy, demo, quay video và livestream.

en.CompMain=Duwn Mirror Core Application & AirPlay Engine (Required)
vi.CompMain=Ứng dụng Duwn Mirror và công cụ AirPlay (bắt buộc)

en.CompVirtualCam=DirectShow Virtual Camera (Allows OBS, Teams, Zoom, TikTok LIVE to receive Duwn Mirror as a webcam)
vi.CompVirtualCam=Camera ảo DirectShow (cho phép OBS, Teams, Zoom, TikTok LIVE nhận hình từ Duwn Mirror như webcam)

en.CreateDesktopIcon=Create a desktop shortcut
vi.CreateDesktopIcon=Tạo lối tắt trên màn hình nền (Desktop)

en.AdditionalIcons=Additional shortcuts:
vi.AdditionalIcons=Lối tắt bổ sung:

en.LaunchProgram=Launch Duwn Mirror
vi.LaunchProgram=Mở Duwn Mirror

en.InstallingVCRedist=Installing Microsoft Visual C++ 2015-2026 Redistributable (x64)…
vi.InstallingVCRedist=Đang cài đặt Microsoft Visual C++ 2015-2026 Redistributable (x64)…

en.RemovingPriorVersion=Removing previous version of Duwn Mirror…
vi.RemovingPriorVersion=Đang gỡ bỏ phiên bản Duwn Mirror cũ trước khi cài đặt bản mới…

en.UpgradeDetected=An existing installation of Duwn Mirror (%1) was detected. Setup will upgrade it to version %2 and preserve your user settings.
vi.UpgradeDetected=Phát hiện phiên bản Duwn Mirror (%1) đang có trên máy. Trình cài đặt sẽ tự động nâng cấp lên phiên bản %2 và bảo toàn cấu hình người dùng.

en.ReinstallDetected=Duwn Mirror version %1 is already installed. Setup will repair or reinstall it.
vi.ReinstallDetected=Phiên bản Duwn Mirror %1 đã được cài đặt. Trình cài đặt sẽ tiến hành sửa chữa hoặc cài đặt lại.

en.DowngradeBlocked=A newer version of Duwn Mirror (%1) is already installed. Downgrading to version %2 is not permitted. Please uninstall the newer version manually if you wish to proceed.
vi.DowngradeBlocked=Phiên bản mới hơn của Duwn Mirror (%1) đã được cài đặt trên máy. Không thể hạ cấp về phiên bản %2. Vui lòng gỡ bản mới thủ công nếu bạn muốn tiếp tục.

en.AppCloseFailed=Failed to close running Duwn Mirror instance. Please close the application manually before continuing.
vi.AppCloseFailed=Không thể đóng phiên làm việc của Duwn Mirror. Vui lòng đóng ứng dụng thủ công trước khi tiếp tục.

en.PriorUninstallFailed=Failed to remove previous version of Duwn Mirror (error code: %1). Installation has been halted to avoid duplicate or conflicting installations. Please restart your computer and run Setup again, or remove the previous version via Windows Settings before retrying.
vi.PriorUninstallFailed=Gỡ bỏ phiên bản Duwn Mirror trước đây thất bại (mã lỗi: %1). Quá trình cài đặt đã dừng lại để tránh tạo bản cài đặt xung đột. Vui lòng khởi động lại máy tính và chạy lại Setup, hoặc gỡ bỏ bản cũ qua Cài đặt Windows trước khi thử lại.

[Components]
Name: "main"; Description: "{cm:CompMain}"; Types: full compact custom; Flags: fixed
Name: "vcam"; Description: "{cm:CompVirtualCam}"; Types: full; Flags: checkablealone

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; Core Application & Documentation
Source: "..\build-msvc\bin\Release\duwn-mirror.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\UxPlay-GPL-3.0.txt"; DestDir: "{app}"; Flags: ignoreversion

; Virtual Camera DirectShow Filter (Optional Component)
Source: "..\build-msvc\bin\Release\duwn-virtualcam.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete 64bit; Components: vcam

; AirPlay Engine & GStreamer Runtime
Source: "..\build-msvc\bin\Release\duwn-airplay\uxplay.exe"; DestDir: "{app}\duwn-airplay"; Flags: ignoreversion
Source: "..\build-msvc\bin\Release\duwn-airplay\*.dll"; DestDir: "{app}\duwn-airplay"; Flags: ignoreversion
Source: "..\build-msvc\bin\Release\duwn-airplay\plugins\*.dll"; DestDir: "{app}\duwn-airplay\plugins"; Flags: ignoreversion
Source: "..\build-msvc\bin\Release\duwn-airplay\libexec\*.exe"; DestDir: "{app}\duwn-airplay\libexec"; Flags: ignoreversion
Source: "..\build-msvc\bin\Release\duwn-airplay\tools\*.exe"; DestDir: "{app}\duwn-airplay\tools"; Flags: ignoreversion

; Tools
Source: "..\tools\configure-firewall.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "..\tools\configure-firewall.cmd"; DestDir: "{app}\tools"; Flags: ignoreversion

; Microsoft Visual C++ 2015-2026 Redistributable (x64) prerequisite
Source: "..\installer\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall; Check: VCRedistNeedsInstall

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"; Comment: "Low-latency AirPlay Mirroring for Windows"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"; Comment: "Low-latency AirPlay Mirroring for Windows"; Tasks: desktopicon

[InstallDelete]
; Clean up legacy WiX shortcuts and artifacts if present
Type: files; Name: "{group}\DUWN Mirror.lnk"
Type: files; Name: "{autodesktop}\DUWN Mirror.lnk"
Type: files; Name: "{app}\Duwn-Mirror-1.*.msi"
Type: files; Name: "{app}\Duwn-Mirror-Setup-*.exe"
Type: files; Name: "{app}\*.wixpdb"

[Registry]
; 64-bit DirectShow Video Input Device Registration (System-wide HKLM)
Root: HKLM; Subkey: "Software\Classes\CLSID\{{8B9F51B8-3232-4518-A7D9-4828E0D71B20}"; ValueType: string; ValueData: "Duwn Mirror Video"; Flags: uninsdeletekey; Components: vcam; Check: Is64BitInstallMode
Root: HKLM; Subkey: "Software\Classes\CLSID\{{8B9F51B8-3232-4518-A7D9-4828E0D71B20}\InprocServer32"; ValueType: string; ValueData: "{app}\duwn-virtualcam.dll"; Flags: uninsdeletekey; Components: vcam; Check: Is64BitInstallMode
Root: HKLM; Subkey: "Software\Classes\CLSID\{{8B9F51B8-3232-4518-A7D9-4828E0D71B20}\InprocServer32"; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Both"; Flags: uninsdeletekey; Components: vcam; Check: Is64BitInstallMode
Root: HKLM; Subkey: "Software\Classes\CLSID\{{860BB310-5D01-11d0-BD3B-00A0C911CE86}\Instance\{{8B9F51B8-3232-4518-A7D9-4828E0D71B20}"; ValueType: string; ValueName: "FriendlyName"; ValueData: "Duwn Mirror Video"; Flags: uninsdeletekey; Components: vcam; Check: Is64BitInstallMode
Root: HKLM; Subkey: "Software\Classes\CLSID\{{860BB310-5D01-11d0-BD3B-00A0C911CE86}\Instance\{{8B9F51B8-3232-4518-A7D9-4828E0D71B20}"; ValueType: string; ValueName: "CLSID"; ValueData: "{{8B9F51B8-3232-4518-A7D9-4828E0D71B20}}"; Flags: uninsdeletekey; Components: vcam; Check: Is64BitInstallMode

[Run]
; Install VC++ Redistributable silently if not already installed
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/quiet /norestart"; StatusMsg: "{cm:InstallingVCRedist}"; Flags: runhidden; Check: VCRedistNeedsInstall

; Configure Windows Defender Firewall Inbound Rules via netsh
Filename: "netsh"; Parameters: "advfirewall firewall add rule name=""Duwn Mirror Core Application (TCP-In)"" dir=in action=allow program=""{app}\duwn-mirror.exe"" enable=yes profile=domain,private protocol=tcp"; Flags: runhidden
Filename: "netsh"; Parameters: "advfirewall firewall add rule name=""Duwn Mirror Core Application (UDP-In)"" dir=in action=allow program=""{app}\duwn-mirror.exe"" enable=yes profile=domain,private protocol=udp"; Flags: runhidden
Filename: "netsh"; Parameters: "advfirewall firewall add rule name=""Duwn Mirror AirPlay Engine (TCP-In)"" dir=in action=allow program=""{app}\duwn-airplay\uxplay.exe"" enable=yes profile=domain,private protocol=tcp"; Flags: runhidden
Filename: "netsh"; Parameters: "advfirewall firewall add rule name=""Duwn Mirror AirPlay Engine (UDP-In)"" dir=in action=allow program=""{app}\duwn-airplay\uxplay.exe"" enable=yes profile=domain,private protocol=udp"; Flags: runhidden
Filename: "netsh"; Parameters: "advfirewall firewall add rule name=""Duwn Mirror mDNS Discovery (UDP-In 5353)"" dir=in action=allow protocol=udp localport=5353 enable=yes profile=domain,private"; Flags: runhidden
Filename: "netsh"; Parameters: "advfirewall firewall add rule name=""Duwn Mirror AirPlay RTSP (TCP-In)"" dir=in action=allow protocol=tcp localport=7000,7001,7100 enable=yes profile=domain,private"; Flags: runhidden
Filename: "netsh"; Parameters: "advfirewall firewall add rule name=""Duwn Mirror Media RTP Streams (UDP-In)"" dir=in action=allow protocol=udp localport=6000-7100 enable=yes profile=domain,private"; Flags: runhidden

; Launch Application on Exit (Non-elevated, suppressed automatically by Inno Setup if system restart pending)
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallRun]
; Remove Firewall Rules
Filename: "{app}\{#MyAppExeName}"; Parameters: "--firewall disable-public"; Flags: runhidden
Filename: "netsh"; Parameters: "advfirewall firewall delete rule name=""Duwn Mirror Core Application (TCP-In)"""; Flags: runhidden
Filename: "netsh"; Parameters: "advfirewall firewall delete rule name=""Duwn Mirror Core Application (UDP-In)"""; Flags: runhidden
Filename: "netsh"; Parameters: "advfirewall firewall delete rule name=""Duwn Mirror AirPlay Engine (TCP-In)"""; Flags: runhidden
Filename: "netsh"; Parameters: "advfirewall firewall delete rule name=""Duwn Mirror AirPlay Engine (UDP-In)"""; Flags: runhidden
Filename: "netsh"; Parameters: "advfirewall firewall delete rule name=""Duwn Mirror mDNS Discovery (UDP-In 5353)"""; Flags: runhidden
Filename: "netsh"; Parameters: "advfirewall firewall delete rule name=""Duwn Mirror AirPlay RTSP (TCP-In)"""; Flags: runhidden
Filename: "netsh"; Parameters: "advfirewall firewall delete rule name=""Duwn Mirror Media RTP Streams (UDP-In)"""; Flags: runhidden

[Code]
type
  TVersionArray = array[0..3] of Integer;
  TInstallType = (itClean, itInno, itWixBurn, itWixMsi);

var
  G_InstallType: TInstallType;
  G_InstalledVersion: String;
  G_InstalledDir: String;
  G_QuietUninstallCmd: String;
  G_QuietUninstallParams: String;
  G_HadVirtualCam: Boolean;

// ---------------------------------------------------------------------------
// Numeric Version Parsing & Comparison
// ---------------------------------------------------------------------------
function ParseVersionNumbers(const VStr: String; var V: TVersionArray): Integer;
var
  I, Count, ValNum: Integer;
  Cur: String;
begin
  Count := 0;
  Cur := '';
  for I := 1 to Length(VStr) do
  begin
    if VStr[I] = '.' then
    begin
      if Cur <> '' then
      begin
        ValNum := StrToIntDef(Cur, 0);
        if Count < 4 then
        begin
          V[Count] := ValNum;
          Inc(Count);
        end;
        Cur := '';
      end;
    end
    else if (VStr[I] >= '0') and (VStr[I] <= '9') then
      Cur := Cur + VStr[I];
  end;
  if Cur <> '' then
  begin
    ValNum := StrToIntDef(Cur, 0);
    if Count < 4 then
    begin
      V[Count] := ValNum;
      Inc(Count);
    end;
  end;
  Result := Count;
end;

function CompareVersions(const V1, V2: String): Integer;
var
  A1, A2: TVersionArray;
  I: Integer;
begin
  for I := 0 to 3 do
  begin
    A1[I] := 0;
    A2[I] := 0;
  end;
  ParseVersionNumbers(V1, A1);
  ParseVersionNumbers(V2, A2);
  for I := 0 to 3 do
  begin
    if A1[I] < A2[I] then
    begin
      Result := -1;
      Exit;
    end
    else if A1[I] > A2[I] then
    begin
      Result := 1;
      Exit;
    end;
  end;
  Result := 0;
end;

// ---------------------------------------------------------------------------
// VC++ Redistributable Detection
// ---------------------------------------------------------------------------
function VCRedistNeedsInstall(): Boolean;
var
  Installed: Cardinal;
begin
  Result := True;
  if RegQueryDWordValue(HKLM, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64', 'Installed', Installed) then
  begin
    if Installed = 1 then
      Result := False;
  end;
end;

// ---------------------------------------------------------------------------
// Command Line Splitter (splits "path" args into executable and arguments)
// ---------------------------------------------------------------------------
procedure SplitCommandLine(const FullCmd: String; var ExePath, Args: String);
var
  Trimmed: String;
  EndQuote: Integer;
begin
  Trimmed := Trim(FullCmd);
  if (Length(Trimmed) > 0) and (Trimmed[1] = '"') then
  begin
    Delete(Trimmed, 1, 1);
    EndQuote := Pos('"', Trimmed);
    if EndQuote > 0 then
    begin
      ExePath := Copy(Trimmed, 1, EndQuote - 1);
      Args := Trim(Copy(Trimmed, EndQuote + 1, Length(Trimmed)));
    end
    else
    begin
      ExePath := Trimmed;
      Args := '';
    end;
  end
  else
  begin
    EndQuote := Pos(' ', Trimmed);
    if EndQuote > 0 then
    begin
      ExePath := Copy(Trimmed, 1, EndQuote - 1);
      Args := Trim(Copy(Trimmed, EndQuote + 1, Length(Trimmed)));
    end
    else
    begin
      ExePath := Trimmed;
      Args := '';
    end;
  end;
end;

// ---------------------------------------------------------------------------
// Detect Prior Installation (Inno Setup or WiX Burn/MSI)
// ---------------------------------------------------------------------------
procedure DetectExistingInstallation();
var
  SubKey, DispVer, DispName, BurnCode, UnStr, QuietUnStr: String;
  Names: TArrayOfString;
  I: Integer;
begin
  G_InstallType := itClean;
  G_InstalledVersion := '';
  G_InstalledDir := '';
  G_QuietUninstallCmd := '';
  G_QuietUninstallParams := '';
  G_HadVirtualCam := False;

  // 1. Check for prior Inno Setup
  SubKey := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{#MyAppId}_is1';
  if RegQueryStringValue(HKLM, SubKey, 'DisplayVersion', DispVer) then
  begin
    G_InstallType := itInno;
    G_InstalledVersion := DispVer;
    RegQueryStringValue(HKLM, SubKey, 'InstallLocation', G_InstalledDir);
    if G_InstalledDir = '' then
      G_InstalledDir := ExpandConstant('{autopf}\{#MyAppName}');
    if FileExists(G_InstalledDir + '\duwn-virtualcam.dll') then
      G_HadVirtualCam := True;
    Exit;
  end;

  // 2. Check for WiX MSI via official UpgradeCode {A3D1E428-8902-4D9A-90A7-6831F901B94C}
  // Standard packed GUID: 824E1D3A2098A9D4097A86139F109BC4
  SubKey := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Installer\UpgradeCodes\824E1D3A2098A9D4097A86139F109BC4';
  if RegGetValueNames(HKLM, SubKey, Names) then
  begin
    if GetArrayLength(Names) > 0 then
    begin
      SubKey := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Installer\UserData\S-1-5-18\Products\' + Names[0] + '\InstallProperties';
      if RegQueryStringValue(HKLM, SubKey, 'UninstallString', UnStr) then
      begin
        G_InstallType := itWixMsi;
        RegQueryStringValue(HKLM, SubKey, 'DisplayVersion', G_InstalledVersion);
        RegQueryStringValue(HKLM, SubKey, 'InstallLocation', G_InstalledDir);
        SplitCommandLine(UnStr, G_QuietUninstallCmd, G_QuietUninstallParams);
        G_QuietUninstallParams := G_QuietUninstallParams + ' /qn /norestart';
        if G_InstalledDir = '' then
          G_InstalledDir := ExpandConstant('{autopf}\{#MyAppName}');
        if FileExists(G_InstalledDir + '\duwn-virtualcam.dll') then
          G_HadVirtualCam := True;
      end;
    end;
  end;

  // 3. Check for WiX Burn Bundle via verified BundleUpgradeCode {B7E5C381-64D9-4FE2-9E92-351E842D5A10}
  SubKey := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall';
  if RegGetSubkeyNames(HKLM, SubKey, Names) then
  begin
    for I := 0 to GetArrayLength(Names) - 1 do
    begin
      if RegQueryStringValue(HKLM, SubKey + '\' + Names[I], 'BundleUpgradeCode', BurnCode) then
      begin
        if Pos('B7E5C381-64D9-4FE2-9E92-351E842D5A10', UpperCase(BurnCode)) > 0 then
        begin
          // Burn bundle supercedes child MSI uninstall call
          G_InstallType := itWixBurn;
          RegQueryStringValue(HKLM, SubKey + '\' + Names[I], 'DisplayVersion', G_InstalledVersion);
          if G_InstalledVersion = '' then
            RegQueryStringValue(HKLM, SubKey + '\' + Names[I], 'BundleVersion', G_InstalledVersion);

          RegQueryStringValue(HKLM, SubKey + '\' + Names[I], 'QuietUninstallString', QuietUnStr);
          if QuietUnStr <> '' then
            SplitCommandLine(QuietUnStr, G_QuietUninstallCmd, G_QuietUninstallParams)
          else if RegQueryStringValue(HKLM, SubKey + '\' + Names[I], 'UninstallString', UnStr) then
          begin
            SplitCommandLine(UnStr, G_QuietUninstallCmd, G_QuietUninstallParams);
            G_QuietUninstallParams := G_QuietUninstallParams + ' /quiet /norestart';
          end;

          RegQueryStringValue(HKLM, SubKey + '\' + Names[I], 'InstallLocation', G_InstalledDir);
          if G_InstalledDir = '' then
            G_InstalledDir := ExpandConstant('{autopf}\{#MyAppName}');
          if FileExists(G_InstalledDir + '\duwn-virtualcam.dll') then
            G_HadVirtualCam := True;
          Break;
        end;
      end;
    end;
  end;

  // Fallback check on standard directory
  if (G_InstallType = itClean) and FileExists(ExpandConstant('{autopf}\{#MyAppName}\{#MyAppExeName}')) then
  begin
    G_InstalledDir := ExpandConstant('{autopf}\{#MyAppName}');
    if FileExists(G_InstalledDir + '\duwn-virtualcam.dll') then
      G_HadVirtualCam := True;
  end;
end;

// ---------------------------------------------------------------------------
// Setup Initialization Hook
// ---------------------------------------------------------------------------
function InitializeSetup(): Boolean;
var
  Cmp: Integer;
  Msg: String;
begin
  Result := True;
  DetectExistingInstallation();

  if (G_InstallType <> itClean) and (G_InstalledVersion <> '') then
  begin
    Cmp := CompareVersions(G_InstalledVersion, '{#AppVersion}');
    if Cmp > 0 then
    begin
      // Downgrade blocked!
      Msg := FmtMessage(CustomMessage('DowngradeBlocked'), [G_InstalledVersion, '{#AppVersion}']);
      MsgBox(Msg, mbError, MB_OK);
      Result := False;
      Exit;
    end;
  end;
end;

// ---------------------------------------------------------------------------
// Wizard Customization Hook
// ---------------------------------------------------------------------------
procedure InitializeWizard();
var
  Cmp: Integer;
  UpgradeMsg: String;
begin
  // Set default directory to previously installed location if available
  if G_InstalledDir <> '' then
  begin
    WizardForm.DirEdit.Text := G_InstalledDir;
  end;

  // If upgrading from an older version, display upgrade notice
  if (G_InstallType <> itClean) and (G_InstalledVersion <> '') then
  begin
    Cmp := CompareVersions(G_InstalledVersion, '{#AppVersion}');
    if Cmp < 0 then
    begin
      UpgradeMsg := FmtMessage(CustomMessage('UpgradeDetected'), [G_InstalledVersion, '{#AppVersion}']);
      WizardForm.WelcomeLabel2.Caption := UpgradeMsg + #13#10#13#10 + CustomMessage('WelcomeSubTitle');
    end
    else if Cmp = 0 then
    begin
      UpgradeMsg := FmtMessage(CustomMessage('ReinstallDetected'), [G_InstalledVersion]);
      WizardForm.WelcomeLabel2.Caption := UpgradeMsg + #13#10#13#10 + CustomMessage('WelcomeSubTitle');
    end;
  end;
end;

// ---------------------------------------------------------------------------
// Running Application Check & Graceful Close
// ---------------------------------------------------------------------------
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  HWndMain: HWND;
  WaitCount: Integer;
  TargetAppDir, PsCmd: String;
  ResultCode: Integer;
begin
  Result := '';
  HWndMain := FindWindowByClassName('DUWNMirrorMainWindow');
  if HWndMain = 0 then
    HWndMain := FindWindowByWindowName('Duwn Mirror');

  if HWndMain <> 0 then
  begin
    // Ask window to close gracefully so settings are flushed to disk
    PostMessage(HWndMain, 16 { WM_CLOSE }, 0, 0);
    WaitCount := 0;
    while (WaitCount < 50) and ((FindWindowByClassName('DUWNMirrorMainWindow') <> 0) or (FindWindowByWindowName('Duwn Mirror') <> 0)) do
    begin
      Sleep(100);
      Inc(WaitCount);
    end;

    if (FindWindowByClassName('DUWNMirrorMainWindow') <> 0) or (FindWindowByWindowName('Duwn Mirror') <> 0) then
    begin
      Result := CustomMessage('AppCloseFailed');
      Exit;
    end;
  end;

  // Gracefully terminate child uxplay.exe originating specifically from the installation directory
  TargetAppDir := G_InstalledDir;
  if TargetAppDir = '' then
    TargetAppDir := ExpandConstant('{app}');

  if (TargetAppDir <> '') and DirExists(TargetAppDir) then
  begin
    PsCmd := 'Get-CimInstance Win32_Process -Filter "Name = ''uxplay.exe''" | Where-Object { $_.ExecutablePath -and ($_.ExecutablePath.StartsWith(''' + TargetAppDir + ''', [System.StringComparison]::OrdinalIgnoreCase)) } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }';
    Exec('powershell.exe', '-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "' + PsCmd + '"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;

// ---------------------------------------------------------------------------
// Step Change: Uninstall prior WiX version before file extraction begins
// ---------------------------------------------------------------------------
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  ErrMsg: String;
  ExecParams: String;
begin
  if CurStep = ssInstall then
  begin
    // 1. Clean up stale HKCU VirtualCam COM keys from dev builds or previous runs
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Classes\CLSID\{8B9F51B8-3232-4518-A7D9-4828E0D71B20}');
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Classes\CLSID\{860BB310-5D01-11d0-BD3B-00A0C911CE86}\Instance\{8B9F51B8-3232-4518-A7D9-4828E0D71B20}');

    // 2. Seamlessly uninstall prior WiX version before file extraction begins
    if (G_InstallType = itWixBurn) or (G_InstallType = itWixMsi) then
    begin
      if G_QuietUninstallCmd <> '' then
      begin
        ExecParams := G_QuietUninstallParams;
        if Pos('/norestart', ExecParams) = 0 then
          ExecParams := ExecParams + ' /norestart';

        WizardForm.StatusLabel.Caption := CustomMessage('RemovingPriorVersion');
        if not Exec(G_QuietUninstallCmd, ExecParams, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
        begin
          ErrMsg := FmtMessage(CustomMessage('PriorUninstallFailed'), [IntToStr(ResultCode)]);
          RaiseException(ErrMsg);
        end;

        if (ResultCode <> 0) and (ResultCode <> 3010) then
        begin
          ErrMsg := FmtMessage(CustomMessage('PriorUninstallFailed'), [IntToStr(ResultCode)]);
          RaiseException(ErrMsg);
        end;
      end;
    end;
  end;
end;

// ---------------------------------------------------------------------------
// Uninstall Step Change: Clean up user-level COM artifacts
// ---------------------------------------------------------------------------
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Classes\CLSID\{8B9F51B8-3232-4518-A7D9-4828E0D71B20}');
    RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Classes\CLSID\{860BB310-5D01-11d0-BD3B-00A0C911CE86}\Instance\{8B9F51B8-3232-4518-A7D9-4828E0D71B20}');
  end;
end;
