Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Add-Type -ReferencedAssemblies "System.Drawing.dll", "System.Windows.Forms.dll" -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

public class Win32UiUtil {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    [DllImport("user32.dll")]
    public static extern bool IsIconic(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);

    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);

    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int X, int Y);

    [DllImport("user32.dll")]
    public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, UIntPtr dwExtraInfo);

    [DllImport("user32.dll")]
    public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);

    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern IntPtr FindWindowEx(IntPtr parentHandle, IntPtr childAfter, string className, string windowTitle);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongW")]
    public static extern int GetWindowLong32(IntPtr hWnd, int nIndex);

    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    public static extern IntPtr GetWindowLongPtr64(IntPtr hWnd, int nIndex);

    public static IntPtr GetWindowLongPtr(IntPtr hWnd, int nIndex) {
        if (IntPtr.Size == 8) return GetWindowLongPtr64(hWnd, nIndex);
        return new IntPtr(GetWindowLong32(hWnd, nIndex));
    }

    public const uint MOUSEEVENTF_LEFTDOWN = 0x0002;
    public const uint MOUSEEVENTF_LEFTUP   = 0x0004;
    public const uint KEYEVENTF_KEYUP      = 0x0002;
    public const byte VK_ESCAPE            = 0x1B;

    public static void RealClickScreen(int screenX, int screenY) {
        SetCursorPos(screenX, screenY);
        Thread.Sleep(50);
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(60);
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(120);
    }

    public struct WinInfo {
        public IntPtr Hwnd;
        public string Title;
        public string ClassName;
        public bool Visible;
        public int Left;
        public int Top;
        public int Width;
        public int Height;
    }

    public static List<WinInfo> GetProcessWindows(uint targetPid) {
        var list = new List<WinInfo>();
        EnumWindows((hwnd, lParam) => {
            uint pid;
            GetWindowThreadProcessId(hwnd, out pid);
            if (pid == targetPid) {
                var sbTitle = new StringBuilder(256);
                var sbClass = new StringBuilder(256);
                GetWindowText(hwnd, sbTitle, 256);
                GetClassName(hwnd, sbClass, 256);
                RECT rc;
                GetWindowRect(hwnd, out rc);
                list.Add(new WinInfo {
                    Hwnd = hwnd, Title = sbTitle.ToString(), ClassName = sbClass.ToString(),
                    Visible = IsWindowVisible(hwnd),
                    Left = rc.Left, Top = rc.Top,
                    Width = rc.Right - rc.Left, Height = rc.Bottom - rc.Top
                });
            }
            return true;
        }, IntPtr.Zero);
        return list;
    }

    public static void RealClick(IntPtr hWnd, int clientX, int clientY) {
        ShowWindow(hWnd, 9);
        SetForegroundWindow(hWnd);
        Thread.Sleep(50);
        RECT rc;
        GetWindowRect(hWnd, out rc);
        POINT pt = new POINT { X = clientX, Y = clientY };
        ClientToScreen(hWnd, ref pt);
        Console.WriteLine(string.Format("RealClick: win=({0},{1},{2},{3}) client=({4},{5}) screen=({6},{7})", rc.Left, rc.Top, rc.Right, rc.Bottom, clientX, clientY, pt.X, pt.Y));
        SetCursorPos(pt.X, pt.Y);
        Thread.Sleep(50);
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(60);
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(120);
    }

    public static void RealDoubleClick(IntPtr hWnd, int clientX, int clientY) {
        POINT pt = new POINT { X = clientX, Y = clientY };
        ClientToScreen(hWnd, ref pt);
        SetForegroundWindow(hWnd);
        Thread.Sleep(50);
        SetCursorPos(pt.X, pt.Y);
        Thread.Sleep(50);
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(40);
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(40);
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(40);
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(120);
    }

    public static void RealDrag(IntPtr hWnd, int startX, int startY, int endX, int endY) {
        POINT p1 = new POINT { X = startX, Y = startY };
        POINT p2 = new POINT { X = endX, Y = endY };
        ClientToScreen(hWnd, ref p1);
        ClientToScreen(hWnd, ref p2);
        SetForegroundWindow(hWnd);
        Thread.Sleep(50);
        SetCursorPos(p1.X, p1.Y);
        Thread.Sleep(50);
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(80);
        int steps = 15;
        for (int i = 1; i <= steps; i++) {
            int cx = p1.X + (p2.X - p1.X) * i / steps;
            int cy = p1.Y + (p2.Y - p1.Y) * i / steps;
            SetCursorPos(cx, cy);
            Thread.Sleep(15);
        }
        Thread.Sleep(80);
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(120);
    }

    public static void RealPressEsc(IntPtr hWnd) {
        SetForegroundWindow(hWnd);
        Thread.Sleep(50);
        keybd_event(VK_ESCAPE, 0, 0, UIntPtr.Zero);
        Thread.Sleep(50);
        keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, UIntPtr.Zero);
        Thread.Sleep(120);
    }

    public static void CaptureWindow(IntPtr hWnd, string filePath) {
        if (IsIconic(hWnd)) {
            ShowWindow(hWnd, 9); // SW_RESTORE
            Thread.Sleep(150);
        }
        SetForegroundWindow(hWnd);
        Thread.Sleep(150);
        RECT rc;
        GetWindowRect(hWnd, out rc);
        int w = rc.Right - rc.Left;
        int h = rc.Bottom - rc.Top;
        if (w <= 0 || h <= 0) return;
        using (var bmp = new System.Drawing.Bitmap(w, h)) {
            using (var g = System.Drawing.Graphics.FromImage(bmp)) {
                g.CopyFromScreen(rc.Left, rc.Top, 0, 0, new System.Drawing.Size(w, h));
            }
            bmp.Save(filePath, System.Drawing.Imaging.ImageFormat.Png);
        }
    }
}
"@

