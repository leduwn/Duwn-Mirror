using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

public class WinUtil {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Auto)] public static extern int GetClassName(IntPtr hWnd, StringBuilder sb, int max);
    [DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Auto)] public static extern int GetWindowText(IntPtr hWnd, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rc);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rc);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT pt);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int X, int Y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, UIntPtr extra);
    [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] public static extern IntPtr GetWindowLongPtr64(IntPtr hWnd, int nIndex);

    public static int GetClientW(IntPtr hWnd) { RECT rc; GetClientRect(hWnd, out rc); return rc.Right - rc.Left; }
    public static int GetClientH(IntPtr hWnd) { RECT rc; GetClientRect(hWnd, out rc); return rc.Bottom - rc.Top; }

    public static void RealClick(IntPtr hWnd, int clientX, int clientY) {
        ShowWindow(hWnd, 9);
        SetForegroundWindow(hWnd);
        Thread.Sleep(80);
        POINT pt = new POINT { X = clientX, Y = clientY };
        ClientToScreen(hWnd, ref pt);
        SetCursorPos(pt.X, pt.Y);
        Thread.Sleep(80);
        mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(80);
        mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
        Thread.Sleep(150);
    }

    public static void Capture(IntPtr hWnd, string filePath) {
        ShowWindow(hWnd, 9);
        SetForegroundWindow(hWnd);
        Thread.Sleep(120);
        RECT rc;
        GetWindowRect(hWnd, out rc);
        int w = rc.Right - rc.Left, h = rc.Bottom - rc.Top;
        if (w <= 0 || h <= 0) return;
        using (var bmp = new System.Drawing.Bitmap(w, h)) {
            using (var g = System.Drawing.Graphics.FromImage(bmp)) {
                g.CopyFromScreen(rc.Left, rc.Top, 0, 0, new System.Drawing.Size(w, h));
            }
            bmp.Save(filePath, System.Drawing.Imaging.ImageFormat.Png);
        }
    }

    public struct WinData { public IntPtr Hwnd; public string Title; public string ClassName; public int Width, Height; }

    public static List<WinData> GetWins(uint pid) {
        var list = new List<WinData>();
        EnumWindows((hwnd, lParam) => {
            uint procId;
            GetWindowThreadProcessId(hwnd, out procId);
            if (procId == pid && IsWindowVisible(hwnd)) {
                var t = new StringBuilder(256); var c = new StringBuilder(256);
                GetWindowText(hwnd, t, 256); GetClassName(hwnd, c, 256);
                RECT rc; GetWindowRect(hwnd, out rc);
                list.Add(new WinData { Hwnd = hwnd, Title = t.ToString(), ClassName = c.ToString(), Width = rc.Right - rc.Left, Height = rc.Bottom - rc.Top });
            }
            return true;
        }, IntPtr.Zero);
        return list;
    }
}