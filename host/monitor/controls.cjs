const {execFile}=require("child_process");
const IS_WINDOWS=process.platform==="win32";
function runPowerShell(script, timeoutMs = 5000) {
  return new Promise((resolve, reject) => {
    execFile(
      "powershell.exe",
      ["-NoProfile", "-Command", script],
      { timeout: timeoutMs, windowsHide: true },
      (error, stdout, stderr) => {
        if (error) {
          reject(new Error(stderr || error.message));
          return;
        }
        resolve(stdout.trim());
      }
    );
  });
}

function switchToSingleDisplay() {
  return new Promise((resolve, reject) => {
    if (!IS_WINDOWS) {
      resolve({ ok: true, message: "Mock single-display mode accepted." });
      return;
    }

    const script = `
      Add-Type -AssemblyName System.Windows.Forms
      $primary = [System.Windows.Forms.Screen]::PrimaryScreen.DeviceName
      Add-Type -TypeDefinition @"
        using System;
        using System.Collections.Generic;
        using System.Runtime.InteropServices;

        public static class DirectDisplaySwitch {
          private const uint QDC_ONLY_ACTIVE_PATHS = 0x00000002;
          private const uint SDC_USE_SUPPLIED_DISPLAY_CONFIG = 0x00000020;
          private const uint SDC_APPLY = 0x00000080;
          private const uint SDC_SAVE_TO_DATABASE = 0x00000200;
          private const uint SDC_ALLOW_CHANGES = 0x00000400;
          private const int ERROR_SUCCESS = 0;
          private const int DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME = 1;

          [StructLayout(LayoutKind.Sequential)]
          public struct LUID {
            public uint LowPart;
            public int HighPart;
          }

          [StructLayout(LayoutKind.Sequential)]
          public struct DISPLAYCONFIG_RATIONAL {
            public uint Numerator;
            public uint Denominator;
          }

          [StructLayout(LayoutKind.Sequential)]
          public struct DISPLAYCONFIG_PATH_SOURCE_INFO {
            public LUID adapterId;
            public uint id;
            public uint modeInfoIdx;
            public uint statusFlags;
          }

          [StructLayout(LayoutKind.Sequential)]
          public struct DISPLAYCONFIG_PATH_TARGET_INFO {
            public LUID adapterId;
            public uint id;
            public uint modeInfoIdx;
            public uint outputTechnology;
            public uint rotation;
            public uint scaling;
            public DISPLAYCONFIG_RATIONAL refreshRate;
            public uint scanLineOrdering;
            [MarshalAs(UnmanagedType.Bool)]
            public bool targetAvailable;
            public uint statusFlags;
          }

          [StructLayout(LayoutKind.Sequential)]
          public struct DISPLAYCONFIG_PATH_INFO {
            public DISPLAYCONFIG_PATH_SOURCE_INFO sourceInfo;
            public DISPLAYCONFIG_PATH_TARGET_INFO targetInfo;
            public uint flags;
          }

          [StructLayout(LayoutKind.Sequential)]
          public struct DISPLAYCONFIG_2DREGION {
            public uint cx;
            public uint cy;
          }

          [StructLayout(LayoutKind.Sequential)]
          public struct DISPLAYCONFIG_VIDEO_SIGNAL_INFO {
            public ulong pixelRate;
            public DISPLAYCONFIG_RATIONAL hSyncFreq;
            public DISPLAYCONFIG_RATIONAL vSyncFreq;
            public DISPLAYCONFIG_2DREGION activeSize;
            public DISPLAYCONFIG_2DREGION totalSize;
            public uint videoStandard;
            public uint scanLineOrdering;
          }

          [StructLayout(LayoutKind.Sequential)]
          public struct DISPLAYCONFIG_TARGET_MODE {
            public DISPLAYCONFIG_VIDEO_SIGNAL_INFO targetVideoSignalInfo;
          }

          [StructLayout(LayoutKind.Sequential)]
          public struct POINTL {
            public int x;
            public int y;
          }

          [StructLayout(LayoutKind.Sequential)]
          public struct DISPLAYCONFIG_SOURCE_MODE {
            public uint width;
            public uint height;
            public uint pixelFormat;
            public POINTL position;
          }

          [StructLayout(LayoutKind.Sequential)]
          public struct RECT {
            public int left;
            public int top;
            public int right;
            public int bottom;
          }

          [StructLayout(LayoutKind.Sequential)]
          public struct DISPLAYCONFIG_DESKTOP_IMAGE_INFO {
            public POINTL PathSourceSize;
            public RECT DesktopImageRegion;
            public RECT DesktopImageClip;
          }

          [StructLayout(LayoutKind.Explicit)]
          public struct DISPLAYCONFIG_MODE_INFO_UNION {
            [FieldOffset(0)]
            public DISPLAYCONFIG_TARGET_MODE targetMode;
            [FieldOffset(0)]
            public DISPLAYCONFIG_SOURCE_MODE sourceMode;
            [FieldOffset(0)]
            public DISPLAYCONFIG_DESKTOP_IMAGE_INFO desktopImageInfo;
          }

          [StructLayout(LayoutKind.Sequential)]
          public struct DISPLAYCONFIG_MODE_INFO {
            public uint infoType;
            public uint id;
            public LUID adapterId;
            public DISPLAYCONFIG_MODE_INFO_UNION modeInfo;
          }

          [StructLayout(LayoutKind.Sequential)]
          public struct DISPLAYCONFIG_DEVICE_INFO_HEADER {
            public int type;
            public uint size;
            public LUID adapterId;
            public uint id;
          }

          [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
          public struct DISPLAYCONFIG_SOURCE_DEVICE_NAME {
            public DISPLAYCONFIG_DEVICE_INFO_HEADER header;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
            public string viewGdiDeviceName;
          }

          [DllImport("user32.dll")]
          private static extern int GetDisplayConfigBufferSizes(uint flags, out uint numPathArrayElements, out uint numModeInfoArrayElements);

          [DllImport("user32.dll")]
          private static extern int QueryDisplayConfig(
            uint flags,
            ref uint numPathArrayElements,
            [Out] DISPLAYCONFIG_PATH_INFO[] pathInfoArray,
            ref uint numModeInfoArrayElements,
            [Out] DISPLAYCONFIG_MODE_INFO[] modeInfoArray,
            IntPtr currentTopologyId);

          [DllImport("user32.dll")]
          private static extern int DisplayConfigGetDeviceInfo(ref DISPLAYCONFIG_SOURCE_DEVICE_NAME requestPacket);

          [DllImport("user32.dll")]
          private static extern int SetDisplayConfig(
            uint numPathArrayElements,
            [In] DISPLAYCONFIG_PATH_INFO[] pathArray,
            uint numModeInfoArrayElements,
            [In] DISPLAYCONFIG_MODE_INFO[] modeInfoArray,
            uint flags);

          private static DISPLAYCONFIG_DEVICE_INFO_HEADER Header(int type, LUID adapterId, uint id, uint size) {
            return new DISPLAYCONFIG_DEVICE_INFO_HEADER {
              type = type,
              size = size,
              adapterId = adapterId,
              id = id
            };
          }

          private static string SourceName(DISPLAYCONFIG_PATH_INFO path) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = new DISPLAYCONFIG_SOURCE_DEVICE_NAME();
            sourceName.header = Header(
              DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,
              path.sourceInfo.adapterId,
              path.sourceInfo.id,
              (uint)Marshal.SizeOf(typeof(DISPLAYCONFIG_SOURCE_DEVICE_NAME)));

            int result = DisplayConfigGetDeviceInfo(ref sourceName);
            if (result != ERROR_SUCCESS) {
              return "";
            }
            return sourceName.viewGdiDeviceName;
          }

          public static string[] KeepOnlyPrimary(string primaryName) {
            uint pathCount;
            uint modeCount;
            int result = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, out pathCount, out modeCount);
            if (result != ERROR_SUCCESS) {
              throw new Exception("GetDisplayConfigBufferSizes failed: " + result);
            }

            DISPLAYCONFIG_PATH_INFO[] paths = new DISPLAYCONFIG_PATH_INFO[pathCount];
            DISPLAYCONFIG_MODE_INFO[] modes = new DISPLAYCONFIG_MODE_INFO[modeCount];
            result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, ref pathCount, paths, ref modeCount, modes, IntPtr.Zero);
            if (result != ERROR_SUCCESS) {
              throw new Exception("QueryDisplayConfig failed: " + result);
            }

            List<DISPLAYCONFIG_PATH_INFO> kept = new List<DISPLAYCONFIG_PATH_INFO>();
            List<string> removed = new List<string>();
            for (int i = 0; i < pathCount; i++) {
              string name = SourceName(paths[i]);
              if (String.Equals(name, primaryName, StringComparison.OrdinalIgnoreCase)) {
                kept.Add(paths[i]);
              } else if (!String.IsNullOrEmpty(name)) {
                removed.Add(name);
              }
            }

            if (kept.Count != 1) {
              throw new Exception("Primary display path not found: " + primaryName + ". Active paths: " + pathCount);
            }

            DISPLAYCONFIG_PATH_INFO[] keptPaths = kept.ToArray();
            result = SetDisplayConfig(
              (uint)keptPaths.Length,
              keptPaths,
              modeCount,
              modes,
              SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_APPLY | SDC_SAVE_TO_DATABASE | SDC_ALLOW_CHANGES);

            if (result != ERROR_SUCCESS) {
              throw new Exception("SetDisplayConfig failed: " + result);
            }
            return removed.ToArray();
          }
        }
"@
      $disabled = [DirectDisplaySwitch]::KeepOnlyPrimary($primary)
      [pscustomobject]@{
        ok = $true
        primary = $primary
        disabled = $disabled
        message = "Single-display mode applied for primary screen."
      } | ConvertTo-Json -Compress
    `;

    runPowerShell(script, 10000)
      .then(raw => resolve(JSON.parse(raw)))
      .catch(reject);
  });
}

