// fdel - delete any file or folder, even one in use by another running app.
//
// Safe-by-default escalation ladder (each risky rung is opt-in):
//   1. Strip ReadOnly/Hidden/System attributes.
//   2. Try a normal delete (fast path).
//   3. If locked, identify the process(es) holding it and name them.
//   4. Offer, in order of increasing risk:
//        - close the holding app(s) normally (WM_CLOSE),
//        - end the holding process(es),
//        - force-close the file handles inside them
//          (DuplicateHandle + DUPLICATE_CLOSE_SOURCE) - ADVANCED, can corrupt
//          the other app's open data, so it is gated behind explicit consent.
//      Retry the delete after each step.
//   5. Otherwise stop and let the user delete it later. fdel NEVER reboots.
//
// Refuses protected Windows/system locations outright.
// Needs Administrator to act on handles owned by other users/services.

using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

internal static class Fdel
{
    private const int Retries = 3;

    private static int Main(string[] args)
    {
        var targets = new List<string>();
        var opt = new Options();

        foreach (var a in args)
        {
            switch (a.ToLowerInvariant())
            {
                case "-y": case "/y": case "--yes": opt.Yes = true; break;
                case "--close": opt.AutoClose = true; break;
                case "--kill": opt.AutoKill = true; break;
                case "--force": opt.AutoForce = true; break;
                case "-h": case "/?": case "--help": Usage(); return 0;
                default:
                    if (a.StartsWith("-")) { Console.Error.WriteLine("unknown option: " + a); return 1; }
                    targets.Add(a); break;
            }
        }
        opt.Interactive = !opt.Yes; // prompts only when not auto-confirmed

        if (targets.Count == 0) { Usage(); return 1; }

        TryEnablePrivilege("SeDebugPrivilege");

        int failures = 0;
        foreach (var raw in targets)
        {
            string path;
            try { path = Path.GetFullPath(raw); }
            catch { Console.Error.WriteLine("skip (bad path): " + raw); failures++; continue; }

            bool isDir = Directory.Exists(path);
            bool isFile = File.Exists(path);
            if (!isDir && !isFile)
            {
                Console.Error.WriteLine("not found: " + path);
                failures++;
                continue;
            }

            string why;
            if (IsProtected(path, out why))
            {
                Console.Error.WriteLine("REFUSED (protected system location): " + path);
                Console.Error.WriteLine("  " + why);
                failures++;
                continue;
            }

            if (opt.Interactive)
            {
                Console.Write("Delete " + (isDir ? "FOLDER" : "file") + " \"" + path + "\" ? [y/N] ");
                if (!YesAnswer(Console.ReadLine())) { Console.WriteLine("skipped."); continue; }
            }

            if (DeleteWithEscalation(path, isDir, opt)) Console.WriteLine("deleted: " + path);
            else { Console.Error.WriteLine("FAILED (still locked): " + path); failures++; }
        }
        return failures == 0 ? 0 : 2;
    }

    private sealed class Options
    {
        public bool Yes, Interactive, AutoClose, AutoKill, AutoForce;
    }

    private static bool YesAnswer(string s)
    {
        if (s == null) return false;
        s = s.Trim().ToLowerInvariant();
        return s == "y" || s == "yes";
    }

    private static void Usage()
    {
        Console.WriteLine("fdel - delete a file or folder, even if it's in use by another app.");
        Console.WriteLine();
        Console.WriteLine("  fdel <path> [more paths...] [options]");
        Console.WriteLine();
        Console.WriteLine("Safe by default. If the target is locked, fdel names the apps holding");
        Console.WriteLine("it and lets you choose how to proceed (close them, end them, or force).");
        Console.WriteLine("Protected Windows/system locations are refused. fdel NEVER reboots the PC.");
        Console.WriteLine();
        Console.WriteLine("Options:");
        Console.WriteLine("  -y        delete without the confirmation prompt");
        Console.WriteLine("  --close   if locked, ask the holding apps to close normally");
        Console.WriteLine("  --kill    if still locked, end the holding processes");
        Console.WriteLine("  --force   force-close the file handles (ADVANCED, may corrupt");
        Console.WriteLine("            the other app's data - last resort)");
        Console.WriteLine("  -h        this help");
        Console.WriteLine();
        Console.WriteLine("With -y and no escalation flag, fdel reports the holders and stops");
        Console.WriteLine("rather than doing anything risky on its own.");
        Console.WriteLine();
        Console.WriteLine("Run as Administrator to act on files held by other users/services.");
    }

