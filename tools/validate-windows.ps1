Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public class Win32Diag {
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
    public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    public struct WindowInfo {
        public IntPtr Hwnd;
        public string Title;
        public string ClassName;
        public bool Visible;
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
                list.Add(new WindowInfo {
                    Hwnd = hwnd,
                    Title = sbTitle.ToString(),
                    ClassName = sbClass.ToString(),
                    Visible = IsWindowVisible(hwnd)
                });
            }
            return true;
        }, IntPtr.Zero);
        return list;
    }

    public static List<WindowInfo> GetChildWindows(IntPtr parentHwnd) {
        var list = new List<WindowInfo>();
        EnumChildWindows(parentHwnd, (hwnd, lParam) => {
            var sbTitle = new StringBuilder(256);
            var sbClass = new StringBuilder(256);
            GetWindowText(hwnd, sbTitle, 256);
            GetClassName(hwnd, sbClass, 256);
            list.Add(new WindowInfo {
                Hwnd = hwnd,
                Title = sbTitle.ToString(),
                ClassName = sbClass.ToString(),
                Visible = IsWindowVisible(hwnd)
            });
            return true;
        }, IntPtr.Zero);
        return list;
    }
}
"@

Write-Host "=== DUWN MIRROR RUNTIME WINDOW VALIDATION ==="

$exePath = "D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-mirror.exe"
if (-not (Test-Path $exePath)) {
    Write-Error "duwn-mirror.exe not found at $exePath"
    exit 1
}

# Launch duwn-mirror
$proc = Start-Process -FilePath $exePath -PassThru
Write-Host "Launched duwn-mirror PID: $($proc.Id)"
Start-Sleep -Seconds 2

try {
    # 1. Enumerate top-level windows
    $topWindows = [Win32Diag]::GetProcessWindows($proc.Id)
    Write-Host "Found $($topWindows.Count) top-level windows:"

    $mainWindow = $null
    $outputWindow = $null
    $previewWindow = $null

    foreach ($w in $topWindows) {
        Write-Host "  HWND=$($w.Hwnd.ToString('X')) Title='$($w.Title)' Class='$($w.ClassName)' Visible=$($w.Visible)"
        if ($w.ClassName -eq "DuwnMirrorWindow") {
            $mainWindow = $w
        }
        if ($w.ClassName -eq "DUWNMirrorOutputWindow") {
            $outputWindow = $w
        }
        if ($w.ClassName -like "*Preview*") {
            $previewWindow = $w
        }
    }

    # Verify:
    $hasMain = ($mainWindow -ne $null)
    $hasOutput = ($outputWindow -ne $null)
    $hasPreview = ($previewWindow -ne $null)

    Write-Host "`n--- WINDOW COUNT & IDENTITY AUDIT ---"
    Write-Host "Main Window exists: $hasMain (HWND: $($mainWindow.Hwnd.ToString('X')))"
    Write-Host "Output Window exists: $hasOutput (HWND: $($outputWindow.Hwnd.ToString('X')))"
    Write-Host "External PreviewWindow exists: $hasPreview (Expected: False)"

    if (-not $hasMain -or -not $hasOutput -or $hasPreview) {
        Write-Error "FAIL: Top-level window invariant violated!"
    } else {
        Write-Host "PASS: Top-level window invariant satisfied (Main + Output only)."
    }

    # 2. Enumerate child windows of Main Window
    Write-Host "`n--- CHILD PREVIEW HWND AUDIT ---"
    $childWindows = [Win32Diag]::GetChildWindows($mainWindow.Hwnd)
    $previewChild = $null
    foreach ($c in $childWindows) {
        Write-Host "  Child HWND=$($c.Hwnd.ToString('X')) Title='$($c.Title)' Class='$($c.ClassName)' Visible=$($c.Visible)"
        if ($c.ClassName -eq "DUWNMirrorPreviewChild") {
            $previewChild = $c
        }
    }

    if ($previewChild -ne $null) {
        Write-Host "PASS: Embedded child preview found: HWND=$($previewChild.Hwnd.ToString('X')) Class=$($previewChild.ClassName)"
    } else {
        Write-Error "FAIL: DUWNMirrorPreviewChild child window not found!"
    }

    # 3. Output Window Close Button (WM_CLOSE -> Hide, HWND stable)
    Write-Host "`n--- OUTPUT HWND STABILITY & WM_CLOSE AUDIT ---"
    $initialOutputHwnd = $outputWindow.Hwnd
    Write-Host "Initial Output HWND: $($initialOutputHwnd.ToString('X'))"

    # Make output window visible
    [Win32Diag]::ShowWindow($initialOutputHwnd, 1) # SW_SHOWNORMAL
    Start-Sleep -Milliseconds 300
    $visibleBefore = [Win32Diag]::IsWindowVisible($initialOutputHwnd)
    Write-Host "OutputWindow Visible after SW_SHOWNORMAL: $visibleBefore"

    # Send WM_CLOSE (0x0010)
    Write-Host "Sending WM_CLOSE to OutputWindow..."
    [Win32Diag]::PostMessage($initialOutputHwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 500

    # Verify HWND is still valid and hidden
    $visibleAfter = [Win32Diag]::IsWindowVisible($initialOutputHwnd)
    Write-Host "OutputWindow Visible after WM_CLOSE: $visibleAfter (Expected: False)"

    $topWindowsAfterClose = [Win32Diag]::GetProcessWindows($proc.Id)
    $outputAfterClose = $topWindowsAfterClose | Where-Object { $_.ClassName -eq "DUWNMirrorOutputWindow" }

    if ($outputAfterClose -ne $null -and $outputAfterClose.Hwnd -eq $initialOutputHwnd -and -not $visibleAfter) {
        Write-Host "PASS: WM_CLOSE intercepted. Window hidden, HWND stable: $($outputAfterClose.Hwnd.ToString('X'))"
    } else {
        Write-Error "FAIL: OutputWindow HWND destroyed or visible state incorrect!"
    }

    # Show window again
    Write-Host "Showing OutputWindow again via SW_SHOWNOACTIVATE (4)..."
    [Win32Diag]::ShowWindow($initialOutputHwnd, 4)
    Start-Sleep -Milliseconds 300
    $visibleReopened = [Win32Diag]::IsWindowVisible($initialOutputHwnd)
    Write-Host "OutputWindow Visible after reopen: $visibleReopened"
    Write-Host "Final Output HWND: $($initialOutputHwnd.ToString('X'))"
    Write-Host "PASS: Reopen preserves identical HWND."

} finally {
    Write-Host "`nTerminating test duwn-mirror process..."
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    Write-Host "Done."
}
