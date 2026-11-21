// APK Thumbnail Provider for Windows Explorer.
//
// Shows the real app icon of .apk / .xapk / .apks / .apkm files in Explorer.
// Implements thumbnail and file-icon shell interfaces; thumbnails run
// out-of-process (dllhost.exe) - it never loads into explorer.exe itself.
//
// Icon lookup:
//   1. bundles (.xapk/.apks/.apkm): icon.png at the archive root, else base.apk
//   2. AndroidManifest.xml -> <application android:icon> resource id
//   3. resources.arsc -> all files for that id, highest density raster wins
//   4. adaptive icons / vector drawables are rendered with WPF when no good
//      raster exists (background + foreground, gradients, groups, clip-paths)
//   5. last resort: res/*/ic_launcher.png by name
//
// Compiled by install.ps1 with the csc.exe that ships with Windows (C# 5).

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;
using System.Threading;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

[assembly: ComVisible(false)]
[assembly: AssemblyTitle("APK Thumbnail Provider")]
[assembly: AssemblyVersion("1.0.0.0")]

namespace ApkThumbnail
{
    [ComVisible(true), ComImport, Guid("e357fccd-a995-4576-b01f-234630154e96"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IThumbnailProvider
    {
        void GetThumbnail(uint cx, out IntPtr phbmp, out int pdwAlpha);
    }

    [ComVisible(true), ComImport, Guid("b824b49d-22ac-4161-ac8a-9916e8fa3f7f"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IInitializeWithStream
    {
        void Initialize([MarshalAs(UnmanagedType.Interface)] IStream pstream, uint grfMode);
    }

    // PreserveSig: these return the HRESULT themselves. Without it the int becomes a hidden
    // [out, retval] argument the shell never passes, and writing it corrupts the caller's stack.
    // szIconFile is a raw caller buffer: a StringBuilder can't be marshalled back into it reliably
    // when native code calls managed code (the marshaller doesn't know the buffer's size).
    [ComVisible(true), ComImport, Guid("000214FA-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IExtractIconW
    {
        [PreserveSig] int GetIconLocation(uint uFlags, IntPtr szIconFile, int cchMax, out int piIndex, out uint pwFlags);
        [PreserveSig] int Extract([MarshalAs(UnmanagedType.LPWStr)] string pszFile, uint nIconIndex, out IntPtr phiconLarge, out IntPtr phiconSmall, uint nIconSize);
    }

    [ComVisible(true), ComImport, Guid("000214EB-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IExtractIconA
    {
        [PreserveSig] int GetIconLocation(uint uFlags, IntPtr szIconFile, int cchMax, out int piIndex, out uint pwFlags);
        [PreserveSig] int Extract([MarshalAs(UnmanagedType.LPStr)] string pszFile, uint nIconIndex, out IntPtr phiconLarge, out IntPtr phiconSmall, uint nIconSize);
    }

    // Thumbnails (IInitializeWithStream + IThumbnailProvider) cover the big views; Details, List
    // and Small icons ask the IconHandler (IPersistFile + IExtractIcon) for 16/32 px icons instead.
    //
    // They are two separate classes on purpose. The shell's stream for a file is a memory-mapped view
    // of the whole file. If the icon handler also accepted a stream, Explorer would hand it one when it
    // only wants an icon, and the stream (and the mapping) would live until the next .NET garbage
    // collection inside explorer.exe - which can be hours - so the file shows as "open in Windows
    // Explorer" and can't be deleted.
    [ComVisible(true), Guid("5E2A7C4B-9D31-4F6A-B8E2-3C7D1A0F9B64"), ClassInterface(ClassInterfaceType.None), ProgId("ApkThumbnail.Provider")]
    public sealed class ApkThumbnailProvider : IThumbnailProvider, IInitializeWithStream
    {
        const int WTSAT_ARGB = 2;
        const int E_FAIL = unchecked((int)0x80004005);
        IStream stream;

        public void Initialize(IStream pstream, uint grfMode)
        {
            Release(ref stream);
            stream = pstream;
        }

        public void GetThumbnail(uint cx, out IntPtr phbmp, out int pdwAlpha)
        {
            phbmp = IntPtr.Zero;
            pdwAlpha = WTSAT_ARGB;
            IStream s = stream;
            stream = null;
            if (s == null) throw new COMException("Not initialized", E_FAIL);

            byte[] pixels;
            int size = (int)Math.Min(Math.Max(cx, 16u), 1024u);
            try
            {
                pixels = IconRenderer.RenderOnSta(delegate { return new ComStream(s); }, size);
            }
            finally
            {
                Release(ref s);
            }

            if (pixels == null) throw new COMException("No icon found", E_FAIL);
            phbmp = Native.CreateBitmap(pixels, size, size);
            if (phbmp == IntPtr.Zero) throw new COMException("CreateDIBSection failed", E_FAIL);
        }

        // FinalRelease: drop every reference the RCW holds now, not at the next garbage collection.
        static void Release(ref IStream s)
        {
            if (s == null) return;
            try { Marshal.FinalReleaseComObject(s); } catch { }
            s = null;
        }
    }

    [ComVisible(true), Guid("F0AEA457-4A09-494F-8240-E59F6F225285"), ClassInterface(ClassInterfaceType.None), ProgId("ApkThumbnail.IconHandler")]
    public sealed class ApkIconHandler : IPersistFile, IExtractIconW, IExtractIconA
    {
        const int S_OK = 0, S_FALSE = 1;
        const int E_FAIL = unchecked((int)0x80004005);
        const uint GIL_PERINSTANCE = 0x00000002, GIL_NOTFILENAME = 0x00000008;
        string path;

        // --- IPersistFile: the shell hands the icon handler its file here

        public void GetClassID(out Guid pClassID) { pClassID = GetType().GUID; }
        public int IsDirty() { return S_FALSE; }
        public void Load(string pszFileName, int dwMode) { path = pszFileName; }
        public void Save(string pszFileName, bool fRemember) { throw new NotImplementedException(); }
        public void SaveCompleted(string pszFileName) { }
        public void GetCurFile(out string ppszFileName) { ppszFileName = path; }

        // --- IExtractIcon

        // The file path is the icon's cache key, so each package gets its own icon.
        public int GetIconLocation(uint uFlags, IntPtr szIconFile, int cchMax, out int piIndex, out uint pwFlags)
        {
            return IconLocation(szIconFile, cchMax, Encoding.Unicode, out piIndex, out pwFlags);
        }

        int IExtractIconA.GetIconLocation(uint uFlags, IntPtr szIconFile, int cchMax, out int piIndex, out uint pwFlags)
        {
            return IconLocation(szIconFile, cchMax, Encoding.Default, out piIndex, out pwFlags);
        }

        int IconLocation(IntPtr buffer, int cchMax, Encoding enc, out int piIndex, out uint pwFlags)
        {
            piIndex = 0;
            pwFlags = GIL_NOTFILENAME | GIL_PERINSTANCE;
            if (string.IsNullOrEmpty(path) || buffer == IntPtr.Zero || cchMax <= 0) return E_FAIL;
            byte[] bytes = enc.GetBytes(path + "\0");
            if (bytes.Length > cchMax * (enc == Encoding.Unicode ? 2 : 1)) return E_FAIL;
            Marshal.Copy(bytes, 0, buffer, bytes.Length);
            return S_OK;
        }

        public int Extract(string pszFile, uint nIconIndex, out IntPtr phiconLarge, out IntPtr phiconSmall, uint nIconSize)
        {
            phiconLarge = IntPtr.Zero;
            phiconSmall = IntPtr.Zero;
            string file = string.IsNullOrEmpty(path) ? pszFile : path;
            if (string.IsNullOrEmpty(file)) return E_FAIL;
            int smallSize = (int)(nIconSize >> 16);
            int largeSize = (int)(nIconSize & 0xffff);
            if (smallSize <= 0) smallSize = 16;
            if (largeSize <= 0) largeSize = 32;

            phiconLarge = Native.CreateIcon(file, largeSize);
            phiconSmall = Native.CreateIcon(file, smallSize);
            if (phiconLarge == IntPtr.Zero || phiconSmall == IntPtr.Zero)
            {
                if (phiconLarge != IntPtr.Zero) Native.DestroyIcon(phiconLarge);
                if (phiconSmall != IntPtr.Zero) Native.DestroyIcon(phiconSmall);
                phiconLarge = IntPtr.Zero;
                phiconSmall = IntPtr.Zero;
                return E_FAIL;
            }
            return 0;
        }
    }

    // ------------------------------------------------------------------ rendering

    sealed class IconRenderer : IDisposable
    {
        const long MaxFile = 64L << 20;
        const uint AttrIcon = 0x01010002, AttrRoundIcon = 0x0101052c, AttrDrawable = 0x01010199,
            AttrSrc = 0x01010119, AttrColor = 0x010101a5;

        readonly List<ZipIndex> zips = new List<ZipIndex>();
        readonly List<ArscTable> tables = new List<ArscTable>();
        readonly List<IDisposable> owned = new List<IDisposable>();
        ZipIndex baseZip;

        public static byte[] Render(Stream input, int size)
        {
            using (var buffered = new BufferedStream(input, 1 << 16))
            using (var outer = new ZipIndex(buffered))
            using (var r = new IconRenderer())
            {
                var group = new DrawingGroup();
                bool ok;
                using (var dc = group.Open())
                    ok = r.DrawPackage(dc, outer, new Rect(0, 0, size, size));
                return ok ? Rasterize(group, size) : null;
            }
        }

        // WPF wants an STA thread; the thumbnail surrogate and Explorer's icon threads are often MTA.
        public static byte[] RenderOnSta(Func<Stream> open, int size)
        {
            byte[] pixels = null;
            ThreadStart work = delegate
            {
                try
                {
                    using (var s = open())
                        pixels = Render(s, size);
                }
                catch
                {
                    pixels = null;
                }
            };

            if (Thread.CurrentThread.GetApartmentState() == ApartmentState.STA)
            {
                work();
            }
            else
            {
                var t = new Thread(delegate ()
                {
                    work();
                    Dispatcher.CurrentDispatcher.InvokeShutdown();
                }, 16 * 1024 * 1024);
                t.SetApartmentState(ApartmentState.STA);
                t.IsBackground = true;
                t.Start();
                t.Join();
            }
            return pixels;
        }

        public void Dispose()
        {
            foreach (var d in owned) d.Dispose();
            owned.Clear();
        }

        bool DrawPackage(DrawingContext dc, ZipIndex outer, Rect box)
        {
            if (outer.Find("AndroidManifest.xml") != null)
            {
                AddApk(outer);
            }
            else
            {
                var icon = LoadBitmap(outer.Read("icon.png", MaxFile));
                if (icon != null)
                {
                    DrawBitmap(dc, icon, box);
                    return true;
                }
                if (!LoadBundle(outer)) return false;
            }
            return DrawAppIcon(dc, box);
        }

        void AddApk(ZipIndex z)
        {
            zips.Add(z);
            if (baseZip == null) baseZip = z;
            try
            {
                var arsc = z.Read("resources.arsc", 256L << 20);
                if (arsc != null) tables.Add(new ArscTable(arsc));
            }
            catch { }
        }

        bool LoadBundle(ZipIndex outer)
        {
            var apks = new List<ZipArchiveEntry>();
            foreach (var e in outer.Entries)
                if (e.FullName.EndsWith(".apk", StringComparison.OrdinalIgnoreCase)) apks.Add(e);
            if (apks.Count == 0) return false;

            ZipArchiveEntry baseEntry = null;
            foreach (var e in apks)
                if (FileName(e) == "base.apk") { baseEntry = e; break; }
            if (baseEntry == null)
                foreach (var e in apks)
                {
                    string n = FileName(e);
                    if (n.StartsWith("config.") || n.StartsWith("split_")) continue;
                    if (baseEntry == null || e.Length > baseEntry.Length) baseEntry = e;
                }
            if (baseEntry == null)
                foreach (var e in apks)
                    if (baseEntry == null || e.Length > baseEntry.Length) baseEntry = e;

            var bz = OpenNested(baseEntry, 1L << 30);
            if (bz == null) return false;
            AddApk(bz);

            // density splits carry the mipmap PNGs of split bundles
            foreach (var e in apks)
            {
                if (e == baseEntry || !FileName(e).Contains("dpi")) continue;
                var z = OpenNested(e, MaxFile);
                if (z != null) AddApk(z);
            }
            return true;
        }

        static string FileName(ZipArchiveEntry e)
        {
            return Path.GetFileName(e.FullName).ToLowerInvariant();
        }

        ZipIndex OpenNested(ZipArchiveEntry e, long max)
        {
            try
            {
                var bytes = ZipIndex.ReadEntry(e, max);
                if (bytes == null) return null;
                var z = new ZipIndex(new MemoryStream(bytes, false));
                owned.Add(z);
                return z;
            }
            catch { return null; }
        }

        bool DrawAppIcon(DrawingContext dc, Rect box)
        {
            var manifest = Axml.Parse(baseZip.Read("AndroidManifest.xml", MaxFile));
            foreach (var el in manifest)
            {
                if (el.Name != "application") continue;
                if (DrawAttr(dc, box, el.Get("icon", AttrIcon), 0)) return true;
                if (DrawAttr(dc, box, el.Get("roundIcon", AttrRoundIcon), 0)) return true;
                break;
            }
            foreach (var el in manifest)
            {
                if (el.Name != "activity" && el.Name != "activity-alias") continue;
                if (DrawAttr(dc, box, el.Get("icon", AttrIcon), 0)) return true;
            }
            return DrawGuess(dc, box);
        }

        // --- resources

        List<ResValue> Values(uint id, int depth)
        {
            var res = new List<ResValue>();
            if (id == 0 || (id >> 24) == 0x01 || depth > 8) return res;
            var raw = new List<ResValue>();
            foreach (var t in tables) t.Lookup(id, raw);
            foreach (var v in raw)
            {
                if (v.Type == 0x01 || v.Type == 0x07) res.AddRange(Values(v.Data, depth + 1));
                else res.Add(v);
            }
            return res;
        }

        byte[] ReadFile(string path)
        {
            foreach (var z in zips)
            {
                var b = z.Read(path, MaxFile);
                if (b != null) return b;
            }
            return null;
        }

        static bool IsRaster(string p)
        {
            p = p.ToLowerInvariant();
            return p.EndsWith(".png") || p.EndsWith(".webp") || p.EndsWith(".jpg") || p.EndsWith(".jpeg") || p.EndsWith(".bmp") || p.EndsWith(".gif");
        }

        static int DensityRank(int d)
        {
            if (d == 0) return 160;
            if (d == 0xFFFF) return 400;
            if (d == 0xFFFE) return 1;
            return d;
        }

        // --- drawables

        bool DrawAttr(DrawingContext dc, Rect box, AxAttr a, int depth)
        {
            if (a == null || depth > 12) return false;
            if (a.Type == 0x01 || a.Type == 0x07) return DrawRef(dc, box, a.Data, depth + 1);
            Color c;
            if (TryColor(a.Type, a.Data, a.Str, out c))
            {
                dc.DrawRectangle(new SolidColorBrush(c), null, box);
                return true;
            }
            if (a.Type == 0x03 && a.Str != null && a.Str.StartsWith("res/")) return DrawFile(dc, box, a.Str, depth + 1);
            return false;
        }

        bool DrawRef(DrawingContext dc, Rect box, uint id, int depth)
        {
            var vals = Values(id, 0);
            var rasters = new List<ResValue>();
            var xmls = new List<ResValue>();
            ResValue color = null;
            foreach (var v in vals)
            {
                if (v.Type == 0x03 && v.Str != null)
                {
                    if (IsRaster(v.Str)) rasters.Add(v);
                    else if (v.Str.EndsWith(".xml", StringComparison.OrdinalIgnoreCase)) xmls.Add(v);
                }
                else if (color == null && v.Type >= 0x1c && v.Type <= 0x1f) color = v;
            }
            rasters.Sort(delegate (ResValue x, ResValue y) { return DensityRank(y.Density).CompareTo(DensityRank(x.Density)); });
            xmls.Sort(delegate (ResValue x, ResValue y) { return y.Sdk.CompareTo(x.Sdk); });

            BitmapSource bmp = null;
            foreach (var r in rasters)
            {
                bmp = LoadBitmap(ReadFile(r.Str));
                if (bmp != null) break;
            }
            // a big raster beats rendering; a tiny one loses to an adaptive/vector icon
            if (bmp != null && (bmp.PixelWidth >= 96 || xmls.Count == 0))
            {
                DrawBitmap(dc, bmp, box);
                return true;
            }
            foreach (var x in xmls)
                if (TryDrawXml(dc, box, x.Str, depth + 1)) return true;
            if (bmp != null)
            {
                DrawBitmap(dc, bmp, box);
                return true;
            }
            if (color != null)
            {
                dc.DrawRectangle(new SolidColorBrush(ToColor(color.Data)), null, box);
                return true;
            }
            return false;
        }

        bool DrawFile(DrawingContext dc, Rect box, string path, int depth)
        {
            if (IsRaster(path))
            {
                var bmp = LoadBitmap(ReadFile(path));
                if (bmp == null) return false;
                DrawBitmap(dc, bmp, box);
                return true;
            }
            return TryDrawXml(dc, box, path, depth);
        }

        // Renders into a scratch group first so a half-failed drawable leaves no trace.
        bool TryDrawXml(DrawingContext dc, Rect box, string path, int depth)
        {
            if (depth > 12) return false;
            var els = Axml.Parse(ReadFile(path));
            if (els.Count == 0) return false;
            var g = new DrawingGroup();
            bool ok;
            try
            {
                using (var sub = g.Open())
                    ok = DrawXml(sub, box, els, 0, depth);
            }
            catch { ok = false; }
            if (ok) dc.DrawDrawing(g);
            return ok;
        }

        static int End(List<AxElement> els, int idx)
        {
            int j = idx + 1;
            while (j < els.Count && els[j].Depth > els[idx].Depth) j++;
            return j;
        }

        IEnumerable<int> Children(List<AxElement> els, int idx)
        {
            int end = End(els, idx), d = els[idx].Depth + 1;
            for (int j = idx + 1; j < end; j++)
                if (els[j].Depth == d) yield return j;
        }

        bool DrawXml(DrawingContext dc, Rect box, List<AxElement> els, int idx, int depth)
        {
            if (depth > 12) return false;
            var root = els[idx];
            switch (root.Name)
            {
                case "adaptive-icon":
                    return DrawAdaptive(dc, box, els, idx, depth);
                case "vector":
                    return DrawVector(dc, box, els, idx, depth);
                case "bitmap":
                case "nine-patch":
                    return DrawAttr(dc, box, root.Get("src", AttrSrc), depth);
                case "inset":
                    return DrawChild(dc, Inset(root, box), els, idx, depth);
                case "animated-vector":
                case "rotate":
                case "scale":
                case "clip":
                    return DrawChild(dc, box, els, idx, depth);
                case "layer-list":
                    {
                        bool any = false;
                        foreach (int j in Children(els, idx))
                            if (els[j].Name == "item" && DrawChild(dc, box, els, j, depth)) any = true;
                        return any;
                    }
                case "selector":
                case "level-list":
                case "animated-selector":
                case "transition":
                    foreach (int j in Children(els, idx))
                        if (els[j].Name == "item" && DrawChild(dc, box, els, j, depth)) return true;
                    return false;
                case "shape":
                    return DrawShape(dc, box, els, idx, depth);
                case "color":
                    {
                        var b = GetBrush(root.Get("color", AttrColor), 1, depth);
                        if (b == null) return false;
                        dc.DrawRectangle(b, null, box);
                        return true;
                    }
                default:
                    return false;
            }
        }

        // android:drawable="@..." or an inline child drawable
        bool DrawChild(DrawingContext dc, Rect box, List<AxElement> els, int idx, int depth)
        {
            var a = els[idx].Get("drawable", AttrDrawable);
            if (a != null) return DrawAttr(dc, box, a, depth + 1);
            foreach (int j in Children(els, idx))
                return DrawXml(dc, box, els, j, depth + 1);
            return false;
        }

        Rect Inset(AxElement el, Rect box)
        {
            double all = InsetValue(el.Get("inset", 0), 0, box.Width);
            double l = InsetValue(el.Get("insetLeft", 0), all, box.Width);
            double t = InsetValue(el.Get("insetTop", 0), all, box.Height);
            double r = InsetValue(el.Get("insetRight", 0), all, box.Width);
            double b = InsetValue(el.Get("insetBottom", 0), all, box.Height);
            double w = box.Width - l - r, h = box.Height - t - b;
            if (w <= 1 || h <= 1) return box;
            return new Rect(box.X + l, box.Y + t, w, h);
        }

        double InsetValue(AxAttr a, double def, double extent)
        {
            if (a == null) return def;
            if (a.Type == 0x06) return Complex(a.Data) * extent;
            // dimensions are assumed relative to a 108dp adaptive-icon layer
            if (a.Type == 0x05) return Complex(a.Data) / 108.0 * extent;
            return def;
        }

        bool DrawAdaptive(DrawingContext dc, Rect box, List<AxElement> els, int idx, int depth)
        {
            int bg = -1, fg = -1;
            foreach (int j in Children(els, idx))
            {
                if (els[j].Name == "background") bg = j;
                else if (els[j].Name == "foreground") fg = j;
            }
            if (fg < 0) return false;

            // 108dp layers, the centre 72dp is what launchers show
            double pad = box.Width / 16;
            var vis = new Rect(box.X + pad, box.Y + pad, box.Width - 2 * pad, box.Height - 2 * pad);
            var layer = new Rect(vis.X - vis.Width / 4, vis.Y - vis.Height / 4, vis.Width * 1.5, vis.Height * 1.5);
            double radius = vis.Width * 0.24;

            dc.PushClip(new RectangleGeometry(vis, radius, radius));
            if (bg >= 0) DrawChild(dc, layer, els, bg, depth);
            bool ok = DrawChild(dc, layer, els, fg, depth);
            dc.Pop();
            return ok;
        }

        bool DrawShape(DrawingContext dc, Rect box, List<AxElement> els, int idx, int depth)
        {
            Brush brush = null;
            foreach (int j in Children(els, idx))
            {
                var c = els[j];
                if (c.Name == "solid") brush = GetBrush(c.Get("color", AttrColor), 1, depth);
                else if (c.Name == "gradient")
                {
                    var stops = GradientStops(els, j, depth);
                    if (stops.Count > 0) brush = new SolidColorBrush(stops[0].Color);
                }
                if (brush != null) break;
            }
            if (brush == null) return false;
            if (GetInt(els[idx].Get("shape", 0), 0) == 1)
                dc.DrawEllipse(brush, null, new Point(box.X + box.Width / 2, box.Y + box.Height / 2), box.Width / 2, box.Height / 2);
            else
                dc.DrawRectangle(brush, null, box);
            return true;
        }

        // --- vector drawables

        bool DrawVector(DrawingContext dc, Rect box, List<AxElement> els, int idx, int depth)
        {
            var root = els[idx];
            double vw = GetFloat(root.Get("viewportWidth", 0x01010402), 0);
            double vh = GetFloat(root.Get("viewportHeight", 0x01010403), 0);
            if (vw <= 0 || vh <= 0) return false;

            int pushes = 0;
            dc.PushTransform(new MatrixTransform(box.Width / vw, 0, 0, box.Height / vh, box.X, box.Y)); pushes++;
            dc.PushClip(new RectangleGeometry(new Rect(0, 0, vw, vh))); pushes++;
            double alpha = GetFloat(root.Get("alpha", 0x0101031f), 1);
            if (alpha < 1) { dc.PushOpacity(alpha); pushes++; }

            // (ownerDepth, pushCount): popped once an element at depth <= ownerDepth shows up
            var stack = new List<KeyValuePair<int, int>>();
            bool drew = false;
            int end = End(els, idx);
            for (int i = idx + 1; i < end; i++)
            {
                var e = els[i];
                while (stack.Count > 0 && e.Depth <= stack[stack.Count - 1].Key)
                {
                    for (int k = 0; k < stack[stack.Count - 1].Value; k++) dc.Pop();
                    stack.RemoveAt(stack.Count - 1);
                }
                if (e.Name == "group")
                {
                    double px = GetFloat(e.Get("pivotX", 0x010101b5), 0), py = GetFloat(e.Get("pivotY", 0x010101b6), 0);
                    var m = Matrix.Identity;
                    m.Translate(-px, -py);
                    m.Scale(GetFloat(e.Get("scaleX", 0x01010324), 1), GetFloat(e.Get("scaleY", 0x01010325), 1));
                    m.Rotate(GetFloat(e.Get("rotation", 0x01010326), 0));
                    m.Translate(px + GetFloat(e.Get("translateX", 0x01010320), 0), py + GetFloat(e.Get("translateY", 0x01010321), 0));
                    dc.PushTransform(new MatrixTransform(m));
                    stack.Add(new KeyValuePair<int, int>(e.Depth, 1));
                }
                else if (e.Name == "clip-path")
                {
                    var geo = SvgPath.Parse(GetString(e.Get("pathData", 0x01010405)), FillRule.Nonzero);
                    if (geo != null)
                    {
                        dc.PushClip(geo);
                        stack.Add(new KeyValuePair<int, int>(e.Depth - 1, 1));
                    }
                }
                else if (e.Name == "path")
                {
                    if (DrawPath(dc, e, depth)) drew = true;
                }
            }
            foreach (var f in stack)
                for (int k = 0; k < f.Value; k++) dc.Pop();
            for (int k = 0; k < pushes; k++) dc.Pop();
            return drew;
        }

        bool DrawPath(DrawingContext dc, AxElement e, int depth)
        {
            var rule = GetInt(e.Get("fillType", 0x0101051e), 0) == 1 ? FillRule.EvenOdd : FillRule.Nonzero;
            var geo = SvgPath.Parse(GetString(e.Get("pathData", 0x01010405)), rule);
            if (geo == null) return false;
            var fill = GetBrush(e.Get("fillColor", 0x01010404), GetFloat(e.Get("fillAlpha", 0x010104cc), 1), depth);
            Pen pen = null;
            double sw = GetFloat(e.Get("strokeWidth", 0x01010407), 0);
            if (sw > 0)
            {
                var sb = GetBrush(e.Get("strokeColor", 0x01010406), GetFloat(e.Get("strokeAlpha", 0x010104cb), 1), depth);
                if (sb != null)
                {
                    pen = new Pen(sb, sw);
                    int cap = GetInt(e.Get("strokeLineCap", 0), 0), join = GetInt(e.Get("strokeLineJoin", 0), 0);
                    pen.StartLineCap = pen.EndLineCap = cap == 1 ? PenLineCap.Round : cap == 2 ? PenLineCap.Square : PenLineCap.Flat;
                    pen.LineJoin = join == 1 ? PenLineJoin.Round : join == 2 ? PenLineJoin.Bevel : PenLineJoin.Miter;
                    pen.MiterLimit = GetFloat(e.Get("strokeMiterLimit", 0), 4);
                }
            }
            if (fill == null && pen == null) return false;
            dc.DrawGeometry(fill, pen, geo);
            return true;
        }

        // --- colours, brushes, numbers

        Brush GetBrush(AxAttr a, double alpha, int depth)
        {
            if (a == null || depth > 12) return null;
            Color c;
            if (TryColor(a.Type, a.Data, a.Str, out c)) return Solid(c, alpha);
            if (a.Type != 0x01 && a.Type != 0x07) return null;
            var vals = Values(a.Data, 0);
            foreach (var v in vals)
                if (TryColor(v.Type, v.Data, v.Str, out c)) return Solid(c, alpha);
            foreach (var v in vals)
            {
                if (v.Type != 0x03 || v.Str == null || !v.Str.EndsWith(".xml", StringComparison.OrdinalIgnoreCase)) continue;
                var b = BrushFromXml(v.Str, alpha, depth + 1);
                if (b != null) return b;
            }
            return null;
        }

        Brush BrushFromXml(string path, double alpha, int depth)
        {
            var els = Axml.Parse(ReadFile(path));
            if (els.Count == 0) return null;
            var root = els[0];
            if (root.Name == "selector")
            {
                // the stateless default is usually the last item
                Brush last = null;
                foreach (int j in Children(els, 0))
                {
                    var b = GetBrush(els[j].Get("color", AttrColor), alpha * GetFloat(els[j].Get("alpha", 0x0101031f), 1), depth);
                    if (b != null) last = b;
                }
                return last;
            }
            if (root.Name != "gradient") return null;

            var stops = GradientStops(els, 0, depth);
            if (stops.Count == 0) return null;
            Brush brush;
            int type = GetInt(root.Get("type", 0), 0);
            if (type == 0)
            {
                var lg = new LinearGradientBrush(stops);
                lg.MappingMode = BrushMappingMode.Absolute;
                lg.StartPoint = new Point(GetFloat(root.Get("startX", 0), 0), GetFloat(root.Get("startY", 0), 0));
                lg.EndPoint = new Point(GetFloat(root.Get("endX", 0), 0), GetFloat(root.Get("endY", 0), 0));
                brush = lg;
            }
            else if (type == 1)
            {
                var rg = new RadialGradientBrush(stops);
                rg.MappingMode = BrushMappingMode.Absolute;
                var center = new Point(GetFloat(root.Get("centerX", 0), 0), GetFloat(root.Get("centerY", 0), 0));
                double r = GetFloat(root.Get("gradientRadius", 0), 0);
                rg.Center = center;
                rg.GradientOrigin = center;
                rg.RadiusX = r;
                rg.RadiusY = r;
                brush = rg;
            }
            else
            {
                brush = new SolidColorBrush(stops[0].Color);
            }
            brush.Opacity = alpha;
            return brush;
        }

        GradientStopCollection GradientStops(List<AxElement> els, int idx, int depth)
        {
            var stops = new GradientStopCollection();
            Color c;
            foreach (int j in Children(els, idx))
            {
                var it = els[j];
                if (it.Name == "item" && GetColor(it.Get("color", AttrColor), out c))
                    stops.Add(new GradientStop(c, GetFloat(it.Get("offset", 0), 0)));
            }
            if (stops.Count > 0) return stops;
            var root = els[idx];
            if (GetColor(root.Get("startColor", 0), out c)) stops.Add(new GradientStop(c, 0));
            if (GetColor(root.Get("centerColor", 0), out c)) stops.Add(new GradientStop(c, 0.5));
            if (GetColor(root.Get("endColor", 0), out c)) stops.Add(new GradientStop(c, 1));
            return stops;
        }

        bool GetColor(AxAttr a, out Color c)
        {
            c = Colors.Transparent;
            if (a == null) return false;
            if (TryColor(a.Type, a.Data, a.Str, out c)) return true;
            if (a.Type == 0x01 || a.Type == 0x07)
                foreach (var v in Values(a.Data, 0))
                    if (TryColor(v.Type, v.Data, v.Str, out c)) return true;
            return false;
        }

        static Brush Solid(Color c, double alpha)
        {
            if (alpha < 1) c.A = (byte)Math.Round(c.A * Math.Max(0, alpha));
            return new SolidColorBrush(c);
        }

        static bool TryColor(int type, uint data, string str, out Color c)
        {
            c = Colors.Transparent;
            if (type >= 0x1c && type <= 0x1f)
            {
                c = ToColor(data);
                return true;
            }
            if (type == 0x03 && str != null && str.StartsWith("#"))
            {
                string h = str.Substring(1);
                uint v;
                if (!uint.TryParse(h, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out v)) return false;
                if (h.Length == 6) v |= 0xFF000000;
                else if (h.Length == 3) v = 0xFF000000 | ((v & 0xF00) * 0x1100) | ((v & 0xF0) * 0x110) | ((v & 0xF) * 0x11);
                else if (h.Length != 8) return false;
                c = ToColor(v);
                return true;
            }
            return false;
        }

        static Color ToColor(uint v)
        {
            return Color.FromArgb((byte)(v >> 24), (byte)(v >> 16), (byte)(v >> 8), (byte)v);
        }

        string GetString(AxAttr a)
        {
            if (a == null) return null;
            if (a.Type == 0x01 || a.Type == 0x07)
            {
                foreach (var v in Values(a.Data, 0))
                    if (v.Type == 0x03) return v.Str;
                return null;
            }
            return a.Str;
        }

        double GetFloat(AxAttr a, double def)
        {
            if (a == null) return def;
            double v;
            if (Num(a.Type, a.Data, a.Str, out v)) return v;
            if (a.Type == 0x01 || a.Type == 0x07)
                foreach (var r in Values(a.Data, 0))
                    if (Num(r.Type, r.Data, r.Str, out v)) return v;
            return def;
        }

        int GetInt(AxAttr a, int def)
        {
            return (int)GetFloat(a, def);
        }

        static bool Num(int type, uint data, string str, out double v)
        {
            v = 0;
            switch (type)
            {
                case 0x04: v = BitConverter.ToSingle(BitConverter.GetBytes(data), 0); return true;
                case 0x05:
                case 0x06: v = Complex(data); return true;
                case 0x10: v = (int)data; return true;
                case 0x11: v = data; return true;
                case 0x12: v = data != 0 ? 1 : 0; return true;
                case 0x03:
                    return str != null && double.TryParse(str, NumberStyles.Float, CultureInfo.InvariantCulture, out v);
            }
            return false;
        }

        static double Complex(uint data)
        {
            int mantissa = (int)(data & 0xFFFFFF00);
            switch ((data >> 4) & 3)
            {
                case 0: return mantissa / 256.0;
                case 1: return mantissa / 32768.0;
                case 2: return mantissa / 8388608.0;
                default: return mantissa / 2147483648.0;
            }
        }

        // --- fallback + bitmaps

        bool DrawGuess(DrawingContext dc, Rect box)
        {
            string[] names = { "ic_launcher", "ic_launcher_round", "app_icon", "icon", "ic_launcher_foreground" };
            ZipIndex bestZip = null;
            string best = null;
            int bestScore = -1;
            foreach (var z in zips)
                foreach (var e in z.Entries)
                {
                    string n = e.FullName;
                    if (!n.StartsWith("res/") || !IsRaster(n)) continue;
                    string file = Path.GetFileName(n).ToLowerInvariant();
                    int dot = file.IndexOf('.');
                    int rank = Array.IndexOf(names, dot > 0 ? file.Substring(0, dot) : file);
                    if (rank < 0) continue;
                    string lower = n.ToLowerInvariant();
                    int dens = lower.Contains("xxxhdpi") ? 640 : lower.Contains("xxhdpi") ? 480 : lower.Contains("xhdpi") ? 320
                        : lower.Contains("hdpi") ? 240 : lower.Contains("mdpi") ? 160 : 100;
                    int score = dens * 10 + (names.Length - rank);
                    if (score > bestScore) { bestScore = score; best = n; bestZip = z; }
                }
            if (best == null) return false;
            var bmp = LoadBitmap(bestZip.Read(best, MaxFile));
            if (bmp == null) return false;
            DrawBitmap(dc, bmp, box);
            return true;
        }

        static BitmapSource LoadBitmap(byte[] bytes)
        {
            if (bytes == null || bytes.Length == 0) return null;
            try
            {
                var dec = BitmapDecoder.Create(new MemoryStream(bytes, false),
                    BitmapCreateOptions.IgnoreColorProfile, BitmapCacheOption.OnLoad);
                if (dec.Frames.Count == 0) return null;
                BitmapSource f = dec.Frames[0];
                if (f.PixelWidth <= 0 || f.PixelHeight <= 0) return null;
                return f;
            }
            catch { return null; }   // e.g. WebP without the "WebP Image Extensions" codec
        }

        static void DrawBitmap(DrawingContext dc, BitmapSource bmp, Rect box)
        {
            double w = bmp.PixelWidth, h = bmp.PixelHeight;
            double s = Math.Min(box.Width / w, box.Height / h);
            dc.DrawImage(bmp, new Rect(box.X + (box.Width - w * s) / 2, box.Y + (box.Height - h * s) / 2, w * s, h * s));
        }

        static byte[] Rasterize(Drawing drawing, int size)
        {
            var dv = new DrawingVisual();
            RenderOptions.SetBitmapScalingMode(dv, BitmapScalingMode.HighQuality);
            using (var dc = dv.RenderOpen()) dc.DrawDrawing(drawing);
            var rtb = new RenderTargetBitmap(size, size, 96, 96, PixelFormats.Pbgra32);
            rtb.Render(dv);
            var bgra = new FormatConvertedBitmap(rtb, PixelFormats.Bgra32, null, 0);
            var px = new byte[size * size * 4];
            bgra.CopyPixels(px, size * 4, 0);
            return px;
        }
    }

    // ------------------------------------------------------------------ SVG / Android pathData

    static class SvgPath
    {
        public static Geometry Parse(string s, FillRule rule)
        {
            if (string.IsNullOrEmpty(s)) return null;
            try
            {
                var geo = new PathGeometry();
                geo.FillRule = rule;
                PathFigure fig = null;
                Point cur = new Point(), start = new Point(), ctrl = new Point();
                char cmd = ' ', prev = ' ';
                int i = 0;
                while (true)
                {
                    Skip(s, ref i);
                    if (i >= s.Length) break;
                    char c = s[i];
                    if (char.IsLetter(c) && c != 'e' && c != 'E')
                    {
                        cmd = c;
                        i++;
                    }
                    else if (cmd == ' ' || cmd == 'z' || cmd == 'Z') break;
                    else if (cmd == 'M') cmd = 'L';
                    else if (cmd == 'm') cmd = 'l';

                    bool rel = char.IsLower(cmd);
                    char up = char.ToUpperInvariant(cmd), pu = char.ToUpperInvariant(prev);
                    switch (up)
                    {
                        case 'M':
                            {
                                Point p = Pt(s, ref i, rel, cur);
                                fig = new PathFigure();
                                fig.StartPoint = p;
                                geo.Figures.Add(fig);
                                cur = start = ctrl = p;
                                break;
                            }
                        case 'L':
                            {
                                Point p = Pt(s, ref i, rel, cur);
                                Figure(ref fig, geo, cur).Segments.Add(new LineSegment(p, true));
                                cur = p;
                                break;
                            }
                        case 'H':
                            {
                                double x = Num(s, ref i);
                                Point p = new Point(rel ? cur.X + x : x, cur.Y);
                                Figure(ref fig, geo, cur).Segments.Add(new LineSegment(p, true));
                                cur = p;
                                break;
                            }
                        case 'V':
                            {
                                double y = Num(s, ref i);
                                Point p = new Point(cur.X, rel ? cur.Y + y : y);
                                Figure(ref fig, geo, cur).Segments.Add(new LineSegment(p, true));
                                cur = p;
                                break;
                            }
                        case 'C':
                            {
                                Point c1 = Pt(s, ref i, rel, cur), c2 = Pt(s, ref i, rel, cur), p = Pt(s, ref i, rel, cur);
                                Figure(ref fig, geo, cur).Segments.Add(new BezierSegment(c1, c2, p, true));
                                ctrl = c2;
                                cur = p;
                                break;
                            }
                        case 'S':
                            {
                                Point c1 = (pu == 'C' || pu == 'S') ? Reflect(ctrl, cur) : cur;
                                Point c2 = Pt(s, ref i, rel, cur), p = Pt(s, ref i, rel, cur);
                                Figure(ref fig, geo, cur).Segments.Add(new BezierSegment(c1, c2, p, true));
                                ctrl = c2;
                                cur = p;
                                break;
                            }
                        case 'Q':
                            {
                                Point c1 = Pt(s, ref i, rel, cur), p = Pt(s, ref i, rel, cur);
                                Figure(ref fig, geo, cur).Segments.Add(new QuadraticBezierSegment(c1, p, true));
                                ctrl = c1;
                                cur = p;
                                break;
                            }
                        case 'T':
                            {
                                Point c1 = (pu == 'Q' || pu == 'T') ? Reflect(ctrl, cur) : cur;
                                Point p = Pt(s, ref i, rel, cur);
                                Figure(ref fig, geo, cur).Segments.Add(new QuadraticBezierSegment(c1, p, true));
                                ctrl = c1;
                                cur = p;
                                break;
                            }
                        case 'A':
                            {
                                double rx = Num(s, ref i), ry = Num(s, ref i), rot = Num(s, ref i);
                                bool large = Flag(s, ref i), sweep = Flag(s, ref i);
                                Point p = Pt(s, ref i, rel, cur);
                                Figure(ref fig, geo, cur).Segments.Add(new ArcSegment(p, new Size(Math.Abs(rx), Math.Abs(ry)), rot, large,
                                    sweep ? SweepDirection.Clockwise : SweepDirection.Counterclockwise, true));
                                cur = p;
                                break;
                            }
                        case 'Z':
                            if (fig != null) fig.IsClosed = true;
                            cur = start;
                            break;
                        default:
                            return null;
                    }
                    prev = cmd;
                }
                geo.Freeze();
                return geo;
            }
            catch { return null; }
        }

        static PathFigure Figure(ref PathFigure fig, PathGeometry geo, Point cur)
        {
            if (fig == null || fig.IsClosed)
            {
                fig = new PathFigure();
                fig.StartPoint = cur;
                geo.Figures.Add(fig);
            }
            return fig;
        }

        static Point Reflect(Point ctrl, Point cur)
        {
            return new Point(2 * cur.X - ctrl.X, 2 * cur.Y - ctrl.Y);
        }

        static Point Pt(string s, ref int i, bool rel, Point cur)
        {
            double x = Num(s, ref i), y = Num(s, ref i);
            return rel ? new Point(cur.X + x, cur.Y + y) : new Point(x, y);
        }

        static void Skip(string s, ref int i)
        {
            while (i < s.Length && (char.IsWhiteSpace(s[i]) || s[i] == ',')) i++;
        }

        static bool Flag(string s, ref int i)
        {
            Skip(s, ref i);
            char c = s[i++];
            if (c != '0' && c != '1') throw new FormatException();
            return c == '1';
        }

        static double Num(string s, ref int i)
        {
            Skip(s, ref i);
            int st = i;
            if (i < s.Length && (s[i] == '-' || s[i] == '+')) i++;
            bool dot = false;
            while (i < s.Length)
            {
                char c = s[i];
                if (c >= '0' && c <= '9') i++;
                else if (c == '.' && !dot) { dot = true; i++; }
                else break;
            }
            if (i < s.Length && (s[i] == 'e' || s[i] == 'E'))
            {
                int save = i++;
                if (i < s.Length && (s[i] == '-' || s[i] == '+')) i++;
                if (i < s.Length && char.IsDigit(s[i])) while (i < s.Length && char.IsDigit(s[i])) i++;
                else i = save;
            }
            if (i == st) throw new FormatException();
            return double.Parse(s.Substring(st, i - st), NumberStyles.Float, CultureInfo.InvariantCulture);
        }
    }

    // ------------------------------------------------------------------ binary XML (AXML)

    sealed class AxAttr
    {
        public string Name;
        public uint ResId;
        public int Type;
        public uint Data;
        public string Str;
    }

    sealed class AxElement
    {
        public string Name;
        public int Depth;
        public readonly List<AxAttr> Attrs = new List<AxAttr>();

        // match by name first; fall back to the android attribute id when names are stripped
        public AxAttr Get(string name, uint id)
        {
            foreach (var a in Attrs)
                if (a.Name == name) return a;
            if (id != 0)
                foreach (var a in Attrs)
                    if (a.ResId == id) return a;
            return null;
        }
    }

    static class Axml
    {
        public static List<AxElement> Parse(byte[] d)
        {
            var list = new List<AxElement>();
            if (d == null || d.Length < 8 || Bin.U16(d, 0) != 0x0003) return list;
            try
            {
                StringPool pool = null;
                uint[] resMap = new uint[0];
                int depth = 0;
                int pos = Bin.U16(d, 2);
                int end = (int)Math.Min((long)d.Length, Bin.U32(d, 4));
                while (pos + 8 <= end)
                {
                    int type = Bin.U16(d, pos), hs = Bin.U16(d, pos + 2);
                    int size = (int)Bin.U32(d, pos + 4);
                    if (size < 8 || pos + size > end) break;
                    if (type == 0x0001)
                    {
                        pool = new StringPool(d, pos);
                    }
                    else if (type == 0x0180)
                    {
                        resMap = new uint[(size - hs) / 4];
                        for (int i = 0; i < resMap.Length; i++) resMap[i] = Bin.U32(d, pos + hs + i * 4);
                    }
                    else if (type == 0x0102 && pool != null)
                    {
                        int ext = pos + hs;
                        var el = new AxElement();
                        el.Name = pool.Get(Bin.U32(d, ext + 4));
                        el.Depth = depth;
                        int attrStart = Bin.U16(d, ext + 8), attrSize = Bin.U16(d, ext + 10), attrCount = Bin.U16(d, ext + 12);
                        for (int i = 0; i < attrCount; i++)
                        {
                            int a = ext + attrStart + i * attrSize;
                            uint nameIdx = Bin.U32(d, a + 4);
                            var at = new AxAttr();
                            at.Name = pool.Get(nameIdx);
                            at.ResId = nameIdx < resMap.Length ? resMap[nameIdx] : 0;
                            at.Type = d[a + 15];
                            at.Data = Bin.U32(d, a + 16);
                            uint raw = Bin.U32(d, a + 8);
                            at.Str = at.Type == 0x03 ? pool.Get(at.Data) : (raw != 0xFFFFFFFF ? pool.Get(raw) : null);
                            el.Attrs.Add(at);
                        }
                        list.Add(el);
                        depth++;
                    }
                    else if (type == 0x0103)
                    {
                        depth--;
                    }
                    pos += size;
                }
            }
            catch { }
            return list;
        }
    }

    // ------------------------------------------------------------------ resources.arsc

    sealed class ResValue
    {
        public int Type;
        public uint Data;
        public string Str;
        public int Density;
        public int Sdk;
    }

    sealed class ArscTable
    {
        readonly byte[] d;
        StringPool values;
        readonly Dictionary<int, List<int>> types = new Dictionary<int, List<int>>();
        readonly List<int> packageIds = new List<int>();

        public ArscTable(byte[] data)
        {
            d = data;
            if (d.Length < 12 || Bin.U16(d, 0) != 0x0002) return;
            int pos = Bin.U16(d, 2);
            int end = (int)Math.Min((long)d.Length, Bin.U32(d, 4));
            while (pos + 8 <= end)
            {
                int type = Bin.U16(d, pos), hs = Bin.U16(d, pos + 2);
                int size = (int)Bin.U32(d, pos + 4);
                if (size < 8 || pos + size > end) break;
                if (type == 0x0001 && values == null) values = new StringPool(d, pos);
                else if (type == 0x0200) ParsePackage(pos, hs, size);
                pos += size;
            }
        }

        void ParsePackage(int p, int hs, int size)
        {
            int id = (int)Bin.U32(d, p + 8);
            packageIds.Add(id);
            int pos = p + hs, end = p + size;
            while (pos + 8 <= end)
            {
                int type = Bin.U16(d, pos);
                int csize = (int)Bin.U32(d, pos + 4);
                if (csize < 8 || pos + csize > end) break;
                if (type == 0x0201)
                {
                    int key = (id << 8) | d[pos + 8];
                    List<int> l;
                    if (!types.TryGetValue(key, out l)) types[key] = l = new List<int>();
                    l.Add(pos);
                }
                pos += csize;
            }
        }

        public void Lookup(uint resId, List<ResValue> output)
        {
            int pkg = (int)(resId >> 24), typeId = (int)((resId >> 16) & 0xFF), entry = (int)(resId & 0xFFFF);
            List<int> chunks;
            if (!types.TryGetValue((pkg << 8) | typeId, out chunks))
            {
                // shared-library style tables use package id 0
                if (packageIds.Count != 1 || !types.TryGetValue((packageIds[0] << 8) | typeId, out chunks)) return;
            }
            foreach (int c in chunks)
            {
                try
                {
                    var v = ReadEntry(c, entry);
                    if (v != null) output.Add(v);
                }
                catch { }
            }
        }

        ResValue ReadEntry(int c, int entry)
        {
            int hs = Bin.U16(d, c + 2);
            int flags = d[c + 9];
            int count = (int)Bin.U32(d, c + 12);
            int entriesStart = (int)Bin.U32(d, c + 16);
            int cfg = c + 20;
            int cfgSize = (int)Bin.U32(d, cfg);
            int offsets = c + hs;
            long off = -1;

            if ((flags & 0x01) != 0)            // FLAG_SPARSE
            {
                int lo = 0, hi = count - 1;
                while (lo <= hi)
                {
                    int mid = (lo + hi) >> 1;
                    int idx = Bin.U16(d, offsets + mid * 4);
                    if (idx == entry) { off = Bin.U16(d, offsets + mid * 4 + 2) * 4L; break; }
                    if (idx < entry) lo = mid + 1; else hi = mid - 1;
                }
            }
            else if ((flags & 0x02) != 0)       // FLAG_OFFSET16
            {
                if (entry >= count) return null;
                int o = Bin.U16(d, offsets + entry * 2);
                if (o != 0xFFFF) off = o * 4L;
            }
            else
            {
                if (entry >= count) return null;
                uint o = Bin.U32(d, offsets + entry * 4);
                if (o != 0xFFFFFFFF) off = o;
            }
            if (off < 0) return null;

            int ep = c + entriesStart + (int)off;
            int eSize = Bin.U16(d, ep), eFlags = Bin.U16(d, ep + 2);
            var v = new ResValue();
            if ((eFlags & 0x08) != 0)           // FLAG_COMPACT
            {
                v.Type = eFlags >> 8;
                v.Data = Bin.U32(d, ep + 4);
            }
            else if ((eFlags & 0x01) != 0)      // bag / style
            {
                return null;
            }
            else
            {
                int vp = ep + eSize;
                v.Type = d[vp + 3];
                v.Data = Bin.U32(d, vp + 4);
            }
            if (v.Type == 0x03 && values != null) v.Str = values.Get(v.Data);
            if (cfgSize >= 16) v.Density = Bin.U16(d, cfg + 14);
            if (cfgSize >= 28) v.Sdk = Bin.U16(d, cfg + 24);
            return v;
        }
    }

    sealed class StringPool
    {
        readonly byte[] d;
        readonly int offsets, strings, count;
        readonly bool utf8;
        readonly string[] cache;

        public StringPool(byte[] data, int p)
        {
            d = data;
            int hs = Bin.U16(d, p + 2);
            count = (int)Math.Min(Bin.U32(d, p + 8), (uint)(d.Length / 4));
            utf8 = (Bin.U32(d, p + 16) & 0x100) != 0;
            strings = p + (int)Bin.U32(d, p + 20);
            offsets = p + hs;
            cache = new string[count];
        }

        public string Get(uint index)
        {
            if (index >= (uint)count) return null;
            string s = cache[index];
            if (s != null) return s;
            try
            {
                int p = strings + (int)Bin.U32(d, offsets + (int)index * 4);
                if (utf8)
                {
                    p += (d[p] & 0x80) != 0 ? 2 : 1;       // UTF-16 length, unused
                    int len = d[p];
                    if ((len & 0x80) != 0) { len = ((len & 0x7F) << 8) | d[p + 1]; p += 2; }
                    else p += 1;
                    s = Encoding.UTF8.GetString(d, p, len);
                }
                else
                {
                    int len = Bin.U16(d, p);
                    if ((len & 0x8000) != 0) { len = ((len & 0x7FFF) << 16) | Bin.U16(d, p + 2); p += 4; }
                    else p += 2;
                    s = Encoding.Unicode.GetString(d, p, len * 2);
                }
            }
            catch { s = ""; }
            cache[index] = s;
            return s;
        }
    }

    static class Bin
    {
        public static int U16(byte[] d, int p) { return d[p] | (d[p + 1] << 8); }
        public static uint U32(byte[] d, int p) { return (uint)(d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | (d[p + 3] << 24)); }
    }

    // ------------------------------------------------------------------ zip + streams

    sealed class ZipIndex : IDisposable
    {
        readonly ZipArchive zip;
        readonly Dictionary<string, ZipArchiveEntry> map = new Dictionary<string, ZipArchiveEntry>(StringComparer.Ordinal);
        readonly Dictionary<string, ZipArchiveEntry> mapCi = new Dictionary<string, ZipArchiveEntry>(StringComparer.OrdinalIgnoreCase);

        public ZipIndex(Stream s)
        {
            zip = new ZipArchive(s, ZipArchiveMode.Read, true);
            foreach (var e in zip.Entries)
            {
                if (!map.ContainsKey(e.FullName)) map[e.FullName] = e;
                if (!mapCi.ContainsKey(e.FullName)) mapCi[e.FullName] = e;
            }
        }

        public IEnumerable<ZipArchiveEntry> Entries { get { return map.Values; } }

        public ZipArchiveEntry Find(string name)
        {
            ZipArchiveEntry e;
            if (map.TryGetValue(name, out e) || mapCi.TryGetValue(name, out e)) return e;
            return null;
        }

        public byte[] Read(string name, long max)
        {
            var e = Find(name);
            return e == null ? null : ReadEntry(e, max);
        }

        public static byte[] ReadEntry(ZipArchiveEntry e, long max)
        {
            if (e.Length <= 0 || e.Length > max) return null;
            try
            {
                var buf = new byte[e.Length];
                using (var s = e.Open())
                {
                    int total = 0;
                    while (total < buf.Length)
                    {
                        int n = s.Read(buf, total, buf.Length - total);
                        if (n <= 0) break;
                        total += n;
                    }
                    if (total != buf.Length) return null;
                }
                return buf;
            }
            catch { return null; }
        }

        public void Dispose() { zip.Dispose(); }
    }

    // Read-only, seekable System.IO.Stream over a COM IStream.
    sealed class ComStream : Stream
    {
        readonly IStream s;
        readonly long length;
        IntPtr scratch;
        long pos;

        public ComStream(IStream stream)
        {
            s = stream;
            System.Runtime.InteropServices.ComTypes.STATSTG st;
            s.Stat(out st, 1);   // STATFLAG_NONAME
            length = st.cbSize;
            scratch = Marshal.AllocCoTaskMem(8);
            s.Seek(0, 0, IntPtr.Zero);
        }

        public override bool CanRead { get { return true; } }
        public override bool CanSeek { get { return true; } }
        public override bool CanWrite { get { return false; } }
        public override long Length { get { return length; } }
        public override long Position { get { return pos; } set { Seek(value, SeekOrigin.Begin); } }
        public override void Flush() { }
        public override void SetLength(long value) { throw new NotSupportedException(); }
        public override void Write(byte[] buffer, int offset, int count) { throw new NotSupportedException(); }

        public override int Read(byte[] buffer, int offset, int count)
        {
            if (count <= 0) return 0;
            byte[] target = offset == 0 ? buffer : new byte[count];
            Marshal.WriteInt32(scratch, 0);
            s.Read(target, count, scratch);
            int n = Marshal.ReadInt32(scratch);
            if (offset != 0 && n > 0) Buffer.BlockCopy(target, 0, buffer, offset, n);
            pos += n;
            return n;
        }

        public override long Seek(long offset, SeekOrigin origin)
        {
            s.Seek(offset, (int)origin, scratch);
            pos = Marshal.ReadInt64(scratch);
            return pos;
        }

        protected override void Dispose(bool disposing)
        {
            if (scratch != IntPtr.Zero)
            {
                Marshal.FreeCoTaskMem(scratch);
                scratch = IntPtr.Zero;
            }
            base.Dispose(disposing);
        }
    }

    static class Native
    {
        [StructLayout(LayoutKind.Sequential)]
        struct ICONINFO
        {
            public bool fIcon;
            public int xHotspot, yHotspot;
            public IntPtr hbmMask, hbmColor;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct BITMAPINFOHEADER
        {
            public int biSize, biWidth, biHeight;
            public short biPlanes, biBitCount;
            public int biCompression, biSizeImage, biXPelsPerMeter, biYPelsPerMeter, biClrUsed, biClrImportant;
        }

        [DllImport("gdi32.dll")]
        static extern IntPtr CreateDIBSection(IntPtr hdc, ref BITMAPINFOHEADER bmi, uint usage, out IntPtr bits, IntPtr section, uint offset);

        [DllImport("gdi32.dll")]
        static extern IntPtr CreateBitmap(int width, int height, uint planes, uint bitsPerPixel, byte[] bits);

        [DllImport("gdi32.dll")]
        static extern bool DeleteObject(IntPtr obj);

        [DllImport("user32.dll")]
        static extern IntPtr CreateIconIndirect(ref ICONINFO iconInfo);

        [DllImport("user32.dll")]
        public static extern bool DestroyIcon(IntPtr icon);

        public static IntPtr CreateBitmap(byte[] bgra, int w, int h)
        {
            var bi = new BITMAPINFOHEADER();
            bi.biSize = Marshal.SizeOf(typeof(BITMAPINFOHEADER));
            bi.biWidth = w;
            bi.biHeight = -h;    // top-down
            bi.biPlanes = 1;
            bi.biBitCount = 32;
            IntPtr bits;
            IntPtr hbmp = CreateDIBSection(IntPtr.Zero, ref bi, 0, out bits, IntPtr.Zero, 0);
            if (hbmp != IntPtr.Zero) Marshal.Copy(bgra, 0, bits, bgra.Length);
            return hbmp;
        }

        public static IntPtr CreateIcon(string path, int size)
        {
            try
            {
                byte[] pixels = IconRenderer.RenderOnSta(delegate
                {
                    return new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                }, size);
                if (pixels == null) return IntPtr.Zero;

                // all-zero AND mask: the colour bitmap's alpha channel decides transparency
                IntPtr color = CreateBitmap(pixels, size, size);
                IntPtr mask = CreateBitmap(size, size, 1, 1, new byte[(size + 15) / 16 * 2 * size]);
                if (color == IntPtr.Zero || mask == IntPtr.Zero)
                {
                    if (color != IntPtr.Zero) DeleteObject(color);
                    if (mask != IntPtr.Zero) DeleteObject(mask);
                    return IntPtr.Zero;
                }

                var info = new ICONINFO { fIcon = true, hbmColor = color, hbmMask = mask };
                IntPtr icon = CreateIconIndirect(ref info);
                DeleteObject(color);
                DeleteObject(mask);
                return icon;
            }
            catch { return IntPtr.Zero; }
        }
    }
}