    // ---- safety guard: never nuke the OS ---------------------------------

    // Refuse if the target IS, or CONTAINS, a location Windows needs to boot.
    // This is deliberately conservative: better to refuse a borderline path
    // than to brick the machine.
    private static bool IsProtected(string path, out string reason)
    {
        reason = null;
        string p = path.TrimEnd('\\', '/').ToLowerInvariant();

        // A bare drive root, e.g. "c:" or "c:\".
        if (p.Length <= 2 && p.EndsWith(":"))
        {
            reason = "cannot delete a whole drive root.";
            return true;
        }

        string sysDrive = (Environment.GetEnvironmentVariable("SystemDrive") ?? "C:").ToLowerInvariant();

        // "full" dirs: the directory itself, anything inside it, and any parent
        // that contains it are all off-limits (these hold OS/program binaries).
        var full = new List<string>();
        Action<string> add = env =>
        {
            var v = Environment.GetEnvironmentVariable(env);
            if (!string.IsNullOrEmpty(v)) full.Add(v.TrimEnd('\\', '/').ToLowerInvariant());
        };
        add("SystemRoot");        // C:\Windows
        add("windir");            // C:\Windows
        add("ProgramFiles");      // C:\Program Files
        add("ProgramFiles(x86)"); // C:\Program Files (x86)
        add("ProgramData");       // C:\ProgramData
        full.Add(sysDrive + "\\windows");
        full.Add(sysDrive + "\\system volume information");
        full.Add(sysDrive + "\\$recycle.bin");
        full.Add(sysDrive + "\\boot");
        full.Add(sysDrive + "\\recovery");
        full.Add(sysDrive + "\\perflogs");

        foreach (var c in full)
        {
            if (string.IsNullOrEmpty(c)) continue;
            if (p == c) { reason = "this is a protected system directory."; return true; }
            if (p.StartsWith(c + "\\"))
            {
                reason = "this lives inside a protected system directory (" + c + ").";
                return true;
            }
            if (c.StartsWith(p + "\\"))
            {
                reason = "deleting this would take out a protected system directory (" + c + ").";
                return true;
            }
        }

        // "root-only" dirs: the folder itself (and any parent of it) is off-limits,
        // but files/folders INSIDE are the user's own data and allowed. Deleting
        // all of C:\Users wipes every profile; deleting your Downloads is fine.
        var rootOnly = new List<string> { sysDrive + "\\users" };
        foreach (var c in rootOnly)
        {
            if (p == c) { reason = "cannot delete the whole \"" + c + "\" tree."; return true; }
            if (c.StartsWith(p + "\\"))
            {
                reason = "deleting this would take out \"" + c + "\".";
                return true;
            }
        }
        return false;
    }

    // ---- main delete routine ---------------------------------------------