function switchToExtendedDisplays() {
  return new Promise((resolve, reject) => {
    if (!IS_WINDOWS) {
      resolve({ ok: true, message: "Mock extended-display mode accepted." });
      return;
    }

    execFile(
      "DisplaySwitch.exe",
      ["/extend"],
      { timeout: 8000, windowsHide: true },
      (error, stdout, stderr) => {
        if (error) {
          reject(new Error(stderr.trim() || stdout.trim() || error.message));
          return;
        }
        resolve({ ok: true, message: "Extended-display mode requested." });
      }
    );
  });
}

function runPowerAction(action) {
  return new Promise((resolve, reject) => {
    if (!IS_WINDOWS) {
      resolve({ ok: true, message: `Mock power action accepted: ${action}` });
      return;
    }

    if (action === "shutdown") {
      execFile("shutdown.exe", ["/s", "/t", "0"], { timeout: 5000, windowsHide: true }, (error, stdout, stderr) => {
        if (error) {
          reject(new Error(stderr.trim() || stdout.trim() || error.message));
          return;
        }
        resolve({ ok: true, message: "Windows shutdown command sent." });
      });
      return;
    }

    if (action === "sleep") {
      const script = `
        Add-Type -TypeDefinition @"
          using System;
          using System.Runtime.InteropServices;
          public static class SleepHelper {
            [DllImport("powrprof.dll", SetLastError=true)]
            public static extern bool SetSuspendState(bool hibernate, bool forceCritical, bool disableWakeEvent);
          }
"@
        $ok = [SleepHelper]::SetSuspendState($false, $false, $false)
        if (-not $ok) { throw "SetSuspendState failed." }
      `;
      runPowerShell(script, 5000)
        .then(() => resolve({ ok: true, message: "Windows sleep command sent." }))
        .catch(reject);
      return;
    }

    reject(new Error(`Unsupported power action: ${action}`));
  });
}


module.exports={single:switchToSingleDisplay,extend:switchToExtendedDisplays,sleep:()=>runPowerAction("sleep"),shutdown:()=>runPowerAction("shutdown")};
