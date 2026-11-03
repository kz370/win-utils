using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using usbToggle.Models;

namespace usbToggle.Services;

public class SetupApiDeviceCatalog : IDisposable
{
    private readonly Guid _allClasses = Guid.Empty;

    public List<DiscoveredDevice> ScanPresent()
    {
        return Scan(NativeMethods.DIGCF_ALLCLASSES | NativeMethods.DIGCF_PRESENT);
    }

    public List<DiscoveredDevice> ScanRetained()
    {
        return Scan(NativeMethods.DIGCF_ALLCLASSES);
    }

    private List<DiscoveredDevice> Scan(uint flags)
    {
        var devices = new List<DiscoveredDevice>();
        IntPtr hdevInfo = NativeMethods.SetupDiGetClassDevsW(ref _allClasses, null, IntPtr.Zero, flags);
        if (hdevInfo == NativeMethods.InvalidHandleValue)
        {
            return devices;
        }

        try
        {
            uint index = 0;
            while (true)
            {
                var data = new NativeMethods.SP_DEVINFO_DATA { cbSize = (uint)Marshal.SizeOf<NativeMethods.SP_DEVINFO_DATA>() };
                if (!NativeMethods.SetupDiEnumDeviceInfo(hdevInfo, index, ref data))
                {
                    break;
                }

                var dev = ReadDevice(hdevInfo, data);
                if (dev != null)
                {
                    devices.Add(dev);
                }
                index++;
            }
        }
        finally
        {
            NativeMethods.SetupDiDestroyDeviceInfoList(hdevInfo);
        }

        return devices;
    }

    private DiscoveredDevice? ReadDevice(IntPtr hdevInfo, NativeMethods.SP_DEVINFO_DATA data)
    {
        string instanceId = GetStringProperty(hdevInfo, data, readInstanceId: true) ?? string.Empty;
        if (string.IsNullOrEmpty(instanceId))
        {
            return null;
        }

        string deviceDesc = GetStringProperty(hdevInfo, data, NativeMethods.SPDRP_DEVICEDESC) ?? string.Empty;
        string[] hardwareIds = GetMultiStringProperty(hdevInfo, data, NativeMethods.SPDRP_HARDWAREID);
        string service = GetStringProperty(hdevInfo, data, NativeMethods.SPDRP_SERVICE) ?? string.Empty;
        string[] locationPaths = GetMultiStringProperty(hdevInfo, data, NativeMethods.SPDRP_LOCATION_PATHS);
        string containerId = GetStringProperty(hdevInfo, data, NativeMethods.SPDRP_CONTAINERID) ?? string.Empty;

        uint status = 0, problem = 0;
        uint cr = NativeMethods.CM_Get_DevNode_Status(out status, out problem, data.devInst, 0);

        var dev = new DiscoveredDevice
        {
            InstanceId = instanceId,
            DeviceDesc = deviceDesc,
            HardwareIds = hardwareIds,
            Service = service,
            LocationPaths = locationPaths,
            ContainerId = containerId,
            DevNodeStatus = cr == NativeMethods.CR_SUCCESS ? status : 0,
            Problem = cr == NativeMethods.CR_SUCCESS ? problem : 0,
        };

        ParseIdentity(dev);
        return dev;
    }

    private string? GetStringProperty(IntPtr hdevInfo, NativeMethods.SP_DEVINFO_DATA data, uint property, bool readInstanceId = false)
    {
        if (readInstanceId)
        {
            if (!NativeMethods.SetupDiGetDeviceInstanceIdW(hdevInfo, ref data, IntPtr.Zero, 0, out uint requiredSize) && requiredSize == 0)
            {
                return null;
            }
            IntPtr buf = Marshal.AllocCoTaskMem((int)requiredSize);
            try
            {
                if (!NativeMethods.SetupDiGetDeviceInstanceIdW(hdevInfo, ref data, buf, requiredSize, out _))
                {
                    return null;
                }
                return Marshal.PtrToStringUni(buf);
            }
            finally
            {
                Marshal.FreeCoTaskMem(buf);
            }
        }

        if (!NativeMethods.SetupDiGetDeviceRegistryPropertyW(hdevInfo, ref data, property, out _, IntPtr.Zero, 0, out uint requiredSize))
        {
            return null;
        }
        IntPtr buf2 = Marshal.AllocCoTaskMem((int)requiredSize);
        try
        {
            if (!NativeMethods.SetupDiGetDeviceRegistryPropertyW(hdevInfo, ref data, property, out _, buf2, requiredSize, out _))
            {
                return null;
            }
            return Marshal.PtrToStringUni(buf2);
        }
        finally
        {
            Marshal.FreeCoTaskMem(buf2);
        }
    }