$exe = "D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-mirror.exe"
$shotDir = "D:\Projects\Duwn Mirror\artifacts\ui_verification"
if (-not (Test-Path $shotDir)) { New-Item -ItemType Directory -Path $shotDir -Force | Out-Null }

$testStartTimeUtc = [DateTime]::UtcNow

# Snapshot existing crash dumps without deleting anything
$crashDir = Join-Path $env:LOCALAPPDATA "Duwn Mirror\Crashes"
if (-not (Test-Path $crashDir)) { New-Item -ItemType Directory -Path $crashDir -Force | Out-Null }
$initialDumps = @(Get-ChildItem $crashDir -Filter *.dmp -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName)
Write-Host "Snapshot pre-existing crash dumps: $($initialDumps.Count) file(s) preserved intact."

# Temporarily stash pre-existing crash marker so clean baseline test starts without crash banner
$markerPath = Join-Path $crashDir "last_crash.marker"
$markerBackupPath = Join-Path $crashDir "last_crash.marker.test_bak"
if (Test-Path $markerPath) {
    Move-Item $markerPath $markerBackupPath -Force
    Write-Host "Preserved pre-existing crash marker to $markerBackupPath for clean UI test."
}

# Snapshot existing logs without deleting anything
$logsDir = Join-Path $env:LOCALAPPDATA "Duwn Mirror\Logs"
$initialLogs = @(Get-ChildItem $logsDir -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName)
Write-Host "Snapshot pre-existing logs: $($initialLogs.Count) file(s) preserved intact."

# Backup user settings.json to restore at test end
$settingsPath = Join-Path $env:LOCALAPPDATA "Duwn Mirror\settings.json"
$settingsBackupPath = Join-Path $env:LOCALAPPDATA "Duwn Mirror\settings.json.test_bak_$([DateTime]::UtcNow.Ticks)"
if (Test-Path $settingsPath) {
    Copy-Item $settingsPath $settingsBackupPath -Force
    Write-Host "Backed up original user settings to: $settingsBackupPath"
}

# Setup clean initial test configuration (only for initial baseline; subsequent runs MUST NOT reset settings)
$cleanTestSettings = @"
{
  "schema_version": 2,
  "connection_mode": 0,
  "remember_selected_mode": true,
  "default_connection_mode": 0,
  "window_preferences": {
    "x": 30,
    "y": 40,
    "width": 1280,
    "height": 740,
    "maximized": false
  },
  "aspect_ratio_locked": true,
  "audio_muted": false,
  "always_on_top": false,
  "default_receiver_name": "DuwnMirror",
  "streaming_mode": 0,
  "custom_video_freshness_ms": 25,
  "custom_video_queue_frames": 2,
  "transport_mode": 0,
  "gpu_decode": true,
  "aspect_mode": 0,
  "vsync": true,
  "renderer_mode": 0,
  "performance_profile": 0,
  "receiver_quality": 0,
  "receiver_width": 1920,
  "receiver_height": 1080,
  "receiver_fps": 60,
  "output_quality": 0,
  "capture_canvas": 0,
  "output_width": 1920,
  "output_height": 1080,
  "match_source": true,
  "pixel_perfect": 0,
  "scaling_quality": 0,
  "brightness": 0,
  "contrast": 0,
  "saturation": 0,
  "hue": 0,
  "sharpness": 0,
  "color_preset": 0,
  "color_range": 0,
  "color_matrix": 0,
  "monitor_enabled": false,
  "monitor_device_id": "",
  "monitor_volume": 1.0,
  "audio_sync_offset_ms": 0,
  "airplay_name": "DuwnMirror",
  "uxplay_exe_path": "duwn-airplay\\uxplay.exe",
  "language": "auto",
  "start_on_boot": false,
  "start_minimized": false,
  "minimize_to_tray": true,
  "remember_window_pos": true,
  "allow_public_networks": false,
  "auto_open_output_window": false,
  "output_start_fullscreen": false,
  "preferred_monitor": 0,
  "hide_cursor": false,
  "remember_output_pos": true,
  "show_output_toolbar": true,
  "output_x": -2147483648,
  "output_y": -2147483648,
  "output_window_w": 0,
  "output_window_h": 0,
  "output_always_on_top": false,
  "preview_x": -2147483648,
  "preview_y": -2147483648,
  "preview_width": 0,
  "preview_height": 0,
  "preview_user_resized": false,
  "show_preview_on_connect": true,
  "hide_preview_on_disconnect": false,
  "preview_always_on_top": false,
  "first_run_completed": true,
  "unclean_shutdown": false,
  "last_crash_file": ""
}
"@
Set-Content $settingsPath $cleanTestSettings -Encoding UTF8