    // Safe escalation ladder:
    //   attributes -> plain delete -> identify holders -> (close) -> (kill)
    //   -> (force handle close) -> (reboot). Each risky rung is opt-in: chosen
    //   from a menu when interactive, or enabled by a flag when automated.
    private static bool DeleteWithEscalation(string path, bool isDir, Options opt)
    {
        ClearAttributes(path, isDir);
        if (TryDelete(path, isDir)) return true;

        var files = new List<string>();
        if (isDir) CollectFiles(path, files); else files.Add(path);

        // Who is holding it?
        var holders = HandleCloser.FindHolders(files);
        if (holders.Count == 0)
        {
            Console.WriteLine("  locked, but no owning process could be identified");
            Console.WriteLine("  (it may be held by a driver or the system itself).");
        }
        else
        {
            Console.WriteLine("  locked by " + holders.Count + " process(es):");
            foreach (var h in holders)
                Console.WriteLine("    PID " + h.Pid + "  " + h.Name);
        }

        // Decide escalation. Interactive => menu. Automated => flags.
        while (true)
        {
            string choice = opt.Interactive
                ? AskMenu(holders.Count > 0)
                : PickFromFlags(opt);

            switch (choice)
            {
                case "close":
                    GracefulClose(holders);
                    if (RetryDelete(path, isDir)) return true;
                    if (!opt.Interactive) opt.AutoClose = false; // don't loop on flags
                    break;

                case "kill":
                    KillHolders(holders);
                    if (RetryDelete(path, isDir)) return true;
                    if (!opt.Interactive) opt.AutoKill = false;
                    break;

                case "force":
                    if (holders.Count == 0)
                    {
                        Console.WriteLine("  no identified holder to force - nothing to do.");
                        if (!opt.Interactive) opt.AutoForce = false;
                        break;
                    }
                    if (opt.Interactive && !ConfirmForce()) break;
                    var pids = new HashSet<int>();
                    foreach (var h in holders) pids.Add(h.Pid);
                    HandleCloser.CloseHandlesTo(files, pids);
                    if (RetryDelete(path, isDir)) return true;
                    if (!opt.Interactive) opt.AutoForce = false;
                    break;

                case "skip":
                default:
                    return false;
            }

            // Re-list holders after an action, so the next menu is accurate.
            if (opt.Interactive)
            {
                holders = HandleCloser.FindHolders(files);
                if (holders.Count > 0)
                {
                    Console.WriteLine("  still locked by:");
                    foreach (var h in holders) Console.WriteLine("    PID " + h.Pid + "  " + h.Name);
                }
                else Console.WriteLine("  no live holders remain, but delete still failed.");
            }
        }
    }

    private static string AskMenu(bool haveHolders)
    {
        Console.WriteLine();
        Console.WriteLine("  How do you want to proceed?");
        if (haveHolders)
        {
            Console.WriteLine("    [1] Close the app(s) normally  (safe)");
            Console.WriteLine("    [2] End the app(s)             (they lose unsaved work)");
        }
        Console.WriteLine("    [3] Force-close the file handles  (ADVANCED - may corrupt the app)");
        Console.WriteLine("    [4] Skip / cancel  (delete it yourself later)");
        Console.Write("  Choice: ");
        var a = (Console.ReadLine() ?? "").Trim();
        switch (a)
        {
            case "1": return haveHolders ? "close" : "skip";
            case "2": return haveHolders ? "kill" : "skip";
            case "3": return "force";
            default:  return "skip";
        }
    }

    // Automated escalation: honor flags in increasing order of risk, once each.
    private static string PickFromFlags(Options opt)
    {
        if (opt.AutoClose) return "close";
        if (opt.AutoKill)  return "kill";
        if (opt.AutoForce) return "force";
        return "skip";
    }

    private static bool ConfirmForce()
    {
        Console.WriteLine();
        Console.WriteLine("  WARNING: force-closing a file handle inside a running app can");
        Console.WriteLine("  corrupt that app's open data and crash it. Only do this if you");
        Console.WriteLine("  understand the risk.");
        Console.Write("  Type FORCE to continue: ");
        return (Console.ReadLine() ?? "").Trim() == "FORCE";
    }

    private static bool RetryDelete(string path, bool isDir)
    {
        for (int i = 0; i < Retries; i++)
        {
            if (TryDelete(path, isDir)) return true;
            System.Threading.Thread.Sleep(120);
        }
        return false;
    }

    private static void GracefulClose(List<HandleCloser.Holder> holders)
    {
        foreach (var h in holders)
        {
            try
            {
                var p = System.Diagnostics.Process.GetProcessById(h.Pid);
                if (p.CloseMainWindow())
                {
                    p.WaitForExit(4000);
                    Console.WriteLine("  asked PID " + h.Pid + " (" + h.Name + ") to close"
                        + (p.HasExited ? " - closed." : " - still running."));
                }
                else Console.WriteLine("  PID " + h.Pid + " (" + h.Name + ") has no window to close.");
            }
            catch (Exception ex) { Console.WriteLine("  PID " + h.Pid + ": " + ex.Message); }
        }
    }