    private string[] GetMultiStringProperty(IntPtr hdevInfo, NativeMethods.SP_DEVINFO_DATA data, uint property)
    {
        if (!NativeMethods.SetupDiGetDeviceRegistryPropertyW(hdevInfo, ref data, property, out _, IntPtr.Zero, 0, out uint requiredSize))
        {
            return Array.Empty<string>();
        }
        IntPtr buf = Marshal.AllocCoTaskMem((int)requiredSize);
        try
        {
            if (!NativeMethods.SetupDiGetDeviceRegistryPropertyW(hdevInfo, ref data, property, out _, buf, requiredSize, out _))
            {
                return Array.Empty<string>();
            }
            return ReadMultiString(buf, (int)requiredSize);
        }
        finally
        {
            Marshal.FreeCoTaskMem(buf);
        }
    }

    private static string[] ReadMultiString(IntPtr buffer, int byteCount)
    {
        if (byteCount <= 0)
        {
            return Array.Empty<string>();
        }
        short[] shorts = new short[byteCount / 2];
        Marshal.Copy(buffer, shorts, 0, shorts.Length);
        string text = new string(shorts.Select(s => (char)s).ToArray());
        return text.Split('\0', StringSplitOptions.RemoveEmptyEntries);
    }

    private static void ParseIdentity(DiscoveredDevice dev)
    {
        string? vid = null, pid = null;
        foreach (string hw in dev.HardwareIds)
        {
            string h = hw.Trim().ToUpperInvariant();
            if (h.StartsWith("USB\\VID_"))
            {
                int vidIdx = h.IndexOf("VID_", StringComparison.Ordinal) + 4;
                int pidIdx = h.IndexOf("PID_", StringComparison.Ordinal);
                if (vidIdx > 3 && pidIdx > vidIdx)
                {
                    int end = h.IndexOf('&', pidIdx);
                    if (end < 0) end = h.Length;
                    vid = h.Substring(vidIdx, 4);
                    pid = h.Substring(pidIdx + 4, end - (pidIdx + 4));
                    break;
                }
            }
        }
        dev.Vid = vid;
        dev.Pid = pid;

        string? serial = null;
        if (dev.InstanceId.StartsWith("USB\\", StringComparison.OrdinalIgnoreCase))
        {
            int last = dev.InstanceId.LastIndexOf('\\');
            if (last >= 0 && last + 1 < dev.InstanceId.Length)
            {
                string candidate = dev.InstanceId.Substring(last + 1);
                if (!string.IsNullOrEmpty(candidate) && candidate.Length >= 3)
                {
                    serial = candidate;
                }
            }
        }
        dev.Serial = serial;
    }

    public static DeviceIdentity BuildIdentity(DiscoveredDevice dev)
    {
        return new DeviceIdentity
        {
            Vid = dev.Vid,
            Pid = dev.Pid,
            Serial = dev.Serial,
            LocationPath = dev.LocationPaths.FirstOrDefault(),
        };
    }

    public List<(DiscoveredDevice Device, List<DiscoveredDevice> Children)> BuildHubTree(List<DiscoveredDevice> devices)
    {
        var byDevInst = devices.ToDictionary(d => d.InstanceId, d => d);
        var hubs = devices.Where(d => d.IsHub).ToList();
        var result = new List<(DiscoveredDevice, List<DiscoveredDevice>)>();

        foreach (DiscoveredDevice hub in hubs)
        {
            var children = new List<DiscoveredDevice>();
            foreach (DiscoveredDevice dev in devices)
            {
                if (dev.InstanceId == hub.InstanceId) continue;
                string? parentHub = FindParentHubId(dev, devices);
                if (parentHub == hub.InstanceId)
                {
                    children.Add(dev);
                }
            }
            result.Add((hub, children));
        }

        var other = devices.Where(d => !d.IsHub && !result.SelectMany(t => t.Children).Select(c => c.InstanceId).Contains(d.InstanceId) && !result.Select(t => t.Device.InstanceId).Contains(d.InstanceId)).ToList();
        if (other.Count > 0)
        {
            var otherHub = new DiscoveredDevice
            {
                InstanceId = "other",
                DeviceDesc = "Other USB devices",
                Service = "OTHER",
                IsHub = true,
            };
            result.Add((otherHub, other));
        }

        return result;
    }

    private static string? FindParentHubId(DiscoveredDevice dev, List<DiscoveredDevice> devices)
    {
        uint parent = dev.ParentDevInst;
        while (parent != 0)
        {
            var candidate = devices.FirstOrDefault(d => MatchDevInst(d, parent));
            if (candidate != null && candidate.IsHub)
            {
                return candidate.InstanceId;
            }
            parent = 0; // we only have devInst for enumerated devices; cannot walk further without reopening CM
        }
        return null;
    }

    private static bool MatchDevInst(DiscoveredDevice dev, uint devInst)
    {
        return false;
    }

    public void Dispose()
    {
    }

    private static class InvalidHandleValueHolder
    {
        public static readonly IntPtr Value = new IntPtr(-1);
    }

    public static IntPtr InvalidHandleValue => InvalidHandleValueHolder.Value;
}
