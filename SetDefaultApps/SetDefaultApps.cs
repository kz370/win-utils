using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;

class SetDefaultApps
{
    static void Extract(string name, string dir)
    {
        using (Stream s = Assembly.GetExecutingAssembly().GetManifestResourceStream(name))
        using (MemoryStream ms = new MemoryStream())
        {
            s.CopyTo(ms);
            byte[] data = ms.ToArray();
            bool hasBom = data.Length >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF;
            using (FileStream f = File.Create(Path.Combine(dir, name)))
            {
                if (!hasBom) f.Write(new byte[] { 0xEF, 0xBB, 0xBF }, 0, 3); // PowerShell 5.1 needs a BOM for UTF-8
                f.Write(data, 0, data.Length);
            }
        }
    }

    static int Main()
    {
        Console.Title = "Set Default Apps";
        string dir = Path.Combine(Path.GetTempPath(), "dfa_" + Guid.NewGuid().ToString("N"));
        int code = 0;
        try
        {
            Directory.CreateDirectory(dir);
            Extract("SFTA.ps1", dir);
            Extract("run.ps1", dir);
            Extract("apps.ini", dir);

            // An apps.ini placed next to the exe overrides the built-in one (no rebuild needed).
            string external = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "apps.ini");
            if (File.Exists(external))
            {
                File.Copy(external, Path.Combine(dir, "apps.ini"), true);
                Console.WriteLine("Using apps.ini from: " + external);
            }

            ProcessStartInfo psi = new ProcessStartInfo("powershell.exe",
                "-NoProfile -ExecutionPolicy Bypass -File \"" + Path.Combine(dir, "run.ps1") + "\"");
            psi.UseShellExecute = false;
            using (Process p = Process.Start(psi))
            {
                p.WaitForExit();
                code = p.ExitCode;
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine("Error: " + ex.Message);
            code = 1;
        }
        finally
        {
            try { Directory.Delete(dir, true); } catch { }
        }

        Console.WriteLine();
        Console.WriteLine("Press any key to close...");
        try { Console.ReadKey(true); } catch { }
        return code;
    }
}
