<#
.SYNOPSIS
    Runs a test executable on a fresh, NON-interactive window station, the way the CI runner's
    service (NETWORK SERVICE, Session 0) runs it: no visible desktop, nothing on any screen.

        powershell -ExecutionPolicy Bypass -File dotnet\hybrid-spike\uitest\run_without_desktop.ps1 <exe>

    Only WinSta0 is ever interactive. A window station this script creates is not, so a process started
    on it can create windows but nothing it draws is ever displayed, and it has no access to the real
    mouse or keyboard. This is a local stand-in for Session 0, not Session 0 itself: the account and
    session differ. The output of the exe is printed, and the script exits with its exit code.
#>
param([Parameter(Mandatory)][string]$Exe)
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class OffDesktop
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct STARTUPINFO
    {
        public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateWindowStation(string name, int flags, uint access, IntPtr sa);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateDesktop(string name, IntPtr device, IntPtr devmode, int flags, uint access, IntPtr sa);
    [DllImport("user32.dll", SetLastError = true)] static extern IntPtr GetProcessWindowStation();
    [DllImport("user32.dll", SetLastError = true)] static extern bool SetProcessWindowStation(IntPtr h);
    [DllImport("user32.dll", SetLastError = true)] static extern bool CloseWindowStation(IntPtr h);
    [DllImport("user32.dll", SetLastError = true)] static extern bool CloseDesktop(IntPtr h);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool CreateProcess(string app, string cmd, IntPtr pa, IntPtr ta, bool inherit, int flags,
                                     IntPtr env, string dir, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll")] static extern bool GetExitCodeProcess(IntPtr h, out int code);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("user32.dll", SetLastError = true)]
    static extern bool GetUserObjectInformation(IntPtr h, int index, byte[] info, int len, out int needed);

    const uint WINSTA_ALL_ACCESS = 0x37F | 0xF0000;   // every window-station right + standard rights
    const uint DESKTOP_ALL_ACCESS = 0x1FF | 0xF0000;

    /// <summary>Starts `commandLine` on a new window station + desktop, waits, returns its exit code. If this
    /// account may not create a window station (ERROR_ACCESS_DENIED for an ordinary user on this machine),
    /// falls back to a new desktop on the CURRENT window station that is never switched to: still nothing
    /// displayed and no input, but the window station stays the interactive one. `where` says which ran.</summary>
    public static int Run(string station, string commandLine, string dir, int timeoutMs, out string where)
    {
        IntPtr original = GetProcessWindowStation();
        IntPtr winsta = CreateWindowStation(station, 0, WINSTA_ALL_ACCESS, IntPtr.Zero);
        int stationError = winsta == IntPtr.Zero ? Marshal.GetLastWin32Error() : 0;
        IntPtr desk = IntPtr.Zero;
        try
        {
            string target;
            if (winsta != IntPtr.Zero)
            {
                // A desktop is created on the CALLING process's window station, so switch to it briefly.
                if (!SetProcessWindowStation(winsta)) { throw new Exception("SetProcessWindowStation failed: " + Marshal.GetLastWin32Error()); }
                desk = CreateDesktop("Default", IntPtr.Zero, IntPtr.Zero, 0, DESKTOP_ALL_ACCESS, IntPtr.Zero);
                int err = Marshal.GetLastWin32Error();
                SetProcessWindowStation(original);
                if (desk == IntPtr.Zero) { throw new Exception("CreateDesktop failed: " + err); }

                // UOI_FLAGS (1): WSF_VISIBLE (1) is set only on an interactive window station.
                var flags = new byte[12];
                int needed;
                GetUserObjectInformation(winsta, 1, flags, flags.Length, out needed);
                bool visible = (BitConverter.ToInt32(flags, 8) & 1) != 0;
                target = station + "\\Default";
                where = "new window station " + target + " (interactive: " + (visible ? "YES" : "no") + ")";
            }
            else
            {
                desk = CreateDesktop(station, IntPtr.Zero, IntPtr.Zero, 0, DESKTOP_ALL_ACCESS, IntPtr.Zero);
                if (desk == IntPtr.Zero) { throw new Exception("CreateDesktop failed: " + Marshal.GetLastWin32Error()); }
                target = "WinSta0\\" + station;
                where = "hidden desktop " + target + " on the interactive window station (a window station of its own was refused, error "
                      + stationError + ")";
            }

            var si = new STARTUPINFO { cb = Marshal.SizeOf(typeof(STARTUPINFO)), lpDesktop = target };
            PROCESS_INFORMATION pi;
            if (!CreateProcess(null, commandLine, IntPtr.Zero, IntPtr.Zero, false, 0x08000000 /* CREATE_NO_WINDOW */,
                               IntPtr.Zero, dir, ref si, out pi))
            {
                throw new Exception("CreateProcess failed: " + Marshal.GetLastWin32Error());
            }
            try
            {
                if (WaitForSingleObject(pi.hProcess, (uint)timeoutMs) != 0) { return -2; }
                int code;
                GetExitCodeProcess(pi.hProcess, out code);
                return code;
            }
            finally { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
        }
        finally
        {
            if (desk != IntPtr.Zero) { CloseDesktop(desk); }
            if (winsta != IntPtr.Zero) { CloseWindowStation(winsta); }
        }
    }
}
'@

$exePath = (Resolve-Path $Exe).Path
$dir = Split-Path $exePath
$out = Join-Path ([IO.Path]::GetTempPath()) ("offdesk_{0}.txt" -f [guid]::NewGuid().ToString('N'))
$cmd = 'cmd.exe /c ""{0}" > "{1}" 2>&1"' -f $exePath, $out
$station = 'TmdqNoDesk' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$sw = [Diagnostics.Stopwatch]::StartNew()
$where = ''
$code = [OffDesktop]::Run($station, $cmd, $dir, 300000, [ref]$where)
"ran on $where in {0:F2} s, exit code {1}" -f $sw.Elapsed.TotalSeconds, $code
if (Test-Path $out) { Get-Content $out; Remove-Item $out } else { 'no output captured' }
exit $code