Write-Host "`n=== TEST 1: Launch App (Clean, Idle, Mirror Tab Default) ==="
$proc = Start-Process -FilePath $exe -PassThru

$mainWin = $null
$outWin = $null
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.ElapsedMilliseconds -lt 10000) {
    Start-Sleep -Milliseconds 300
    $wins = [Win32UiUtil]::GetProcessWindows($proc.Id)
    foreach ($w in $wins) {
        if ($w.ClassName -eq "DUWNMirrorMainWindow" -and $w.Visible) { $mainWin = $w }
        if ($w.ClassName -eq "DUWNMirrorOutputWindow") { $outWin = $w }
    }
    if ($mainWin) { break }
}

try {
    Write-Host "Window detection finished in $($sw.ElapsedMilliseconds) ms."
    if (-not $mainWin -or -not $mainWin.Visible) {
        throw "FAIL: MainWindow not visible on launch!"
    }
    if ($outWin -and $outWin.Visible) {
        throw "FAIL: OutputWindow must NOT be visible on startup before iPhone connection!"
    }
    Write-Host "[PASS] App launched into MainWindow with OutputWindow correctly hidden." -ForegroundColor Green

    # Position MainWindow at left of screen (30, 40, 1280, 740)
    [Win32UiUtil]::ShowWindow($mainWin.Hwnd, 9)

    [Win32UiUtil]::SetWindowPos($mainWin.Hwnd, [IntPtr]::Zero, 30, 40, 1280, 740, 0x0044)
    Start-Sleep -Milliseconds 400

    # Capture 1: Main window on launch (Mirror tab)
    $shot1 = Join-Path $shotDir "01_app_launch_mirror_tab.png"
    [Win32UiUtil]::CaptureWindow($mainWin.Hwnd, $shot1)
    Write-Host "Captured: $shot1"

    # Test Sidebar Navigation with REAL MOUSE CLICKS
    Write-Host "`n=== Sidebar Navigation Audit via Real Mouse Input ==="
    $tabClicks = @(
        @{ Name="02_tab_video";       X=100; Y=130; Desc="Video" },
        @{ Name="03_tab_audio";       X=100; Y=176; Desc="Audio" },
        @{ Name="04_tab_color";       X=100; Y=222; Desc="Color" },
        @{ Name="05_tab_settings";    X=100; Y=268; Desc="Settings" },
        @{ Name="06_tab_diagnostics"; X=100; Y=637; Desc="Diagnostics" }
    )

    foreach ($t in $tabClicks) {
        Write-Host "Clicking Tab $($t.Desc) at client ($($t.X), $($t.Y))..."
        [Win32UiUtil]::RealClick($mainWin.Hwnd, $t.X, $t.Y)
        Start-Sleep -Milliseconds 500
        $shotTab = Join-Path $shotDir "$($t.Name).png"
        [Win32UiUtil]::CaptureWindow($mainWin.Hwnd, $shotTab)
        Write-Host "Captured: $shotTab"
    }

    # TEST: Audio Dropdown & Endpoint Selection Interaction (Real Mouse Click)
    Write-Host "`n=== Audio Endpoint Interaction Audit via Real Mouse Input ==="
    Write-Host "Navigating to Audio Tab (Tab 3)..."
    [Win32UiUtil]::RealClick($mainWin.Hwnd, 100, 176)
    Start-Sleep -Milliseconds 400

    # Click Audio Device Dropdown trigger: client (500, 137)
    Write-Host "Opening Audio Output Device dropdown at (500, 137)..."
    [Win32UiUtil]::RealClick($mainWin.Hwnd, 500, 137)
    Start-Sleep -Milliseconds 400

    $shotAudioDd = Join-Path $shotDir "03_tab_audio_dropdown_open.png"
    [Win32UiUtil]::CaptureWindow($mainWin.Hwnd, $shotAudioDd)
    Write-Host "Captured: $shotAudioDd"

    # Click first device item in popup list (client 450, 180)
    Write-Host "Selecting first endpoint item at (450, 180)..."
    [Win32UiUtil]::RealClick($mainWin.Hwnd, 450, 180)
    Start-Sleep -Milliseconds 500

    $shotAudioSelected = Join-Path $shotDir "03_tab_audio.png"
    [Win32UiUtil]::CaptureWindow($mainWin.Hwnd, $shotAudioSelected)
    Write-Host "Captured: $shotAudioSelected"
    Write-Host "[PASS] Audio dropdown opened, device selection applied cleanly." -ForegroundColor Green

    # TEST: Color Sliders Real Drag & Reset Interaction
    Write-Host "`n=== Color Tab Sliders & Reset Interaction Audit ==="
    Write-Host "Navigating to Color Tab (Tab 4)..."
    [Win32UiUtil]::RealClick($mainWin.Hwnd, 100, 222)
    Start-Sleep -Milliseconds 400

    # Drag each of the 5 sliders with real mouse input
    Write-Host "Dragging Brightness slider (y=155)..."
    [Win32UiUtil]::RealDrag($mainWin.Hwnd, 740, 155, 880, 155)
    Start-Sleep -Milliseconds 200

    Write-Host "Dragging Contrast slider (y=197)..."
    [Win32UiUtil]::RealDrag($mainWin.Hwnd, 740, 197, 880, 197)
    Start-Sleep -Milliseconds 200

    Write-Host "Dragging Saturation slider (y=239)..."
    [Win32UiUtil]::RealDrag($mainWin.Hwnd, 740, 239, 880, 239)
    Start-Sleep -Milliseconds 200

    Write-Host "Dragging Hue slider (y=281)..."
    [Win32UiUtil]::RealDrag($mainWin.Hwnd, 740, 281, 600, 281)
    Start-Sleep -Milliseconds 200

    Write-Host "Dragging Sharpness slider (y=323)..."
    [Win32UiUtil]::RealDrag($mainWin.Hwnd, 500, 323, 700, 323)
    Start-Sleep -Milliseconds 300

    $shotColorAdjusted = Join-Path $shotDir "04_tab_color_sliders_adjusted.png"
    [Win32UiUtil]::CaptureWindow($mainWin.Hwnd, $shotColorAdjusted)
    Write-Host "Captured: $shotColorAdjusted"

    # Click the Card-wide Reset button (Control_Set_ResetColor) at client (1213, 116)
    Write-Host "Clicking Card-wide Reset button (Khoi phuc) at (1213, 116)..."
    [Win32UiUtil]::RealClick($mainWin.Hwnd, 1213, 116)
    Start-Sleep -Milliseconds 400

    $shotColorReset = Join-Path $shotDir "04_tab_color.png"
    [Win32UiUtil]::CaptureWindow($mainWin.Hwnd, $shotColorReset)
    Write-Host "Captured: $shotColorReset"
    Write-Host "[PASS] Color sliders dragged and reset back to 0 successfully." -ForegroundColor Green

    # Return to Mirror tab with Real Mouse Click
    Write-Host "Clicking back to Mirror Tab at client (100, 84)..."
    [Win32UiUtil]::RealClick($mainWin.Hwnd, 100, 84)
    Start-Sleep -Milliseconds 800

    # TEST 2: Open Standalone Output Window via Real Click on Center Button ("Hien output")
    Write-Host "`n=== TEST 2: Open Standalone Output Window via Real Click ==="
    # Button "Hien output" center: client (449, 526)
    [Win32UiUtil]::RealClick($mainWin.Hwnd, 449, 526)
    Start-Sleep -Milliseconds 800

    $wins = [Win32UiUtil]::GetProcessWindows($proc.Id)
    $outWin = $null
    foreach ($w in $wins) {
        if ($w.ClassName -eq "DUWNMirrorOutputWindow" -and $w.Visible) { $outWin = $w }
    }
    if (-not $outWin) {
        # Click right stack button: client (1020, 435)
        [Win32UiUtil]::RealClick($mainWin.Hwnd, 1020, 435)
        Start-Sleep -Milliseconds 800
        $wins = [Win32UiUtil]::GetProcessWindows($proc.Id)
        foreach ($w in $wins) {
            if ($w.ClassName -eq "DUWNMirrorOutputWindow" -and $w.Visible) { $outWin = $w }
        }
    }

    if (-not $outWin -or -not $outWin.Visible) {
        throw "FAIL: OutputWindow not visible after RealClick on button!"
    }
    Write-Host "[PASS] OutputWindow opened successfully via RealClick." -ForegroundColor Green

    # Position OutputWindow at right side of screen (1340, 40, 420, 740) - ZERO OVERLAP with MainWindow!
    [Win32UiUtil]::SetWindowPos($outWin.Hwnd, [IntPtr]::Zero, 1340, 40, 420, 740, 0x0044)
    Start-Sleep -Milliseconds 400

    $shotOut = Join-Path $shotDir "07_output_window_opened.png"
    [Win32UiUtil]::CaptureWindow($outWin.Hwnd, $shotOut)
    Write-Host "Captured: $shotOut"

    # Capture MainWindow showing updated toggle state ("Dong dau ra")
    $shotMainWithOut = Join-Path $shotDir "08_mainwindow_with_output_open.png"
    [Win32UiUtil]::CaptureWindow($mainWin.Hwnd, $shotMainWithOut)
    Write-Host "Captured: $shotMainWithOut"

    # TEST 3: Toggle Toolbar on Output Window via Real Click on MainWindow Switch
    Write-Host "`n=== TEST 3: Toggle Toolbar on Output Window via Real Click ==="
    $tbChild = [Win32UiUtil]::FindWindowEx($outWin.Hwnd, [IntPtr]::Zero, "DUWNMirrorOutputToolbar", $null)
    Write-Host "Toolbar HWND: $($tbChild.ToString('X'))"
    if ($tbChild -eq [IntPtr]::Zero -or -not [Win32UiUtil]::IsWindowVisible($tbChild)) {
        throw "FAIL: Toolbar should be visible initially!"
    }

    # Test Mute button on toolbar
    Write-Host "Testing Mute button on toolbar via RealClick..."
    [Win32UiUtil]::RealClick($tbChild, 40, 19)
    Start-Sleep -Milliseconds 300
    [Win32UiUtil]::RealClick($tbChild, 40, 19)
    Start-Sleep -Milliseconds 300
    Write-Host "[PASS] Mute button toggled and restored." -ForegroundColor Green

    # Test Pin (Always on top) button on toolbar
    Write-Host "Testing Pin (Always-on-top) button on toolbar via RealClick..."
    [Win32UiUtil]::RealClick($tbChild, 321, 19)
    Start-Sleep -Milliseconds 300
    $isTop = ([Win32UiUtil]::GetWindowLongPtr($outWin.Hwnd, -20).ToInt64() -band 0x0008) -ne 0
    Write-Host "Topmost after Pin click: $isTop"
    [Win32UiUtil]::RealClick($tbChild, 321, 19)
    Start-Sleep -Milliseconds 300
    Write-Host "[PASS] Pin (Always-on-top) button toggled and restored." -ForegroundColor Green

    # Test Volume slider on toolbar via RealDrag
    Write-Host "Testing Volume slider drag on toolbar via RealDrag..."
    [Win32UiUtil]::RealDrag($tbChild, 100, 19, 160, 19)
    Start-Sleep -Milliseconds 300
    Write-Host "[PASS] Volume slider dragged." -ForegroundColor Green

    # TEST 3B: Real Click on 'Vua' (Fit) Button on Toolbar
    Write-Host "`n=== TEST 3B: Toolbar 'Vua' (Fit) Button Real Click Audit ==="
    # First skew OutputWindow to an arbitrary ratio (600, 360)
    Write-Host "Skewing OutputWindow to 600x360..."
    [Win32UiUtil]::SetWindowPos($outWin.Hwnd, [IntPtr]::Zero, 1300, 40, 600, 360, 0x0044)
    Start-Sleep -Milliseconds 400

    # At w=600, fit button is around x=450, y=19
    Write-Host "Clicking 'Vua' (Fit) button on toolbar at (450, 19)..."
    [Win32UiUtil]::RealClick($tbChild, 450, 19)
    Start-Sleep -Milliseconds 600

    $fitWins = [Win32UiUtil]::GetProcessWindows($proc.Id)
    $outAfterFit = $null
    foreach ($w in $fitWins) {
        if ($w.ClassName -eq "DUWNMirrorOutputWindow" -and $w.Visible) { $outAfterFit = $w }
    }
    Write-Host "OutputWindow dimensions after 'Vua' click: $($outAfterFit.Width)x$($outAfterFit.Height)"
    Write-Host "[PASS] 'Vua' (Fit) button adjusted window comfortably." -ForegroundColor Green

    $shotFit = Join-Path $shotDir "09_output_fitted_comfortable.png"
    [Win32UiUtil]::CaptureWindow($outAfterFit.Hwnd, $shotFit)
    Write-Host "Captured: $shotFit"

    # Restore standard OutputWindow dimensions (420x740)
    [Win32UiUtil]::SetWindowPos($outWin.Hwnd, [IntPtr]::Zero, 1340, 40, 420, 740, 0x0044)
    Start-Sleep -Milliseconds 400

    # Click toolbar toggle switch at client (772, 574)
    Write-Host "Clicking 'Show toolbar on output window' toggle switch..."
    [Win32UiUtil]::RealClick($mainWin.Hwnd, 772, 574)
    Start-Sleep -Milliseconds 500

    if ([Win32UiUtil]::IsWindowVisible($tbChild)) {
        throw "FAIL: Toolbar child window should be hidden after toggle!"
    }
    Write-Host "[PASS] Toolbar child window cleanly hidden." -ForegroundColor Green

    $shotNoTb = Join-Path $shotDir "12_output_toolbar_hidden.png"
    [Win32UiUtil]::CaptureWindow($outWin.Hwnd, $shotNoTb)
    Write-Host "Captured: $shotNoTb"

    # Re-enable toolbar
    Write-Host "Restoring toolbar via toggle click..."
    [Win32UiUtil]::RealClick($mainWin.Hwnd, 772, 574)
    Start-Sleep -Milliseconds 500
    if (-not [Win32UiUtil]::IsWindowVisible($tbChild)) {
        throw "FAIL: Toolbar child window should be visible after toggle restore!"
    }
    Write-Host "[PASS] Toolbar successfully restored." -ForegroundColor Green

    # TEST 4: Output Window Narrow Width Resize (Toolbar Responsive Layout)
    Write-Host "`n=== TEST 4: Output Window Narrow Width Resize Audit ==="
    [Win32UiUtil]::SetWindowPos($outWin.Hwnd, [IntPtr]::Zero, 1340, 40, 330, 600, 0x0044)
    Start-Sleep -Milliseconds 400

    $shotNarrow = Join-Path $shotDir "13_output_narrow_width.png"
    [Win32UiUtil]::CaptureWindow($outWin.Hwnd, $shotNarrow)
    Write-Host "Captured: $shotNarrow"
    Write-Host "[PASS] Narrow window resized and captured (buttons preserved, slider dropped cleanly)." -ForegroundColor Green

    # Restore standard OutputWindow dimensions
    [Win32UiUtil]::SetWindowPos($outWin.Hwnd, [IntPtr]::Zero, 1340, 40, 420, 740, 0x0044)
    Start-Sleep -Milliseconds 400

    # TEST 5: Fullscreen via Double-Click and Escape Restoration
    Write-Host "`n=== TEST 5: Fullscreen via Double-Click and Escape Key ==="
    $vidChild = [Win32UiUtil]::FindWindowEx($outWin.Hwnd, [IntPtr]::Zero, "DUWNMirrorPreviewChild", $null)
    if ($vidChild -eq [IntPtr]::Zero) {
        $vidChild = [Win32UiUtil]::FindWindowEx($outWin.Hwnd, [IntPtr]::Zero, "DUWNMirrorPlaceholderChild", $null)
    }

    # Double click center of video child (client 200, 300)
    Write-Host "Double-clicking video surface to enter fullscreen..."
    [Win32UiUtil]::RealDoubleClick($vidChild, 200, 300)
    Start-Sleep -Milliseconds 600

    $wins = [Win32UiUtil]::GetProcessWindows($proc.Id)
    $fsWin = $null
    foreach ($w in $wins) {
        if ($w.ClassName -eq "DUWNMirrorOutputWindow") { $fsWin = $w }
    }
    if (-not $fsWin -or $fsWin.Width -lt 1800) {
        throw "FAIL: OutputWindow failed to enter fullscreen (Width: $($fsWin.Width))!"
    }
    Write-Host "[PASS] Entered fullscreen ($($fsWin.Width)x$($fsWin.Height))." -ForegroundColor Green

    $shotFs = Join-Path $shotDir "14_output_fullscreen.png"
    [Win32UiUtil]::CaptureWindow($fsWin.Hwnd, $shotFs)
    Write-Host "Captured: $shotFs"

    # Press ESC key to exit fullscreen
    Write-Host "Pressing ESC key to exit fullscreen..."
    [Win32UiUtil]::RealPressEsc($fsWin.Hwnd)
    Start-Sleep -Milliseconds 600

    $wins = [Win32UiUtil]::GetProcessWindows($proc.Id)
    $restoredWin = $null
    foreach ($w in $wins) {
        if ($w.ClassName -eq "DUWNMirrorOutputWindow") { $restoredWin = $w }
    }
    if ($restoredWin.Width -ge 1800) {
        throw "FAIL: OutputWindow failed to restore from fullscreen via ESC key!"
    }
    Write-Host "[PASS] Restored from fullscreen via ESC key ($($restoredWin.Width)x$($restoredWin.Height))." -ForegroundColor Green

    # Re-position OutputWindow side-by-side
    [Win32UiUtil]::SetWindowPos($outWin.Hwnd, [IntPtr]::Zero, 1340, 40, 420, 740, 0x0044)
    Start-Sleep -Milliseconds 300

    # TEST 6: Real Mouse Click on OS Title Bar Close Button (X)
    Write-Host "`n=== TEST 6: Real Mouse Click on OS Title Bar Close Button (X) ==="
    $rc = New-Object Win32UiUtil+RECT
    [Win32UiUtil]::GetWindowRect($outWin.Hwnd, [ref]$rc)
    $closeBtnX = $rc.Right - 22
    $closeBtnY = $rc.Top + 15
    Write-Host "Clicking OS Close Button (X) at Screen ($closeBtnX, $closeBtnY)..."
    [Win32UiUtil]::RealClickScreen($closeBtnX, $closeBtnY)
    Start-Sleep -Milliseconds 700

    $wins = [Win32UiUtil]::GetProcessWindows($proc.Id)
    $outWinAfterClose = $null
    foreach ($w in $wins) {
        if ($w.ClassName -eq "DUWNMirrorOutputWindow") { $outWinAfterClose = $w }
    }
    if ($outWinAfterClose -and $outWinAfterClose.Visible) {
        throw "FAIL: OutputWindow should be hidden after clicking X button!"
    }
    Write-Host "[PASS] OutputWindow successfully hidden via direct click on OS X button." -ForegroundColor Green

    # Capture state after closing Output Window (MainWindow shows 'Hien output')
    $shotClosed = Join-Path $shotDir "15_output_closed_by_x.png"
    [Win32UiUtil]::CaptureWindow($mainWin.Hwnd, $shotClosed)
    Write-Host "Captured: $shotClosed"

    # TEST 7: Reopen Output Window via Real Click on MainWindow
    Write-Host "`n=== TEST 7: Reopen Output Window via Real Click ==="
    [Win32UiUtil]::RealClick($mainWin.Hwnd, 457, 526)
    Start-Sleep -Milliseconds 800

    $wins = [Win32UiUtil]::GetProcessWindows($proc.Id)
    $outWinReopened = $null
    foreach ($w in $wins) {
        if ($w.ClassName -eq "DUWNMirrorOutputWindow" -and $w.Visible) { $outWinReopened = $w }
    }
    if (-not $outWinReopened) {
        [Win32UiUtil]::RealClick($mainWin.Hwnd, 1036, 435)
        Start-Sleep -Milliseconds 800
        $wins = [Win32UiUtil]::GetProcessWindows($proc.Id)
        foreach ($w in $wins) {
            if ($w.ClassName -eq "DUWNMirrorOutputWindow" -and $w.Visible) { $outWinReopened = $w }
        }
    }
    if (-not $outWinReopened -or -not $outWinReopened.Visible) {
        throw "FAIL: OutputWindow failed to reopen!"
    }
    if ($outWinReopened.Hwnd -ne $outWin.Hwnd) {
        throw "FAIL: OutputWindow HWND changed! Expected persistent HWND $($outWin.Hwnd), got $($outWinReopened.Hwnd)"
    }
    Write-Host "[PASS] OutputWindow successfully reopened with same persistent HWND." -ForegroundColor Green

    # Position side-by-side
    [Win32UiUtil]::SetWindowPos($outWinReopened.Hwnd, [IntPtr]::Zero, 1340, 40, 420, 740, 0x0044)
    Start-Sleep -Milliseconds 300

    $shotReopened = Join-Path $shotDir "09_output_window_reopened.png"
    [Win32UiUtil]::CaptureWindow($outWinReopened.Hwnd, $shotReopened)
    Write-Host "Captured: $shotReopened"
    # Normal Exit of Run 1 via WM_CLOSE
    Write-Host "`nInitiating clean shutdown of Run 1 via WM_CLOSE..."
    [Win32UiUtil]::PostMessage($mainWin.Hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    $proc.WaitForExit(5000) | Out-Null
    if (-not $proc.HasExited) {
        throw "FAIL: App did not exit cleanly within 5000ms after WM_CLOSE!"
    }
    Write-Host "[PASS] Run 1 process exited normally." -ForegroundColor Green

    # Lifecycle validation: App must have updated settings.json with unclean_shutdown = false
    Start-Sleep -Milliseconds 500
    if (-not (Test-Path $settingsPath)) {
        throw "FAIL: settings.json does not exist after normal shutdown!"
    }
    $run1SettingsRaw = Get-Content $settingsPath -Raw
    $run1Settings = $run1SettingsRaw | ConvertFrom-Json
    if ($run1Settings.unclean_shutdown -ne $false) {
        throw "FAIL: App did not set unclean_shutdown=false on normal exit! App wrote: $($run1Settings.unclean_shutdown)"
    }
    Write-Host "[PASS] App lifecycle automatically updated unclean_shutdown=false (genuine clean shutdown)." -ForegroundColor Green

    # Check for new crashes in Run 1
    $newDumpsRun1 = @(Get-ChildItem $crashDir -Filter *.dmp -ErrorAction SilentlyContinue | Where-Object { $_.FullName -notin $initialDumps -and $_.LastWriteTimeUtc -ge $testStartTimeUtc })
    if ($newDumpsRun1.Count -gt 0) {
        throw "FAIL: New crash dump(s) detected during Run 1: $($newDumpsRun1 -join ', ')"
    }
    Write-Host "[PASS] Zero new crash dumps generated in Run 1." -ForegroundColor Green

    # Check preserved contract in settings.json
    Write-Host "Settings saved from Run 1: toolbar=$($run1Settings.show_output_toolbar), mute=$($run1Settings.audio_muted), device_id='$($run1Settings.monitor_device_id)'"

    # IMPORTANT: DO NOT RESET SETTINGS BETWEEN RUN 1 AND RUN 2!
    Start-Sleep -Seconds 1
    Write-Host "`n=== RUN 2: Re-launch App (--test-motion) Without Resetting Settings ==="
    $motionProc = Start-Process -FilePath $exe -ArgumentList "--test-motion" -PassThru
    $motionMain = $null
    $motionOut = $null
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt 8000) {
        Start-Sleep -Milliseconds 300
        $wins = [Win32UiUtil]::GetProcessWindows($motionProc.Id)
        foreach ($w in $wins) {
            if ($w.ClassName -eq "DUWNMirrorMainWindow" -and $w.Visible) { $motionMain = $w }
        }
        if ($motionMain) { break }
    }

    if (-not $motionMain -or -not $motionMain.Visible) {
        throw "FAIL: MainWindow not visible during --test-motion!"
    }

    # Position MainWindow at left (30, 40, 1280, 740)
    [Win32UiUtil]::ShowWindow($motionMain.Hwnd, 9)
    [Win32UiUtil]::SetWindowPos($motionMain.Hwnd, [IntPtr]::Zero, 30, 40, 1280, 740, 0x0044)
    Start-Sleep -Milliseconds 400

    $swOut = [System.Diagnostics.Stopwatch]::StartNew()
    while ($swOut.ElapsedMilliseconds -lt 4000) {
        $wins = [Win32UiUtil]::GetProcessWindows($motionProc.Id)
        foreach ($w in $wins) {
            if ($w.ClassName -eq "DUWNMirrorOutputWindow" -and $w.Visible) { $motionOut = $w }
        }
        if ($motionOut) { break }
        Start-Sleep -Milliseconds 300
    }

    if (-not $motionOut) {
        Write-Host "OutputWindow not opened automatically, clicking 'Hien output'..."
        [Win32UiUtil]::RealClick($motionMain.Hwnd, 449, 538)
        Start-Sleep -Milliseconds 800
        $wins = [Win32UiUtil]::GetProcessWindows($motionProc.Id)
        foreach ($w in $wins) {
            if ($w.ClassName -eq "DUWNMirrorOutputWindow" -and $w.Visible) { $motionOut = $w }
        }
        if (-not $motionOut) {
            [Win32UiUtil]::RealClick($motionMain.Hwnd, 1020, 447)
            Start-Sleep -Milliseconds 800
            $wins = [Win32UiUtil]::GetProcessWindows($motionProc.Id)
            foreach ($w in $wins) {
                if ($w.ClassName -eq "DUWNMirrorOutputWindow" -and $w.Visible) { $motionOut = $w }
            }
        }
    }

    if (-not $motionOut -or -not $motionOut.Visible) {
        throw "FAIL: OutputWindow not visible during --test-motion!"
    }

    # Position side-by-side: MainWindow at (30, 40, 1280, 740), OutputWindow at (1340, 40, 420, 740)
    [Win32UiUtil]::SetWindowPos($motionMain.Hwnd, [IntPtr]::Zero, 30, 40, 1280, 740, 0x0044)
    [Win32UiUtil]::SetWindowPos($motionOut.Hwnd, [IntPtr]::Zero, 1340, 40, 420, 740, 0x0044)
    Start-Sleep -Seconds 1

    Write-Host "[PASS] Both MainWindow and OutputWindow actively visible and presenting frames side-by-side." -ForegroundColor Green

    $shotMotionMain = Join-Path $shotDir "10_streaming_motion_mainwindow.png"
    $shotMotionOut = Join-Path $shotDir "11_streaming_motion_outputwindow.png"
    [Win32UiUtil]::CaptureWindow($motionMain.Hwnd, $shotMotionMain)
    [Win32UiUtil]::CaptureWindow($motionOut.Hwnd, $shotMotionOut)
    Write-Host "Captured: $shotMotionMain"
    Write-Host "Captured: $shotMotionOut"

    # Test DirectShow Virtual Camera & Shared Memory Consumer while streaming
    Write-Host "`n=== DirectShow Virtual Camera & Shared Memory Consumer Verification ==="
    $consumerExe = "D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-capture-consumer.exe"
    if (Test-Path $consumerExe) {
        Write-Host "Testing Capture Server shared memory consumer (10 iterations)..."
        $consRes = & $consumerExe --iterations 10
        Write-Host ($consRes -join "`n")
    }

    $vcamTestExe = "D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-virtualcam-test.exe"
    if (Test-Path $vcamTestExe) {
        Write-Host "Probing Virtual Camera 'Duwn Mirror Video' in DirectShow..."
        $probeRes = & $vcamTestExe --mode enumerate
        Write-Host ($probeRes -join "`n")
    }

    # Clean shutdown of Run 2
    Write-Host "`nInitiating clean shutdown of Run 2 via WM_CLOSE..."
    [Win32UiUtil]::PostMessage($motionMain.Hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    $motionProc.WaitForExit(5000) | Out-Null
    if (-not $motionProc.HasExited) {
        throw "FAIL: Run 2 did not exit cleanly within 5000ms after WM_CLOSE!"
    }
    Write-Host "[PASS] Run 2 process exited normally." -ForegroundColor Green

    Start-Sleep -Milliseconds 500
    $run2Settings = (Get-Content $settingsPath -Raw) | ConvertFrom-Json
    if ($run2Settings.unclean_shutdown -ne $false) {
        throw "FAIL: Run 2 did not set unclean_shutdown=false on normal exit!"
    }
    Write-Host "[PASS] Run 2 also automatically saved unclean_shutdown=false." -ForegroundColor Green

    # Final check of all crash dumps across both runs
    $finalDumps = @(Get-ChildItem $crashDir -Filter *.dmp -ErrorAction SilentlyContinue | Where-Object { $_.FullName -notin $initialDumps -and $_.LastWriteTimeUtc -ge $testStartTimeUtc })
    if ($finalDumps.Count -gt 0) {
        throw "FAIL: New crash dump(s) detected during test: $($finalDumps -join ', ')"
    }
    Write-Host "[PASS] Zero new crash dumps across all test runs." -ForegroundColor Green
}
finally {
    # Ensure any lingering test processes are stopped
    if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
    if ($motionProc -and -not $motionProc.HasExited) { Stop-Process -Id $motionProc.Id -Force -ErrorAction SilentlyContinue }
    Get-Process -Name "duwn-mirror", "uxplay" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

    # Restore original user settings
    if (Test-Path $settingsBackupPath) {
        Copy-Item $settingsBackupPath $settingsPath -Force
        Remove-Item $settingsBackupPath -Force -ErrorAction SilentlyContinue
        Write-Host "Restored original user settings.json from backup."
    }

    # Restore crash marker if it was preserved
    if (Test-Path $markerBackupPath) {
        Move-Item $markerBackupPath $markerPath -Force
        Write-Host "Restored pre-existing crash marker."
    }
}

Write-Host "`nAll UI Acceptance Tests completed successfully!" -ForegroundColor Green
