using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.IO.Pipes;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace WindowsFormsApp
{
    public unsafe partial class OverlayForm : Form
    {
        private const string OverlaysMapName = "OVRlay.OverlayState";

        [StructLayout(LayoutKind.Sequential)]
        private struct Vector3
        {
            #region Fields
            public float x;
            public float y;
            public float z;
            #endregion
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct Quaternion
        {
            #region Fields
            public float x;
            public float y;
            public float z;
            public float w;
            #endregion
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct Pose
        {
            #region Fields
            public Quaternion orientation;
            public Vector3 position;
            #endregion
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct OverlayState
        {
            #region Fields
            public ulong handle;
            public Pose pose;
            public float scale;
            [MarshalAs(UnmanagedType.I1)]
            public bool isMonitor;
            public byte opacity;
            public byte placement;
            [MarshalAs(UnmanagedType.I1)]
            public bool isInteractable;
            [MarshalAs(UnmanagedType.I1)]
            public bool isFrozen;
            [MarshalAs(UnmanagedType.I1)]
            public bool isMinimized;
            #endregion
        };

        [StructLayout(LayoutKind.Sequential)]
        private struct Overlays
        {
            #region Fields
            public OverlayState window0;
            public OverlayState window1;
            public OverlayState window2;
            public OverlayState window3;
            #endregion
        };

        private enum Operation
        {
            Import,
            Update,
            Remove
        }

        private struct WindowState
        {
            public int opacity;
            public int placement;
            public bool isFrozen;
            public bool isInteractable;
        }

        private MemoryMappedFile mappedFile;
        private MemoryMappedViewAccessor mappedView;
        private Overlays* overlays;
        private Dictionary<IntPtr, WindowState> windowState = new Dictionary<IntPtr, WindowState>();

        public OverlayForm()
        {
            InitializeComponent();
            import.Text = "\u25BC";
            remove.Text = "\u25B2";

            refresh_Tick(null, null);

            var size = Marshal.SizeOf<Overlays>();
            try
            {
#if !DEBUG
                mappedFile = MemoryMappedFile.OpenExisting(OverlaysMapName, MemoryMappedFileRights.ReadWrite);
#else
                mappedFile = MemoryMappedFile.CreateOrOpen(OverlaysMapName, size, MemoryMappedFileAccess.ReadWrite);
#endif
                mappedView = mappedFile.CreateViewAccessor(0, size);

                byte* ptr = null;
                mappedView.SafeMemoryMappedViewHandle.AcquirePointer(ref ptr);
                overlays = (Overlays*)ptr;

                // TODO: Read state from previous instance.
            }
            catch
            {
                MessageBox.Show(this, "Failed to open MemoryMappedFile. Make sure the Virtual Desktop Streamer (v1.30 or later) is running.", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void pushUpdate(Operation op)
        {
            var hwnd = hwndForImportedWindow[importedWindows.SelectedIndex];
            WindowState state;
            state.opacity = opacity.Value;
            state.placement = placement.SelectedIndex;
            state.isFrozen = freeze.Checked;
            state.isInteractable = allowInteractions.Checked;
            windowState[hwnd] = state;
            bool isMonitor = importedWindows.SelectedIndex < numMonitors;

            switch (importedWindows.SelectedIndex)
            {
                case 0:
                    syncState(ref overlays->window0, hwnd, state, op, isMonitor);
                    break;
                case 1:
                    syncState(ref overlays->window1, hwnd, state, op, isMonitor);
                    break;
                case 2:
                    syncState(ref overlays->window2, hwnd, state, op, isMonitor);
                    break;
                case 3:
                    syncState(ref overlays->window3, hwnd, state, op, isMonitor);
                    break;
            }
        }

        private void syncState(ref OverlayState overlay, IntPtr handle, WindowState state, Operation op, bool isMonitor = false)
        {
            if (op == Operation.Remove)
            {
                overlay.handle = 0;
            }
            else
            {
                overlay.opacity = (byte)state.opacity;
                overlay.placement = (byte)state.placement;
                overlay.isFrozen = state.isFrozen;
                overlay.isInteractable = state.isInteractable;

                if (op == Operation.Import)
                {
                    overlay.pose.position.x = overlay.pose.position.y = overlay.pose.position.z = float.NaN;
                    overlay.pose.orientation.x = overlay.pose.orientation.y = overlay.pose.orientation.z = overlay.pose.orientation.w = float.NaN;
                    overlay.scale = 1.0f;
                    overlay.isMinimized = false;

                    overlay.isMonitor = isMonitor;

                    // Do this last to avoid tearing.
                    overlay.handle = (ulong)handle;
                }
            }
        }

        int numMonitors = 0;
        List<IntPtr> hwndForAvailableWindow = new List<IntPtr>();
        List<IntPtr> hwndForImportedWindow = new List<IntPtr>();
        private void refresh_Tick(object sender, EventArgs e)
        {
            availableWindows.BeginUpdate();
            string lastSelected = (string)availableWindows.SelectedItem;
            int scrollOffset = availableWindows.TopIndex;
            availableWindows.Items.Clear();
            hwndForAvailableWindow.Clear();
            User32.EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, delegate (IntPtr hMonitor, IntPtr hdc, IntPtr lprcClip, IntPtr lParam)
            {
                User32.MONITORINFOEX mi = new User32.MONITORINFOEX() { Size = Marshal.SizeOf(typeof(User32.MONITORINFOEX)) };
                if (User32.GetMonitorInfo(hMonitor, ref mi))
                {
                    availableWindows.Items.Add(mi.DeviceName);
                    hwndForAvailableWindow.Add(hMonitor);
                }
                return true;
            }, IntPtr.Zero);
            numMonitors = availableWindows.Items.Count;
            availableWindows.Items.Add("---");
            hwndForAvailableWindow.Add(IntPtr.Zero);
            User32.EnumWindows(delegate (IntPtr hwnd, IntPtr param)
            {
                bool isVisible = User32.IsWindowVisible(hwnd);
                bool isEnabled = (User32.GetWindowLongPtr(hwnd, User32.GWL_STYLE) & User32.WS_DISABLED) == 0;
                bool isRoot = User32.GetAncestor(hwnd, User32.GA_ROOT) == hwnd;
                if (hwnd == IntPtr.Zero || hwnd == User32.GetShellWindow() || !isVisible || !isRoot || !isEnabled)
                {
                    return true;
                }

                string title = User32.GetWindowTextHelper(hwnd);
                if (title == "")
                {
                    return true;
                }

                if (hwndForImportedWindow.Exists(x => x == hwnd))
                {
                    return true;
                }

                availableWindows.Items.Add(title);
                hwndForAvailableWindow.Add(hwnd);
                return true;
            }, IntPtr.Zero);
            availableWindows.SelectedItem = lastSelected;
            availableWindows.TopIndex = scrollOffset;

            importedWindows.BeginUpdate();
            // TODO: Remove windows that are extinct.
            importedWindows.EndUpdate();

            availableWindows.EndUpdate();

            availableWindows_SelectedIndexChanged(null, null);
            importedWindows_SelectedIndexChanged(null, null);
        }

        private void availableWindows_SelectedIndexChanged(object sender, EventArgs e)
        {
            import.Enabled = importedWindows.Items.Count < 4 && availableWindows.SelectedItem != null && availableWindows.SelectedIndex != numMonitors;
        }

        private void import_Click(object sender, EventArgs e)
        {
            var hwnd = hwndForAvailableWindow[availableWindows.SelectedIndex];
            importedWindows.Items.Add(availableWindows.SelectedItem);
            hwndForImportedWindow.Add(hwnd);
            importedWindows.SelectedIndex = importedWindows.Items.Count - 1;

            // Set defaults.
            opacity.Value = 100;
            placement.SelectedIndex = 0;
            freeze.Checked = false;
            allowInteractions.Checked = true;
            refresh_Tick(null, null);

            pushUpdate(Operation.Import);
        }

        private void remove_Click(object sender, EventArgs e)
        {
            var index = importedWindows.SelectedIndex;
            var hwnd = hwndForImportedWindow[index];
            hwndForImportedWindow.RemoveAt(index);
            importedWindows.Items.RemoveAt(index);
            refresh_Tick(null, null);

            switch (index)
            {
                case 0:
                    overlays->window0.handle = 0;
                    break;
                case 1:
                    overlays->window1.handle = 0;
                    break;
                case 2:
                    overlays->window2.handle = 0;
                    break;
                case 3:
                    overlays->window3.handle = 0;
                    break;
            }

            availableWindows_SelectedIndexChanged(null, null);
        }

        private void importedWindows_SelectedIndexChanged(object sender, EventArgs e)
        {
            opacityLabel.Enabled = opacity.Enabled = placementLabel.Enabled = placement.Enabled =
                freeze.Enabled = allowInteractions.Enabled = remove.Enabled = importedWindows.SelectedItem != null;
            if (importedWindows.SelectedItem != null)
            {
                var hwnd = hwndForImportedWindow[importedWindows.SelectedIndex];

                WindowState state;
                if (windowState.TryGetValue(hwnd, out state))
                {
                    opacity.Value = state.opacity;
                    placement.SelectedIndex = state.placement;
                    freeze.Checked = state.isFrozen;
                    allowInteractions.Checked = state.isInteractable;
                }
            }
        }

        private void opacity_Scroll(object sender, EventArgs e)
        {
            pushUpdate(Operation.Update);
        }

        private void placement_SelectedIndexChanged(object sender, EventArgs e)
        {
            pushUpdate(Operation.Update);
        }

        private void freeze_CheckedChanged(object sender, EventArgs e)
        {
            pushUpdate(Operation.Update);
        }

        private void allowInteractions_CheckedChanged(object sender, EventArgs e)
        {
            pushUpdate(Operation.Update);
        }

        private class User32
        {
            [DllImport("user32.dll")]
            public static extern IntPtr GetShellWindow();

            [DllImport("user32.dll", CharSet = CharSet.Unicode)]
            private static extern int GetWindowText(IntPtr hWnd, StringBuilder strText, int maxCount);

            [DllImport("user32.dll", CharSet = CharSet.Unicode)]
            private static extern int GetWindowTextLength(IntPtr hWnd);

            [DllImport("user32.dll")]
            public static extern bool EnumWindows(EnumWindowsProc enumProc, IntPtr lParam);


            public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
            [DllImport("user32.dll")]
            public static extern bool EnumDisplayMonitors(IntPtr hDC, IntPtr lprcClip, EnumDisplayMonitorsProc enumProc, IntPtr lParam);

            public delegate bool EnumDisplayMonitorsProc(IntPtr hMonitor, IntPtr hdc, IntPtr lprcClip, IntPtr lParam);

            [StructLayout(LayoutKind.Sequential)]
            public struct RECT
            {
                public int Left;
                public int Top;
                public int Right;
                public int Bottom;
            }

            [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
            public struct MONITORINFOEX
            {
                public int Size;
                public RECT Monitor;
                public RECT WorkArea;
                public uint Flags;
                [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
                public string DeviceName;
            }

            [DllImport("user32.dll", CharSet = CharSet.Unicode)]
            public static extern bool GetMonitorInfo(IntPtr hMonitor, ref MONITORINFOEX lpmi);

            [DllImport("user32.dll")]
            [return: MarshalAs(UnmanagedType.Bool)]
            public static extern bool IsWindowVisible(IntPtr hWnd);

            [DllImport("user32.dll", ExactSpelling = true)]
            public static extern IntPtr GetAncestor(IntPtr hwnd, uint flags);
            public const uint GA_ROOT = 2;

            [DllImport("user32.dll", EntryPoint = "GetWindowLongPtr")]
            public static extern uint GetWindowLongPtr(IntPtr hWnd, int nIndex);
            public const int GWL_STYLE = -16;
            public const uint WS_DISABLED = 0x8000000;

            public static string GetWindowTextHelper(IntPtr hWnd)
            {
                int size = GetWindowTextLength(hWnd);
                if (size > 0)
                {
                    var builder = new StringBuilder(size + 1);
                    GetWindowText(hWnd, builder, builder.Capacity);
                    return builder.ToString();
                }

                return String.Empty;
            }
        }
    }
}