    private static void KillHolders(List<HandleCloser.Holder> holders)
    {
        foreach (var h in holders)
        {
            try
            {
                var p = System.Diagnostics.Process.GetProcessById(h.Pid);
                p.Kill();
                p.WaitForExit(4000);
                Console.WriteLine("  ended PID " + h.Pid + " (" + h.Name + ").");
            }
            catch (Exception ex) { Console.WriteLine("  PID " + h.Pid + ": " + ex.Message); }
        }
    }

    private static void ClearAttributes(string path, bool isDir)
    {
        try
        {
            if (isDir)
            {
                foreach (var f in Directory.GetFiles(path, "*", SearchOption.AllDirectories))
                    try { File.SetAttributes(f, FileAttributes.Normal); } catch { }
                File.SetAttributes(path, FileAttributes.Directory);
            }
            else File.SetAttributes(path, FileAttributes.Normal);
        }
        catch { }
    }

    private static void CollectFiles(string dir, List<string> into)
    {
        try
        {
            foreach (var f in Directory.GetFiles(dir)) into.Add(f);
            foreach (var d in Directory.GetDirectories(dir)) CollectFiles(d, into);
        }
        catch { }
    }

    private static bool TryDelete(string path, bool isDir)
    {
        try
        {
            if (isDir) Directory.Delete(path, true);
            else File.Delete(path);
            return !(File.Exists(path) || Directory.Exists(path));
        }
        catch { return false; }
    }

    // ---- SeDebugPrivilege ------------------------------------------------

    [StructLayout(LayoutKind.Sequential)]
    private struct LUID { public uint Low; public int High; }
    [StructLayout(LayoutKind.Sequential)]
    private struct TOKEN_PRIVILEGES { public uint Count; public LUID Luid; public uint Attributes; }

    private const uint TOKEN_ADJUST_PRIVILEGES = 0x20, TOKEN_QUERY = 0x8;
    private const uint SE_PRIVILEGE_ENABLED = 0x2;

    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool OpenProcessToken(IntPtr proc, uint access, out IntPtr token);
    [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern bool LookupPrivilegeValue(string sys, string name, out LUID luid);
    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool AdjustTokenPrivileges(IntPtr token, bool disableAll, ref TOKEN_PRIVILEGES newState, uint len, IntPtr prev, IntPtr retLen);
    [DllImport("kernel32.dll")]
    private static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr h);

    private static void TryEnablePrivilege(string name)
    {
        IntPtr token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, out token)) return;
        try
        {
            LUID luid;
            if (!LookupPrivilegeValue(null, name, out luid)) return;
            var tp = new TOKEN_PRIVILEGES { Count = 1, Luid = luid, Attributes = SE_PRIVILEGE_ENABLED };
            AdjustTokenPrivileges(token, false, ref tp, 0, IntPtr.Zero, IntPtr.Zero);
        }
        finally { CloseHandle(token); }
    }
}

// ------------------------------------------------------------------------
// HandleCloser: enumerate every open handle system-wide, find the ones that
// name our target file, and close them in their owning process.
// ------------------------------------------------------------------------
internal static class HandleCloser
{
    public struct Holder { public int Pid; public string Name; }

    // List the processes that hold any of the given files open, using the
    // Windows Restart Manager. This is the official, fast, hang-free way to ask
    // "who has this file locked?" - far better than scanning every system handle
    // (many of which hang GetFinalPathNameByHandle) just to identify holders.
    public static List<Holder> FindHolders(List<string> filePaths)
    {
        var result = new List<Holder>();
        var files = filePaths.ToArray();
        if (files.Length == 0) return result;

        uint session;
        var key = new StringBuilder(CCH_RM_SESSION_KEY + 1);
        if (RmStartSession(out session, 0, key) != 0) return result;
        try
        {
            if (RmRegisterResources(session, (uint)files.Length, files, 0, null, 0, null) != 0)
                return result;

            uint needed = 0, count = 0, reason;
            int rc = RmGetList(session, out needed, ref count, null, out reason);
            if (needed == 0) return result;

            var arr = new RM_PROCESS_INFO[needed];
            count = needed;
            rc = RmGetList(session, out needed, ref count, arr, out reason);
            if (rc != 0) return result;

            for (int i = 0; i < count; i++)
            {
                int pid = arr[i].Process.dwProcessId;
                string name = string.IsNullOrEmpty(arr[i].strAppName) ? ProcessName(pid) : arr[i].strAppName;
                result.Add(new Holder { Pid = pid, Name = name });
            }
        }
        finally { RmEndSession(session); }
        return result;
    }

