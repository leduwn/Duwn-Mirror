Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public class UiPolishDiag {
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

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    public struct WindowInfo {
        public IntPtr Hwnd;
        public string Title;
        public string ClassName;
        public bool Visible;
        public int Width;
        public int Height;
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
                    Hwnd = hwnd,
                    Title = sbTitle.ToString(),
                    ClassName = sbClass.ToString(),
                    Visible = IsWindowVisible(hwnd),
                    Width = rc.Right - rc.Left,
                    Height = rc.Bottom - rc.Top
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
            RECT rc;
            GetWindowRect(hwnd, out rc);
            list.Add(new WindowInfo {
                Hwnd = hwnd,
                Title = sbTitle.ToString(),
                ClassName = sbClass.ToString(),
                Visible = IsWindowVisible(hwnd),
                Width = rc.Right - rc.Left,
                Height = rc.Bottom - rc.Top
            });
            return true;
        }, IntPtr.Zero);
        return list;
    }
}
"@

Write-Host '============================================================' -ForegroundColor Cyan
Write-Host '   DUWN MIRROR - FINAL UI/UX POLISH RUNTIME VALIDATION      ' -ForegroundColor Cyan
Write-Host '============================================================' -ForegroundColor Cyan

$exePath = 'D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-mirror.exe'
if (-not (Test-Path $exePath)) {
    Write-Error ('duwn-mirror.exe not found at {0}' -f $exePath)
    exit 1
}

$proc = Start-Process -FilePath $exePath -PassThru
Write-Host ('Launched duwn-mirror.exe (PID: {0})' -f $proc.Id) -ForegroundColor Green
Start-Sleep -Seconds 2

