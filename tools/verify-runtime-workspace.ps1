Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Add-Type -ReferencedAssemblies "System.Drawing.dll", "System.Windows.Forms.dll" -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public class Win32TestUtil {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumChildWindows(IntPtr hWndParent, EnumWindowsProc lpEnumFunc, IntPtr lParam);

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
    public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    public struct WindowInfo {
        public IntPtr Hwnd;
        public string Title;
        public string ClassName;
        public bool Visible;
        public int X, Y, Width, Height;
    }

    public static List<WindowInfo> GetProcessWindows(uint targetPid) {
        var list = new List<WindowInfo>();
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
                list.Add(new WindowInfo {
                    Hwnd = hwnd, Title = sbTitle.ToString(), ClassName = sbClass.ToString(),
                    Visible = IsWindowVisible(hwnd), X = rc.Left, Y = rc.Top,
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

Write-Host "=== VERIFY RUNTIME WORKSPACE V2 & SHARED TEXTURE EXPORT ==="

$exePath = "D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-mirror.exe"
$consumerPath = "D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-capture-consumer.exe"
$artifactDir = "D:\Projects\Duwn Mirror\artifacts"

if (-not (Test-Path $artifactDir)) { New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null }

# 1. Clean previous duwn instances
Get-Process duwn-mirror -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process uxplay -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

# 2. Launch duwn-mirror with --test-motion
Write-Host "Launching duwn-mirror.exe --test-motion..."
$proc = Start-Process -FilePath $exePath -ArgumentList "--test-motion" -PassThru
Start-Sleep -Seconds 2

try {
    # Verify top level windows
    $topWindows = [Win32TestUtil]::GetProcessWindows($proc.Id)
    $visibleCount = 0
    $mainHwnd = [IntPtr]::Zero
    foreach ($w in $topWindows) {
        Write-Host "Top Window: HWND=0x$($w.Hwnd.ToString('X')) Class='$($w.ClassName)' Title='$($w.Title)' Visible=$($w.Visible) Size=$($w.Width)x$($w.Height)"
        if ($w.Visible) {
            $visibleCount++
            if ($w.ClassName -eq "DUWNMirrorMainWindow") {
                $mainHwnd = $w.Hwnd
            }
        }
    }

    if ($visibleCount -ne 1) {
        Write-Error "VIOLATION: Expected exactly 1 visible top-level window, but found $visibleCount"
    } else {
        Write-Host "[PASS] Exactly one top-level window is visible to the user." -ForegroundColor Green
    }

    # Capture 1: Workspace Normal
    $shot1 = Join-Path $artifactDir "01_workspace_normal.png"
    [Win32TestUtil]::CaptureWindow($mainHwnd, $shot1)
    Write-Host "Captured: $shot1"

    # Start consumer in separate process in background
    Write-Host "Launching duwn-capture-consumer.exe in separate process..."
    $rawBmp = Join-Path $artifactDir "consumer_shared_texture.bmp"
    $consumerProc = Start-Process -FilePath $consumerPath -ArgumentList "--iterations 35 --reopen-test --export-raw-frame `"$rawBmp`"" -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $artifactDir "consumer_output.json") -RedirectStandardError (Join-Path $artifactDir "consumer_error.log")

    Start-Sleep -Milliseconds 800

    # Switch to Screen-Only mode (WM_APP + 0x020, wParam = 1)
    Write-Host "Switching to Screen-Only mode..."
    [Win32TestUtil]::PostMessage($mainHwnd, 0x8020, [IntPtr]1, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 800

    # Capture 2: Screen-Only mode
    $shot2 = Join-Path $artifactDir "02_screen_only.png"
    [Win32TestUtil]::CaptureWindow($mainHwnd, $shot2)
    Write-Host "Captured: $shot2"

    # Switch back to Workspace mode (WM_APP + 0x020, wParam = 0)
    Write-Host "Switching back to Workspace mode..."
    [Win32TestUtil]::PostMessage($mainHwnd, 0x8020, [IntPtr]0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 800

    # Capture 3: Back to Workspace
    $shot3 = Join-Path $artifactDir "03_back_to_workspace.png"
    [Win32TestUtil]::CaptureWindow($mainHwnd, $shot3)
    Write-Host "Captured: $shot3"

    # Resize window (e.g. 1360 x 860)
    Write-Host "Resizing MainWindow..."
    [Win32TestUtil]::SetWindowPos($mainHwnd, [IntPtr]::Zero, 100, 100, 1360, 860, 0x0014)
    Start-Sleep -Milliseconds 800

    # Capture 4: Resized
    $shot4 = Join-Path $artifactDir "04_resized_window.png"
    [Win32TestUtil]::CaptureWindow($mainHwnd, $shot4)
    Write-Host "Captured: $shot4"

    # Switch to Settings tab (WM_APP + 0x021, wParam = 4)
    Write-Host "Switching to Settings tab..."
    [Win32TestUtil]::PostMessage($mainHwnd, 0x8021, [IntPtr]4, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 800

    # Capture 5: Tab switch (Settings)
    $shot5 = Join-Path $artifactDir "05_tab_switch_settings.png"
    [Win32TestUtil]::CaptureWindow($mainHwnd, $shot5)
    Write-Host "Captured: $shot5"

    # Switch back to Mirror tab (WM_APP + 0x021, wParam = 0)
    Write-Host "Switching back to Mirror tab..."
    [Win32TestUtil]::PostMessage($mainHwnd, 0x8021, [IntPtr]0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 800

    # Capture 6: Tab switch back (Mirror)
    $shot6 = Join-Path $artifactDir "06_tab_switch_mirror.png"
    [Win32TestUtil]::CaptureWindow($mainHwnd, $shot6)
    Write-Host "Captured: $shot6"

    # Wait for consumer process to finish
    Write-Host "Waiting for duwn-capture-consumer process..."
    $consumerProc.WaitForExit(10000)
    Write-Host "Consumer ExitCode: $($consumerProc.ExitCode)"

    $consumerJson = Get-Content (Join-Path $artifactDir "consumer_output.json") -Raw -ErrorAction SilentlyContinue
    Write-Host "Consumer Result:"
    Write-Host $consumerJson
}
finally {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    Get-Process duwn-mirror -ErrorAction SilentlyContinue | Stop-Process -Force
    Get-Process uxplay -ErrorAction SilentlyContinue | Stop-Process -Force
}