    // Force-close the handles the given files have open, but ONLY inside the
    // given processes. Scoping the system-handle scan to the few known holders
    // keeps it fast and avoids the mass-hang of querying every handle on the box.
    // Dangerous: the owning app keeps running but loses its handle mid-flight.
    public static void CloseHandlesTo(List<string> filePaths, HashSet<int> onlyPids)
    {
        Scan(filePaths, onlyPids, (pid, ownerHandle, remoteHandle) =>
        {
            IntPtr sink;
            if (DuplicateHandle(ownerHandle, remoteHandle, GetCurrentProcess(), out sink,
                                0, false, DUPLICATE_CLOSE_SOURCE))
            {
                CloseHandle(sink);
                Console.WriteLine("  force-closed a handle in PID " + pid);
            }
        });
    }

    private struct Match { public int Pid; public IntPtr Owner; public IntPtr Handle; }

    // How long, in total, the tool will spend resolving handle names. A handful
    // of handles can hang GetFinalPathNameByHandle indefinitely; rather than
    // time out each one serially (tens of thousands of handles -> minutes), the
    // work is spread over many threads under one overall deadline.
    private const int ScanBudgetMs = 6000;
    private const int ScanThreads = 32;

    // Handle scan restricted to the given PIDs. For every handle in those
    // processes that names one of the wanted files, invokes
    // onMatch(pid, ownerProcessHandle, remoteHandleValue).
    private static void Scan(List<string> filePaths, HashSet<int> onlyPids,
                             Action<int, IntPtr, IntPtr> onMatch)
    {
        var wanted = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var fp in filePaths)
        {
            try { wanted.Add(Path.GetFullPath(fp).TrimEnd('\\')); } catch { }
        }
        if (wanted.Count == 0) return;

        int myPid = System.Diagnostics.Process.GetCurrentProcess().Id;
        var handles = QueryAllHandles();

        var procCache = new Dictionary<int, IntPtr>();   // pid -> process handle
        var typeCache = new Dictionary<ushort, bool>();  // type index -> is a File?
        var matches = new List<Match>();
        object gate = new object();
        int next = -1;

        System.Threading.ThreadStart body = () =>
        {
            while (true)
            {
                int i = System.Threading.Interlocked.Increment(ref next);
                if (i >= handles.Count) break;
                var h = handles[i];

                if (h.Pid == myPid || h.Pid <= 4) continue;
                if (onlyPids != null && !onlyPids.Contains(h.Pid)) continue;
                if (h.Access == 0x00100000) continue; // SYNCHRONIZE-only: never a real file

                IntPtr owner;
                lock (gate)
                {
                    if (!procCache.TryGetValue(h.Pid, out owner))
                    {
                        owner = OpenProcess(PROCESS_DUP_HANDLE, false, h.Pid);
                        procCache[h.Pid] = owner;
                    }
                }
                if (owner == IntPtr.Zero) continue;

                IntPtr dup;
                if (!DuplicateHandle(owner, h.Handle, GetCurrentProcess(), out dup,
                                     0, false, DUPLICATE_SAME_ACCESS))
                    continue;
                try
                {
                    bool isFile;
                    lock (gate)
                    {
                        if (!typeCache.TryGetValue(h.TypeIndex, out isFile))
                        {
                            isFile = GetTypeName(dup) == "File";
                            typeCache[h.TypeIndex] = isFile;
                        }
                    }
                    if (!isFile) continue;

                    string name = RawQueryName(dup);   // DOS path, e.g. "C:\dir\file"
                    if (name == null || !wanted.Contains(name.TrimEnd('\\'))) continue;

                    lock (gate) matches.Add(new Match { Pid = h.Pid, Owner = owner, Handle = h.Handle });
                }
                finally { CloseHandle(dup); }
            }
        };

