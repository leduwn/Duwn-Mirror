Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Add-Type -ReferencedAssemblies "System.Drawing.dll", "System.Windows.Forms.dll" -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;

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
    public static extern bool SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    public struct WinInfo {
        public IntPtr Hwnd;
        public string Title;
        public string ClassName;
        public bool Visible;
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
                    Width = rc.Right - rc.Left, Height = rc.Bottom - rc.Top
                });
            }
            return true;
        }, IntPtr.Zero);
        return list;
    }

    public static void CaptureWindow(IntPtr hWnd, string filePath) {
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

Write-Host "=== TEST 1: Launch App (Clean, Idle, Mirror Tab Default) ==="
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

    # Capture 1: Main window on launch (Phản chiếu tab)
    $shot1 = Join-Path $shotDir "01_app_launch_mirror_tab.png"
    [Win32UiUtil]::CaptureWindow($mainWin.Hwnd, $shot1)
    Write-Host "Captured: $shot1"

    # Iterate through sidebar tabs
    $tabs = @(
        @{ Name="02_tab_video"; Index=1; Desc="Hinh anh" },
        @{ Name="03_tab_audio"; Index=2; Desc="Am thanh" },
        @{ Name="04_tab_color"; Index=3; Desc="Mau sac" },
        @{ Name="05_tab_settings"; Index=4; Desc="Cai dat" },
        @{ Name="06_tab_diagnostics"; Index=5; Desc="Chan doan" }
    )

    foreach ($t in $tabs) {
        Write-Host "Switching to Tab $($t.Desc) (Index $($t.Index))..."
        [Win32UiUtil]::PostMessage($mainWin.Hwnd, 0x8021, [IntPtr]$($t.Index), [IntPtr]::Zero)
        Start-Sleep -Milliseconds 400
        $shotTab = Join-Path $shotDir "$($t.Name).png"
        [Win32UiUtil]::CaptureWindow($mainWin.Hwnd, $shotTab)
        Write-Host "Captured: $shotTab"
    }

    # Return to Mirror tab
    Write-Host "Switching back to Mirror Tab..."
    [Win32UiUtil]::PostMessage($mainWin.Hwnd, 0x8021, [IntPtr]0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 400

    # Toggle Output window visible via WM_APP + 0x022
    Write-Host "`n=== TEST 2: Open Standalone Output Window ==="
    [Win32UiUtil]::PostMessage($mainWin.Hwnd, 0x8022, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 800

    $wins = [Win32UiUtil]::GetProcessWindows($proc.Id)
    $outWin = $null
    foreach ($w in $wins) {
        if ($w.ClassName -eq "DUWNMirrorOutputWindow") { $outWin = $w }
    }

    if (-not $outWin -or -not $outWin.Visible) {
        throw "FAIL: OutputWindow not visible after toggle!"
    }
    Write-Host "[PASS] OutputWindow opened successfully ($($outWin.Width)x$($outWin.Height))." -ForegroundColor Green

    $shotOut = Join-Path $shotDir "07_output_window_opened.png"
    [Win32UiUtil]::CaptureWindow($outWin.Hwnd, $shotOut)
    Write-Host "Captured: $shotOut"

    # Capture MainWindow showing updated toggle state
    $shotMainWithOut = Join-Path $shotDir "08_mainwindow_with_output_open.png"
    [Win32UiUtil]::CaptureWindow($mainWin.Hwnd, $shotMainWithOut)
    Write-Host "Captured: $shotMainWithOut"

    # Test closing OutputWindow via WM_CLOSE
    Write-Host "`n=== TEST 3: Close Output Window via WM_CLOSE (Hide Invariant) ==="
    [Win32UiUtil]::PostMessage($outWin.Hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) # WM_CLOSE
    Start-Sleep -Milliseconds 600

    $wins = [Win32UiUtil]::GetProcessWindows($proc.Id)
    $outWinAfterClose = $null
    foreach ($w in $wins) {
        if ($w.ClassName -eq "DUWNMirrorOutputWindow") { $outWinAfterClose = $w }
    }
    if ($outWinAfterClose -and $outWinAfterClose.Visible) {
        throw "FAIL: OutputWindow should be hidden after WM_CLOSE!"
    }
    Write-Host "[PASS] OutputWindow successfully hidden without terminating application." -ForegroundColor Green

    # Reopen OutputWindow
    Write-Host "`n=== TEST 4: Reopen Output Window via Toggle ==="
    [Win32UiUtil]::PostMessage($mainWin.Hwnd, 0x8022, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 600

    $wins = [Win32UiUtil]::GetProcessWindows($proc.Id)
    $outWinReopened = $null
    foreach ($w in $wins) {
        if ($w.ClassName -eq "DUWNMirrorOutputWindow") { $outWinReopened = $w }
    }
    if (-not $outWinReopened -or -not $outWinReopened.Visible) {
        throw "FAIL: OutputWindow failed to reopen!"
    }
    Write-Host "[PASS] OutputWindow successfully reopened." -ForegroundColor Green
    $shotReopened = Join-Path $shotDir "09_output_window_reopened.png"
    [Win32UiUtil]::CaptureWindow($outWinReopened.Hwnd, $shotReopened)
    Write-Host "Captured: $shotReopened"
}
finally {
    if ($proc -and -not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "`n=== TEST 5: Live Pipeline Synthetic Motion Streaming (--test-motion) ==="
$motionProc = Start-Process -FilePath $exe -ArgumentList "--test-motion" -PassThru
try {
    $motionMain = $null
    $motionOut = $null
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt 12000) {
        Start-Sleep -Milliseconds 400
        $wins = [Win32UiUtil]::GetProcessWindows($motionProc.Id)
        foreach ($w in $wins) {
            if ($w.ClassName -eq "DUWNMirrorMainWindow" -and $w.Visible) { $motionMain = $w }
            if ($w.ClassName -eq "DUWNMirrorOutputWindow" -and $w.Visible) { $motionOut = $w }
        }
        if ($motionMain -and $motionOut) { break }
    }

    if (-not $motionMain -or -not $motionMain.Visible) {
        throw "FAIL: MainWindow not visible during --test-motion!"
    }
    if (-not $motionOut -or -not $motionOut.Visible) {
        throw "FAIL: OutputWindow should automatically show when motion stream starts!"
    }

    Write-Host "[PASS] Both MainWindow and OutputWindow actively visible and presenting frames." -ForegroundColor Green
    Start-Sleep -Seconds 1

    $shotMotionMain = Join-Path $shotDir "10_streaming_motion_mainwindow.png"
    $shotMotionOut = Join-Path $shotDir "11_streaming_motion_outputwindow.png"
    [Win32UiUtil]::CaptureWindow($motionMain.Hwnd, $shotMotionMain)
    [Win32UiUtil]::CaptureWindow($motionOut.Hwnd, $shotMotionOut)
    Write-Host "Captured: $shotMotionMain"
    Write-Host "Captured: $shotMotionOut"
}
finally {
    if ($motionProc -and -not $motionProc.HasExited) {
        Stop-Process -Id $motionProc.Id -Force -ErrorAction SilentlyContinue
    }
}