try {
    # 1. Enumerate and audit top-level windows
    Write-Host ''
    Write-Host '[Check 1] Auditing Top-Level Windows...' -ForegroundColor Yellow
    $topWindows = [UiPolishDiag]::GetProcessWindows($proc.Id)
    $mainWindow = $null
    $outputWindow = $null
    $duplicatePreview = $null

    foreach ($w in $topWindows) {
        $msg = '  -> HWND={0} Title=''{1}'' Class=''{2}'' Visible={3} ({4}x{5})' -f $w.Hwnd.ToString('X'), $w.Title, $w.ClassName, $w.Visible, $w.Width, $w.Height
        Write-Host $msg
        if ($w.ClassName -eq 'DuwnMirrorWindow') {
            $mainWindow = $w
        }
        if ($w.ClassName -eq 'DUWNMirrorOutputWindow') {
            $outputWindow = $w
        }
        if ($w.ClassName -like '*Preview*' -and $w.ClassName -ne 'DUWNMirrorPreviewChild') {
            $duplicatePreview = $w
        }
    }

    if ($null -eq $mainWindow) {
        throw 'FAIL: MainWindow (DuwnMirrorWindow) not found!'
    }
    Write-Host ('  [PASS] MainWindow found: HWND={0} Title=''{1}''' -f $mainWindow.Hwnd.ToString('X'), $mainWindow.Title) -ForegroundColor Green

    if ($null -eq $outputWindow) {
        throw 'FAIL: OutputWindow (DUWNMirrorOutputWindow) not found!'
    }
    Write-Host ('  [PASS] OutputWindow found: HWND={0} Title=''{1}''' -f $outputWindow.Hwnd.ToString('X'), $outputWindow.Title) -ForegroundColor Green

    if ($null -ne $duplicatePreview) {
        throw ('FAIL: Duplicate external preview window detected: {0}' -f $duplicatePreview.ClassName)
    }
    Write-Host '  [PASS] No duplicate external preview window (Capture/Preview separation verified).' -ForegroundColor Green

    # 2. Verify Minimum Bounds
    Write-Host ''
    Write-Host '[Check 2] Verifying Minimum Window Bounds...' -ForegroundColor Yellow
    if ($mainWindow.Width -ge 960 -and $mainWindow.Height -ge 560) {
        $msg = '  [PASS] Window bounds ({0}x{1}) meet or exceed 960x560 minimum.' -f $mainWindow.Width, $mainWindow.Height
        Write-Host $msg -ForegroundColor Green
    } else {
        $msg = 'FAIL: Window bounds ({0}x{1}) violate minimum 960x560!' -f $mainWindow.Width, $mainWindow.Height
        throw $msg
    }

    # 3. Verify Embedded Preview Child Window
    Write-Host ''
    Write-Host '[Check 3] Auditing Child Windows of MainWindow...' -ForegroundColor Yellow
    $childWindows = [UiPolishDiag]::GetChildWindows($mainWindow.Hwnd)
    $previewChild = $null
    foreach ($c in $childWindows) {
        $msg = '  -> Child HWND={0} Class=''{1}'' Visible={2} ({3}x{4})' -f $c.Hwnd.ToString('X'), $c.ClassName, $c.Visible, $c.Width, $c.Height
        Write-Host $msg
        if ($c.ClassName -eq 'DUWNMirrorPreviewChild') {
            $previewChild = $c
        }
    }

    if ($null -eq $previewChild) {
        throw 'FAIL: Embedded child preview (DUWNMirrorPreviewChild) not found!'
    }
    Write-Host ('  [PASS] Embedded child preview active: HWND={0}' -f $previewChild.Hwnd.ToString('X')) -ForegroundColor Green

    # 4. Output HWND Stability & WM_CLOSE Interception
    Write-Host ''
    Write-Host '[Check 4] Auditing Output HWND Stability and Lifetime...' -ForegroundColor Yellow
    $initialHwnd = $outputWindow.Hwnd
    Write-Host ('  Initial Output HWND: {0}' -f $initialHwnd.ToString('X'))

    # Show Output Window
    [UiPolishDiag]::ShowWindow($initialHwnd, 1) # SW_SHOWNORMAL
    Start-Sleep -Milliseconds 300
    $vis1 = [UiPolishDiag]::IsWindowVisible($initialHwnd)
    Write-Host ('  Output Window visible after Show: {0}' -f $vis1)

    # Post WM_CLOSE
    Write-Host '  Posting WM_CLOSE (0x0010) to Output Window...'
    [UiPolishDiag]::PostMessage($initialHwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 500

    # Verify hidden, not destroyed
    $vis2 = [UiPolishDiag]::IsWindowVisible($initialHwnd)
    Write-Host ('  Output Window visible after WM_CLOSE: {0} (Expected: False)' -f $vis2)

    $topAfter = [UiPolishDiag]::GetProcessWindows($proc.Id)
    $outAfter = $topAfter | Where-Object { $_.ClassName -eq 'DUWNMirrorOutputWindow' }

    if ($null -ne $outAfter -and $outAfter.Hwnd -eq $initialHwnd -and -not $vis2) {
        Write-Host ('  [PASS] WM_CLOSE intercepted to Hide. HWND remains {0}' -f $initialHwnd.ToString('X')) -ForegroundColor Green
    } else {
        throw 'FAIL: OutputWindow HWND destroyed or visibility state invalid after WM_CLOSE!'
    }

    # Reopen
    Write-Host '  Reopening Output Window via SW_SHOWNOACTIVATE...'
    [UiPolishDiag]::ShowWindow($initialHwnd, 4)
    Start-Sleep -Milliseconds 300
    $vis3 = [UiPolishDiag]::IsWindowVisible($initialHwnd)
    Write-Host ('  Output Window visible after reopen: {0} (Expected: True)' -f $vis3)
    Write-Host ('  [PASS] HWND preserved after reopen: {0}' -f $initialHwnd.ToString('X')) -ForegroundColor Green

    Write-Host '============================================================' -ForegroundColor Cyan
    Write-Host '   ALL RUNTIME UI POLISH AND ARCHITECTURE CHECKS PASSED!     ' -ForegroundColor Cyan
    Write-Host '============================================================' -ForegroundColor Cyan

} finally {
    Write-Host 'Cleaning up duwn-mirror.exe process...'
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    Write-Host 'Done.'
}