        var workers = new System.Threading.Thread[ScanThreads];
        for (int i = 0; i < workers.Length; i++)
        {
            workers[i] = new System.Threading.Thread(body) { IsBackground = true };
            workers[i].Start();
        }
        var sw = System.Diagnostics.Stopwatch.StartNew();
        foreach (var w in workers)
        {
            int remaining = ScanBudgetMs - (int)sw.ElapsedMilliseconds;
            w.Join(remaining > 0 ? remaining : 0); // deadline reached: leave stragglers running
        }

        // Act on matches serially, after the scan, on this thread.
        List<Match> snapshot;
        lock (gate) snapshot = new List<Match>(matches);
        foreach (var m in snapshot) onMatch(m.Pid, m.Owner, m.Handle);

        // Owner handles may still be in use by any straggler threads, so only
        // free them if every worker finished within the budget.
        bool allDone = true;
        foreach (var w in workers) if (w.IsAlive) { allDone = false; break; }
        if (allDone)
            lock (gate) foreach (var kv in procCache) if (kv.Value != IntPtr.Zero) CloseHandle(kv.Value);
    }

    private static string ProcessName(int pid)
    {
        try { return System.Diagnostics.Process.GetProcessById(pid).ProcessName + ".exe"; }
        catch { return "(pid " + pid + ")"; }
    }

    // --- system handle enumeration ---

    private struct HInfo { public int Pid; public IntPtr Handle; public ushort TypeIndex; public uint Access; }

    // SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX - the extended form, because the legacy
    // one stores the PID in a USHORT and modern PIDs overflow that.
    [StructLayout(LayoutKind.Sequential)]
    private struct SYSTEM_HANDLE_EX
    {
        public IntPtr Object;
        public IntPtr UniqueProcessId;
        public IntPtr HandleValue;
        public uint GrantedAccess;
        public ushort CreatorBackTraceIndex;
        public ushort ObjectTypeIndex;
        public uint HandleAttributes;
        public uint Reserved;
    }

    private static List<HInfo> QueryAllHandles()
    {
        var list = new List<HInfo>();
        int len = 0x100000;
        IntPtr buf = Marshal.AllocHGlobal(len);
        try
        {
            int need;
            uint status;
            while ((status = NtQuerySystemInformation(SystemExtendedHandleInformation, buf, len, out need)) == STATUS_INFO_LEN_MISMATCH)
            {
                Marshal.FreeHGlobal(buf);
                len = Math.Max(need, len * 2);
                buf = Marshal.AllocHGlobal(len);
            }
            if (status != 0) return list;

            // SYSTEM_HANDLE_INFORMATION_EX: { ULONG_PTR NumberOfHandles; ULONG_PTR Reserved; entries[] }
            long count = IntPtr.Size == 8 ? Marshal.ReadInt64(buf) : Marshal.ReadInt32(buf);
            IntPtr entry = (IntPtr)(buf.ToInt64() + 2L * IntPtr.Size);
            int stride = Marshal.SizeOf(typeof(SYSTEM_HANDLE_EX));
            for (long i = 0; i < count; i++)
            {
                var sh = (SYSTEM_HANDLE_EX)Marshal.PtrToStructure(
                    (IntPtr)(entry.ToInt64() + i * stride), typeof(SYSTEM_HANDLE_EX));
                list.Add(new HInfo
                {
                    Pid = (int)sh.UniqueProcessId.ToInt64(),
                    Handle = sh.HandleValue,
                    TypeIndex = sh.ObjectTypeIndex,
                    Access = sh.GrantedAccess
                });
            }
        }
        finally { Marshal.FreeHGlobal(buf); }
        return list;
    }

    // Object type name for a duplicated handle, e.g. "File", "Key", "Mutant".
    private static string GetTypeName(IntPtr handle)
    {
        int len = 0x1000;
        IntPtr buf = Marshal.AllocHGlobal(len);
        try
        {
            int need;
            if (NtQueryObject(handle, ObjectTypeInformation, buf, len, out need) != 0) return null;
            // PUBLIC_OBJECT_TYPE_INFORMATION starts with a UNICODE_STRING TypeName.
            ushort byteLen = (ushort)Marshal.ReadInt16(buf);
            IntPtr strPtr = Marshal.ReadIntPtr(buf, IntPtr.Size);
            if (byteLen == 0 || strPtr == IntPtr.Zero) return null;
            return Marshal.PtrToStringUni(strPtr, byteLen / 2);
        }
        catch { return null; }
        finally { Marshal.FreeHGlobal(buf); }
    }

    // Resolve a File handle to its DOS path via GetFinalPathNameByHandle. Unlike
    // NtQueryObject, this returns a ready-to-compare "C:\dir\file" and does not
    // hang on ordinary disk files (it just fails fast on pipes/consoles/etc.).
    private static string RawQueryName(IntPtr handle)
    {
        var sb = new StringBuilder(600);
        uint n = GetFinalPathNameByHandle(handle, sb, (uint)sb.Capacity, 0); // VOLUME_NAME_DOS | FILE_NAME_NORMALIZED
        if (n == 0) return null;
        if (n > sb.Capacity)
        {
            sb = new StringBuilder((int)n + 1);
            n = GetFinalPathNameByHandle(handle, sb, (uint)sb.Capacity, 0);
            if (n == 0) return null;
        }
        string s = sb.ToString();
        if (s.StartsWith(@"\\?\UNC\")) return @"\\" + s.Substring(8); // network share
        if (s.StartsWith(@"\\?\")) return s.Substring(4);            // strip the \\?\ prefix
        return s;
    }

    // --- Restart Manager (holder identification) ---

    private const int CCH_RM_SESSION_KEY = 32;
    private const int CCH_RM_MAX_APP_NAME = 255;
    private const int CCH_RM_MAX_SVC_NAME = 63;

    [StructLayout(LayoutKind.Sequential)]
    private struct RM_UNIQUE_PROCESS { public int dwProcessId; public System.Runtime.InteropServices.ComTypes.FILETIME ProcessStartTime; }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct RM_PROCESS_INFO
    {
        public RM_UNIQUE_PROCESS Process;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = CCH_RM_MAX_APP_NAME + 1)] public string strAppName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = CCH_RM_MAX_SVC_NAME + 1)] public string strServiceShortName;
        public int ApplicationType;
        public uint AppStatus;
        public uint TSSessionId;
        [MarshalAs(UnmanagedType.Bool)] public bool bRestartable;
    }

    [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
    private static extern int RmStartSession(out uint pSessionHandle, int dwSessionFlags, StringBuilder strSessionKey);
    [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
    private static extern int RmRegisterResources(uint pSessionHandle, uint nFiles, string[] rgsFilenames,
        uint nApplications, RM_UNIQUE_PROCESS[] rgApplications, uint nServices, string[] rgsServiceNames);
    [DllImport("rstrtmgr.dll")]
    private static extern int RmGetList(uint dwSessionHandle, out uint pnProcInfoNeeded,
        ref uint pnProcInfo, [In, Out] RM_PROCESS_INFO[] rgAffectedApps, out uint lpdwRebootReasons);
    [DllImport("rstrtmgr.dll")]
    private static extern int RmEndSession(uint pSessionHandle);

    // --- P/Invoke ---

    private const int SystemExtendedHandleInformation = 64;
    private const int ObjectTypeInformation = 2;
    private const uint STATUS_INFO_LEN_MISMATCH = 0xC0000004;
    private const uint PROCESS_DUP_HANDLE = 0x0040;
    private const uint DUPLICATE_CLOSE_SOURCE = 0x1;
    private const uint DUPLICATE_SAME_ACCESS = 0x2;

    [DllImport("ntdll.dll")]
    private static extern uint NtQuerySystemInformation(int cls, IntPtr info, int len, out int need);
    [DllImport("ntdll.dll")]
    private static extern uint NtQueryObject(IntPtr h, int cls, IntPtr info, int len, out int need);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool DuplicateHandle(IntPtr srcProc, IntPtr src, IntPtr dstProc, out IntPtr dst, uint access, bool inherit, uint options);
    [DllImport("kernel32.dll")]
    private static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern uint GetFinalPathNameByHandle(IntPtr h, StringBuilder path, uint count, uint flags);
}
