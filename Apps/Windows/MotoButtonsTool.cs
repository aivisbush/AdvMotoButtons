// Moto Buttons Tool - portable Windows wizard for the Moto Buttons controller.
// Firmware file -> phone (USB, adb) -> controller found by the phone -> update over Bluetooth.
// Build with build.ps1 (C# 5, .NET Framework 4.8, no install needed).

using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using Microsoft.Win32;

namespace MotoButtonsTool
{
    static class Program
    {
        [DllImport("user32.dll")]
        static extern bool SetProcessDPIAware();

        [STAThread]
        static void Main(string[] args)
        {
            try { SetProcessDPIAware(); } catch (Exception) { }
            ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Theme.Init();
            // A .bin given on the command line (or via "Open with") is preselected.
            Application.Run(new WizardForm(args.Length > 0 ? args[0] : null));
        }
    }

    // Phone-side names; must match Apps/Android.
    static class Config
    {
        public const string PlatformToolsUrl = "https://dl.google.com/android/repository/platform-tools-latest-windows.zip";
        public const string DmdManageUrl = "https://free-maps.advhub.net/dmdnext/DMD_Manage_v3_06.apk";
        public const string DmdManagePackage = "com.thorkracing.wireddevices";
        public const string DmdManageService = "com.thorkracing.wireddevices/com.thorkracing.wireddevices.MyAccessibilityService";
        public const string AppPackage = "com.bush.motobuttons";
        public const int AppVersionCode = 4; // Apps/Android/app/build.gradle versionCode
        public const string AppActivity = "com.bush.motobuttons/.MainActivity";
        public const string AppApkResource = "MotoButtons.apk";
        public const string PhoneFiles = "/sdcard/Android/data/com.bush.motobuttons/files/";
        public const string ControllerName = "DMD-Remote3";
        public const int ImageMagic = 0xE9;
        public const int MaxImageSize = 0x140000;
        public const string VersionTag = "MBFWVER=";
    }

    /* =============================== theme =============================== */

    // Material 3 colour roles, seeded from the logo blue; follows the Windows light/dark setting.
    static class Theme
    {
        public static bool Dark;
        public static Color Primary, OnPrimary, PrimaryContainer, OnPrimaryContainer;
        public static Color SecondaryContainer, OnSecondaryContainer;
        public static Color Tertiary, TertiaryContainer, OnTertiaryContainer;
        public static Color Error, ErrorContainer, OnErrorContainer;
        public static Color Surface, SurfaceContainerLow, SurfaceContainer, SurfaceContainerHigh, SurfaceContainerHighest;
        public static Color OnSurface, OnSurfaceVariant, Outline, OutlineVariant;
        public static Color Success;
        public static string FontFamily = "Segoe UI";
        public static float Scale = 1f;
        public static bool LayoutScaled; // true once the form has applied DPI scaling

        // Pixel size at the current DPI (designs are in 96 dpi pixels).
        public static int S(float px) { return (int)Math.Round(px * Scale); }

        public static void Init()
        {
            using (Graphics screen = Graphics.FromHwnd(IntPtr.Zero))
                Scale = screen.DpiX / 96f;
            try
            {
                object value = Registry.GetValue(@"HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize", "AppsUseLightTheme", 1);
                Dark = value is int && (int)value == 0;
            }
            catch (Exception) { }
            using (Font probe = new Font("Segoe UI Variable Text", 10f))
            {
                if (probe.Name == "Segoe UI Variable Text")
                    FontFamily = "Segoe UI Variable Text";
            }
            if (Dark)
            {
                Primary = C(0xA3C9FE); OnPrimary = C(0x00315B); PrimaryContainer = C(0x1F4876); OnPrimaryContainer = C(0xD3E4FF);
                SecondaryContainer = C(0x3C4858); OnSecondaryContainer = C(0xD8E3F8);
                Tertiary = C(0xFFB59A); TertiaryContainer = C(0x7C2E0B); OnTertiaryContainer = C(0xFFDBCF);
                Error = C(0xFFB4AB); ErrorContainer = C(0x93000A); OnErrorContainer = C(0xFFDAD6);
                Surface = C(0x111418); SurfaceContainerLow = C(0x191C20); SurfaceContainer = C(0x1D2024);
                SurfaceContainerHigh = C(0x272A2F); SurfaceContainerHighest = C(0x32353A);
                OnSurface = C(0xE1E2E8); OnSurfaceVariant = C(0xC3C7CF); Outline = C(0x8D9199); OutlineVariant = C(0x43474E);
                Success = C(0x8BD68F);
            }
            else
            {
                Primary = C(0x3A608F); OnPrimary = C(0xFFFFFF); PrimaryContainer = C(0xD3E4FF); OnPrimaryContainer = C(0x001C38);
                SecondaryContainer = C(0xD8E3F8); OnSecondaryContainer = C(0x111C2B);
                Tertiary = C(0x9A4521); TertiaryContainer = C(0xFFDBCF); OnTertiaryContainer = C(0x380D00);
                Error = C(0xBA1A1A); ErrorContainer = C(0xFFDAD6); OnErrorContainer = C(0x410002);
                Surface = C(0xF8F9FF); SurfaceContainerLow = C(0xF2F3FA); SurfaceContainer = C(0xECEEF4);
                SurfaceContainerHigh = C(0xE6E8EE); SurfaceContainerHighest = C(0xE1E2E8);
                OnSurface = C(0x191C20); OnSurfaceVariant = C(0x43474E); Outline = C(0x73777F); OutlineVariant = C(0xC3C7CF);
                Success = C(0x2E7D32);
            }
        }

        static Color C(int rgb) { return Color.FromArgb(255, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF); }

        public static Font Font(float size, FontStyle style)
        {
            return new Font(FontFamily, size, style);
        }

        public static Color Blend(Color baseColor, Color overlay, float amount)
        {
            return Color.FromArgb(255,
                (int)(baseColor.R + (overlay.R - baseColor.R) * amount),
                (int)(baseColor.G + (overlay.G - baseColor.G) * amount),
                (int)(baseColor.B + (overlay.B - baseColor.B) * amount));
        }

        public static GraphicsPath Rounded(RectangleF r, float radius)
        {
            GraphicsPath path = new GraphicsPath();
            float d = Math.Min(radius * 2, Math.Min(r.Width, r.Height));
            if (d <= 0)
            {
                path.AddRectangle(r);
                return path;
            }
            path.AddArc(r.X, r.Y, d, d, 180, 90);
            path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            path.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            path.CloseFigure();
            return path;
        }
    }

    /* ============================== controls ============================= */

    enum ButtonKind { Filled, Tonal, Text, Outlined }

    // M3 common button: pill shape, state layers on hover/press.
    class M3Button : Control
    {
        public ButtonKind Kind;
        bool hover, pressed;

        public M3Button(string text, ButtonKind kind)
        {
            Text = text;
            Kind = kind;
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                     ControlStyles.ResizeRedraw | ControlStyles.SupportsTransparentBackColor, true);
            Font = Theme.Font(10f, FontStyle.Bold);
            Cursor = Cursors.Hand;
            TabStop = true;
            Height = 40;
            AutoSizeWidth();
        }

        public void AutoSizeWidth()
        {
            Size text = TextRenderer.MeasureText(Text, Font);
            int pad = Kind == ButtonKind.Text ? 28 : 48;
            // Before the form scales its layout, sizes are in 96 dpi units.
            Width = Theme.LayoutScaled ? text.Width + Theme.S(pad) : (int)(text.Width / Theme.Scale) + pad;
        }

        protected override void OnTextChanged(EventArgs e) { base.OnTextChanged(e); Invalidate(); }
        protected override void OnEnabledChanged(EventArgs e) { base.OnEnabledChanged(e); Invalidate(); }
        protected override void OnGotFocus(EventArgs e) { base.OnGotFocus(e); Invalidate(); }
        protected override void OnLostFocus(EventArgs e) { base.OnLostFocus(e); Invalidate(); }

        protected override bool IsInputKey(Keys keyData)
        {
            return keyData == Keys.Enter || keyData == Keys.Space || base.IsInputKey(keyData);
        }

        protected override void OnKeyUp(KeyEventArgs e)
        {
            base.OnKeyUp(e);
            if (e.KeyCode == Keys.Enter || e.KeyCode == Keys.Space)
                OnClick(EventArgs.Empty);
        }

        // Screen readers and UI Automation see a push button they can press.
        protected override AccessibleObject CreateAccessibilityInstance()
        {
            return new ButtonAccessible(this);
        }

        public void PerformClick()
        {
            if (Enabled && Visible)
                OnClick(EventArgs.Empty);
        }

        class ButtonAccessible : ControlAccessibleObject
        {
            readonly M3Button owner;
            public ButtonAccessible(M3Button owner) : base(owner) { this.owner = owner; }
            public override AccessibleRole Role { get { return AccessibleRole.PushButton; } }
            public override string DefaultAction { get { return "Press"; } }
            public override void DoDefaultAction() { owner.PerformClick(); }
        }
        protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
        protected override void OnMouseLeave(EventArgs e) { hover = false; pressed = false; Invalidate(); base.OnMouseLeave(e); }
        protected override void OnMouseDown(MouseEventArgs e) { pressed = true; Invalidate(); base.OnMouseDown(e); }
        protected override void OnMouseUp(MouseEventArgs e) { pressed = false; Invalidate(); base.OnMouseUp(e); }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            Color parentBack = Parent != null ? Parent.BackColor : Theme.Surface;
            g.Clear(parentBack);
            g.SmoothingMode = SmoothingMode.AntiAlias;

            Color container, content;
            switch (Kind)
            {
                case ButtonKind.Filled: container = Theme.Primary; content = Theme.OnPrimary; break;
                case ButtonKind.Tonal: container = Theme.SecondaryContainer; content = Theme.OnSecondaryContainer; break;
                default: container = parentBack; content = Theme.Primary; break;
            }
            if (!Enabled)
            {
                container = Kind == ButtonKind.Filled || Kind == ButtonKind.Tonal ? Theme.Blend(parentBack, Theme.OnSurface, 0.12f) : parentBack;
                content = Theme.Blend(parentBack, Theme.OnSurface, 0.38f);
            }
            else if (pressed || hover || Focused)
            {
                container = Theme.Blend(container, content, pressed ? 0.12f : 0.08f);
            }

            RectangleF r = new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f);
            using (GraphicsPath path = Theme.Rounded(r, Height / 2f))
            {
                using (SolidBrush brush = new SolidBrush(container))
                    g.FillPath(brush, path);
                if (Kind == ButtonKind.Outlined)
                    using (Pen pen = new Pen(Enabled ? Theme.Outline : Theme.Blend(parentBack, Theme.OnSurface, 0.12f)))
                        g.DrawPath(pen, path);
            }
            TextRenderer.DrawText(g, Text, Font, ClientRectangle, content, container,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
        }
    }

    // Rounded surface; children sit on its colour.
    class Card : Panel
    {
        public float Radius = Theme.S(20);
        public Color Fill;

        public Card(Color fill)
        {
            Fill = fill;
            BackColor = fill;
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.Clear(Parent != null ? Parent.BackColor : Theme.Surface);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            using (GraphicsPath path = Theme.Rounded(new RectangleF(0, 0, Width - 1, Height - 1), Radius))
            using (SolidBrush brush = new SolidBrush(Fill))
                g.FillPath(brush, path);
        }
    }

    // Numbered steps joined by a line; done steps show a check.
    class Stepper : Control
    {
        readonly string[] steps;
        int current;

        public Stepper(string[] steps)
        {
            this.steps = steps;
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            Height = 64;
            Font = Theme.Font(9f, FontStyle.Regular);
        }

        public int Current { get { return current; } set { current = value; Invalidate(); } }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.Clear(Parent != null ? Parent.BackColor : Theme.Surface);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            int n = steps.Length;
            float slot = Width / (float)n;
            float dot = Theme.S(28);
            using (Font bold = Theme.Font(9f, FontStyle.Bold))
            {
                for (int i = 0; i < n; i++)
                {
                    float cx = slot * i + slot / 2;
                    if (i < n - 1)
                        using (Pen line = new Pen(i < current ? Theme.Primary : Theme.OutlineVariant, 2f))
                            g.DrawLine(line, cx + dot / 2 + Theme.S(6), dot / 2, cx + slot - dot / 2 - Theme.S(6), dot / 2);

                    RectangleF circle = new RectangleF(cx - dot / 2, 0, dot, dot);
                    bool done = i < current, active = i == current;
                    Color fill = done ? Theme.PrimaryContainer : active ? Theme.Primary : Theme.SurfaceContainerHighest;
                    Color ink = done ? Theme.OnPrimaryContainer : active ? Theme.OnPrimary : Theme.OnSurfaceVariant;
                    using (SolidBrush brush = new SolidBrush(fill))
                        g.FillEllipse(brush, circle);
                    string mark = done ? "✓" : (i + 1).ToString();
                    TextRenderer.DrawText(g, mark, bold, Rectangle.Round(circle), ink, fill,
                        TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
                    Rectangle label = new Rectangle((int)(cx - slot / 2), Theme.S(34), (int)slot, Theme.S(24));
                    TextRenderer.DrawText(g, steps[i], active ? bold : Font, label, active ? Theme.OnSurface : Theme.OnSurfaceVariant,
                        TextFormatFlags.HorizontalCenter | TextFormatFlags.Top);
                }
            }
        }
    }

    // M3 linear progress: rounded track, determinate or animated indeterminate.
    class LinearProgress : Control
    {
        int value;
        bool indeterminate;
        readonly Timer timer = new Timer();
        float phase;

        public LinearProgress()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            Height = 6;
            timer.Interval = 16;
            timer.Tick += delegate { phase = (phase + 0.012f) % 1.6f; Invalidate(); };
        }

        public int Value { get { return value; } set { this.value = Math.Max(0, Math.Min(100, value)); Invalidate(); } }

        public bool Indeterminate
        {
            get { return indeterminate; }
            set { indeterminate = value; timer.Enabled = value && Visible; Invalidate(); }
        }

        protected override void OnVisibleChanged(EventArgs e) { base.OnVisibleChanged(e); timer.Enabled = indeterminate && Visible; }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.Clear(Parent != null ? Parent.BackColor : Theme.Surface);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            float h = Height - 1;
            using (SolidBrush track = new SolidBrush(Theme.SecondaryContainer))
            using (GraphicsPath path = Theme.Rounded(new RectangleF(0, 0, Width - 1, h), h / 2))
                g.FillPath(track, path);
            float start, width;
            if (indeterminate)
            {
                start = (phase - 0.4f) * Width;
                width = Width * 0.4f;
            }
            else
            {
                start = 0;
                width = (Width - 1) * value / 100f;
            }
            float left = Math.Max(0, start), right = Math.Min(Width - 1, start + width);
            if (right - left < 1)
                return;
            using (SolidBrush bar = new SolidBrush(Theme.Primary))
            using (GraphicsPath path = Theme.Rounded(new RectangleF(left, 0, right - left, h), h / 2))
                g.FillPath(bar, path);
        }
    }

    // One row in a selectable list.
    class ListRow
    {
        public string Title, Subtitle;
        public object Tag;
        public ListRow(string title, string subtitle, object tag) { Title = title; Subtitle = subtitle; Tag = tag; }
        public override string ToString() { return Title; }
    }

    // M3 list: two-line rows, selected row on a secondary container.
    class M3List : ListBox
    {
        public M3List(Color back)
        {
            DrawMode = DrawMode.OwnerDrawFixed;
            ItemHeight = Theme.S(60);
            BorderStyle = BorderStyle.None;
            BackColor = back;
            IntegralHeight = false;
            Font = Theme.Font(10.5f, FontStyle.Regular);
        }

        protected override void OnDrawItem(DrawItemEventArgs e)
        {
            if (e.Index < 0)
                return;
            ListRow row = (ListRow)Items[e.Index];
            Graphics g = e.Graphics;
            g.SmoothingMode = SmoothingMode.AntiAlias;
            using (SolidBrush back = new SolidBrush(BackColor))
                g.FillRectangle(back, e.Bounds);
            bool selected = (e.State & DrawItemState.Selected) != 0;
            Color fill = selected ? Theme.SecondaryContainer : BackColor;
            if (selected)
            {
                RectangleF r = new RectangleF(e.Bounds.X + 2, e.Bounds.Y + Theme.S(3), e.Bounds.Width - 5, e.Bounds.Height - Theme.S(6));
                using (GraphicsPath path = Theme.Rounded(r, Theme.S(14)))
                using (SolidBrush brush = new SolidBrush(fill))
                    g.FillPath(brush, path);
            }
            Rectangle title = new Rectangle(e.Bounds.X + Theme.S(16), e.Bounds.Y + Theme.S(9), e.Bounds.Width - Theme.S(24), Theme.S(24));
            Rectangle sub = new Rectangle(e.Bounds.X + Theme.S(16), e.Bounds.Y + Theme.S(32), e.Bounds.Width - Theme.S(24), Theme.S(20));
            TextRenderer.DrawText(g, row.Title, Font, title, selected ? Theme.OnSecondaryContainer : Theme.OnSurface, fill,
                TextFormatFlags.Left | TextFormatFlags.EndEllipsis);
            using (Font small = Theme.Font(9f, FontStyle.Regular))
                TextRenderer.DrawText(g, row.Subtitle, small, sub, Theme.OnSurfaceVariant, fill, TextFormatFlags.Left | TextFormatFlags.EndEllipsis);
        }
    }

    // Dashed drop target for the firmware file.
    class DropZone : Control
    {
        public string Headline = "Drop the firmware .bin here";
        public string Detail = "or use Browse";
        bool dragging;

        public DropZone()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            AllowDrop = true;
            Height = 120;
        }

        public bool Dragging { set { dragging = value; Invalidate(); } }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            Color back = Parent != null ? Parent.BackColor : Theme.Surface;
            g.Clear(back);
            g.SmoothingMode = SmoothingMode.AntiAlias;
            Color fill = dragging ? Theme.PrimaryContainer : back;
            using (GraphicsPath path = Theme.Rounded(new RectangleF(1, 1, Width - 3, Height - 3), Theme.S(16)))
            {
                using (SolidBrush brush = new SolidBrush(fill))
                    g.FillPath(brush, path);
                using (Pen pen = new Pen(dragging ? Theme.Primary : Theme.Outline, 1.5f))
                {
                    pen.DashStyle = DashStyle.Dash;
                    g.DrawPath(pen, path);
                }
            }
            using (Font big = Theme.Font(12f, FontStyle.Bold))
            using (Font small = Theme.Font(9.5f, FontStyle.Regular))
            {
                TextRenderer.DrawText(g, Headline, big, new Rectangle(0, Height / 2 - Theme.S(28), Width, Theme.S(28)), Theme.OnSurface, fill, TextFormatFlags.HorizontalCenter | TextFormatFlags.EndEllipsis);
                TextRenderer.DrawText(g, Detail, small, new Rectangle(0, Height / 2 + Theme.S(4), Width, Theme.S(24)), Theme.OnSurfaceVariant, fill, TextFormatFlags.HorizontalCenter);
            }
        }
    }

    /* ================================ adb ================================ */

    class CommandResult
    {
        public int ExitCode;
        public string Output;
        public bool Ok { get { return ExitCode == 0; } }
    }

    class PhoneInfo
    {
        public string Serial, Model, State;
    }

    // Downloads, caches and runs Google's adb.
    class Adb
    {
        readonly Action<string> log;
        readonly string dataDir;
        string adbPath;
        public string Serial;

        public Adb(Action<string> log)
        {
            this.log = log;
            dataDir = PickDataDir();
        }

        public string DataDir { get { return dataDir; } }
        public bool Ready { get { return adbPath != null; } }

        // Portable: next to the exe when writable, else in the user profile.
        static string PickDataDir()
        {
            string exeDir = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
            string local = Path.Combine(exeDir, "MotoButtonsTool_data");
            try
            {
                Directory.CreateDirectory(local);
                string probe = Path.Combine(local, ".write_test");
                File.WriteAllText(probe, "ok");
                File.Delete(probe);
                return local;
            }
            catch (Exception)
            {
                string fallback = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "MotoButtonsTool");
                Directory.CreateDirectory(fallback);
                return fallback;
            }
        }

        public void EnsureAdb()
        {
            if (adbPath != null)
                return;
            string candidate = Path.Combine(dataDir, "platform-tools", "adb.exe");
            if (!File.Exists(candidate))
            {
                string zip = Path.Combine(dataDir, "platform-tools.zip");
                log("Downloading Android platform tools (adb) from Google: " + Config.PlatformToolsUrl);
                Download(Config.PlatformToolsUrl, zip);
                string target = Path.Combine(dataDir, "platform-tools");
                if (Directory.Exists(target))
                    Directory.Delete(target, true);
                ZipFile.ExtractToDirectory(zip, dataDir);
                File.Delete(zip);
            }
            if (!File.Exists(candidate))
                throw new Exception("adb.exe not found after unpacking the platform tools.");
            adbPath = candidate;
            RunGlobal("start-server", 30000);
        }

        public void Download(string url, string file)
        {
            string partial = file + ".part";
            using (WebClient client = new WebClient())
            {
                client.Headers.Add("User-Agent", "MotoButtonsTool");
                client.DownloadFile(url, partial);
            }
            if (File.Exists(file))
                File.Delete(file);
            File.Move(partial, file);
        }

        public CommandResult RunGlobal(string arguments, int timeoutMs)
        {
            return Exec(arguments, timeoutMs);
        }

        public CommandResult Run(string arguments, int timeoutMs)
        {
            return Exec("-s " + Serial + " " + arguments, timeoutMs);
        }

        public string Shell(string command)
        {
            return Run("shell " + command, 60000).Output;
        }

        CommandResult Exec(string arguments, int timeoutMs)
        {
            ProcessStartInfo info = new ProcessStartInfo(adbPath, arguments);
            info.UseShellExecute = false;
            info.RedirectStandardOutput = true;
            info.RedirectStandardError = true;
            info.CreateNoWindow = true;
            info.StandardOutputEncoding = Encoding.UTF8;
            using (Process process = Process.Start(info))
            {
                Task<string> stdout = process.StandardOutput.ReadToEndAsync();
                Task<string> stderr = process.StandardError.ReadToEndAsync();
                if (!process.WaitForExit(timeoutMs))
                {
                    try { process.Kill(); } catch (Exception) { }
                    throw new Exception("adb " + arguments + " timed out.");
                }
                CommandResult result = new CommandResult();
                result.ExitCode = process.ExitCode;
                result.Output = (stdout.Result + stderr.Result).Trim();
                if (!arguments.EndsWith("devices -l"))
                    log("adb " + arguments + "\r\n    " + result.Output.Replace("\n", "\n    "));
                return result;
            }
        }

        public List<PhoneInfo> ListPhones()
        {
            List<PhoneInfo> phones = new List<PhoneInfo>();
            foreach (string raw in Exec("devices -l", 15000).Output.Split('\n'))
            {
                string line = raw.Trim();
                if (line.Length == 0 || line.StartsWith("List of devices") || line.StartsWith("*"))
                    continue;
                string[] parts = line.Split(new char[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length < 2)
                    continue;
                PhoneInfo phone = new PhoneInfo();
                phone.Serial = parts[0];
                phone.State = parts[1];
                phone.Model = "Android phone";
                foreach (string part in parts)
                    if (part.StartsWith("model:"))
                        phone.Model = part.Substring(6).Replace('_', ' ');
                phones.Add(phone);
            }
            return phones;
        }
    }

    /* ============================== firmware ============================= */

    class FirmwareInfo
    {
        public string Path;
        public byte[] Bytes;
        public string Version;

        // Throws with a user-facing message when the file is unusable.
        public static FirmwareInfo Load(string path)
        {
            byte[] bytes = File.ReadAllBytes(path);
            string name = System.IO.Path.GetFileName(path);
            if (bytes.Length == 0 || bytes[0] != Config.ImageMagic)
                throw new Exception(name + " is not a Moto Buttons firmware file.");
            if (bytes.Length > Config.MaxImageSize)
                throw new Exception(name + " is too big. Use the app .bin, not the merged one.");
            FirmwareInfo info = new FirmwareInfo();
            info.Path = path;
            info.Bytes = bytes;
            info.Version = FindVersion(bytes);
            return info;
        }

        static string FindVersion(byte[] bytes)
        {
            byte[] tag = Encoding.ASCII.GetBytes(Config.VersionTag);
            for (int i = 0; i + tag.Length < bytes.Length; i++)
            {
                int j = 0;
                while (j < tag.Length && bytes[i + j] == tag[j])
                    j++;
                if (j < tag.Length)
                    continue;
                int start = i + tag.Length, end = start;
                while (end < bytes.Length && end - start < 16 && (char.IsLetterOrDigit((char)bytes[end]) || bytes[end] == '.' || bytes[end] == '-'))
                    end++;
                return end > start ? Encoding.ASCII.GetString(bytes, start, end - start) : null;
            }
            return null;
        }

        // Negative when a is older than b; dotted numbers (2.2.10 > 2.2.9),
        // a pre-release older than its release (2.4.1-beta.2 < 2.4.1).
        public static int Compare(string a, string b)
        {
            string[] sa = a.Split(new char[] { '-' }, 2), sb = b.Split(new char[] { '-' }, 2);
            int core = CompareDotted(sa[0], sb[0]);
            if (core != 0)
                return core;
            if (sa.Length == 1 || sb.Length == 1)
                return sb.Length.CompareTo(sa.Length);
            return CompareDotted(DigitsAndDots(sa[1]), DigitsAndDots(sb[1]));
        }

        static string DigitsAndDots(string s)
        {
            StringBuilder b = new StringBuilder();
            foreach (char c in s)
                if (char.IsDigit(c) || c == '.')
                    b.Append(c);
            return b.ToString();
        }

        static int CompareDotted(string a, string b)
        {
            string[] pa = a.Split('.'), pb = b.Split('.');
            for (int i = 0; i < Math.Max(pa.Length, pb.Length); i++)
            {
                int va = i < pa.Length ? Number(pa[i]) : 0, vb = i < pb.Length ? Number(pb[i]) : 0;
                if (va != vb)
                    return va.CompareTo(vb);
            }
            return 0;
        }

        static int Number(string part)
        {
            int value = 0;
            foreach (char c in part)
            {
                if (!char.IsDigit(c))
                    break;
                value = value * 10 + (c - '0');
            }
            return value;
        }
    }

    class ControllerInfo
    {
        public string Name, Address, Version;
    }

    /* =============================== wizard ============================== */

    class WizardForm : Form
    {
        const int PageFirmware = 0, PagePhone = 1, PageController = 2, PageUpdate = 3;

        readonly Adb adb;
        readonly JavaScriptSerializer json = new JavaScriptSerializer();
        int seq = Environment.TickCount % 100000;
        int page;
        bool busy;
        bool updateDone;

        FirmwareInfo firmware;
        ControllerInfo controller;

        // chrome
        Stepper stepper;
        Card card;
        Panel[] pages;
        M3Button backButton, nextButton;
        TextBox logBox;
        M3Button detailsToggle;
        Panel detailsPanel;

        // page 1
        DropZone dropZone;
        Label fileTitle, fileDetail;
        // page 2
        M3List phoneList;
        Label phoneHint;
        CheckBox dmdCheck;
        LinearProgress phoneProgress;
        Label phoneStatus;
        Timer phoneTimer;
        bool phonePolling;
        // page 3
        M3List controllerList;
        LinearProgress scanProgress;
        Label scanStatus;
        Panel pairHelp;
        // page 4
        Label compareTitle, compareDetail;
        LinearProgress updateProgress;
        Label updateStatus;
        M3Button updateButton, reinstallButton;

        public WizardForm(string initialFirmware)
        {
            // Layout below is in 96 dpi pixels; WinForms scales it to the screen on ResumeLayout.
            SuspendLayout();
            AutoScaleDimensions = new SizeF(96F, 96F);
            AutoScaleMode = AutoScaleMode.Dpi;
            Text = "Moto Buttons";
            ClientSize = new Size(800, 620);
            MinimumSize = new Size(700, 580);
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Theme.Surface;
            ForeColor = Theme.OnSurface;
            Font = Theme.Font(10f, FontStyle.Regular);
            try { Icon = Icon.ExtractAssociatedIcon(Assembly.GetExecutingAssembly().Location); } catch (Exception) { }

            adb = new Adb(Log);
            BuildChrome();
            BuildFirmwarePage();
            BuildPhonePage();
            BuildControllerPage();
            BuildUpdatePage();
            ResumeLayout(false);
            PerformLayout();
            Theme.LayoutScaled = true;
            GoTo(PageFirmware);
            Log("Moto Buttons Tool. Data folder: " + adb.DataDir);
            if (initialFirmware != null && File.Exists(initialFirmware))
                SelectFirmware(initialFirmware);
        }

        /* ------------------------------ chrome ------------------------------ */

        void BuildChrome()
        {
            // Header: title and stepper.
            Panel header = new Panel();
            header.Dock = DockStyle.Top;
            header.Size = new Size(ClientSize.Width, 140);
            header.BackColor = Theme.Surface;
            header.Padding = new Padding(28, 20, 28, 0);
            Label title = MakeLabel("Moto Buttons", 18f, FontStyle.Bold, Theme.OnSurface);
            title.Location = new Point(26, 12);
            Label subtitle = MakeLabel("Firmware update", 10.5f, FontStyle.Regular, Theme.OnSurfaceVariant);
            subtitle.Location = new Point(29, 48);
            stepper = new Stepper(new string[] { "Firmware", "Phone", "Controller", "Update" });
            stepper.Location = new Point(28, 76);
            stepper.Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top;
            stepper.Width = ClientSize.Width - 56;
            header.Controls.Add(title);
            header.Controls.Add(subtitle);
            header.Controls.Add(stepper);

            // Details: collapsible log.
            detailsPanel = new Panel();
            detailsPanel.Dock = DockStyle.Bottom;
            detailsPanel.Size = new Size(ClientSize.Width, 44);
            detailsPanel.BackColor = Theme.SurfaceContainer;
            detailsToggle = new M3Button("▸  Details", ButtonKind.Text);
            detailsToggle.Font = Theme.Font(9.5f, FontStyle.Regular);
            detailsToggle.AutoSizeWidth();
            detailsToggle.Location = new Point(16, 2);
            detailsToggle.Click += delegate { ToggleDetails(); };
            logBox = new TextBox();
            logBox.Multiline = true;
            logBox.ReadOnly = true;
            logBox.ScrollBars = ScrollBars.Vertical;
            logBox.BorderStyle = BorderStyle.None;
            logBox.BackColor = Theme.SurfaceContainer;
            logBox.ForeColor = Theme.OnSurfaceVariant;
            logBox.Font = new Font("Consolas", 9f);
            logBox.Location = new Point(28, 44);
            logBox.Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top | AnchorStyles.Bottom;
            logBox.Size = new Size(ClientSize.Width - 56, 150);
            logBox.Visible = false;
            detailsPanel.Controls.Add(detailsToggle);
            detailsPanel.Controls.Add(logBox);

            // Footer: navigation.
            Panel footer = new Panel();
            footer.Dock = DockStyle.Bottom;
            footer.Size = new Size(ClientSize.Width, 72);
            footer.BackColor = Theme.Surface;
            backButton = new M3Button("Back", ButtonKind.Text);
            backButton.Location = new Point(24, 16);
            backButton.Click += delegate { if (!busy && page > 0) GoTo(page - 1); };
            nextButton = new M3Button("Next", ButtonKind.Filled);
            nextButton.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            nextButton.Width = 140;
            nextButton.Location = new Point(ClientSize.Width - 28 - nextButton.Width, 16);
            nextButton.Click += delegate { OnNext(); };
            footer.Controls.Add(backButton);
            footer.Controls.Add(nextButton);

            // Content card holding the pages.
            Panel content = new Panel();
            content.Dock = DockStyle.Fill;
            content.Size = new Size(ClientSize.Width, 380);
            content.BackColor = Theme.Surface;
            content.Padding = new Padding(28, 4, 28, 4);
            card = new Card(Theme.SurfaceContainerLow);
            card.Dock = DockStyle.Fill;
            card.Size = new Size(ClientSize.Width - 56, 380);
            card.Padding = new Padding(28, 24, 28, 24);
            content.Controls.Add(card);

            Controls.Add(content);
            Controls.Add(footer);
            Controls.Add(detailsPanel);
            Controls.Add(header);
            pages = new Panel[4];
        }

        void ToggleDetails()
        {
            bool open = !logBox.Visible;
            logBox.Visible = open;
            detailsPanel.Height = open ? 210 : 44;
            detailsToggle.Text = (open ? "▾" : "▸") + "  Details";
            detailsToggle.AutoSizeWidth();
        }

        Panel NewPage(int index, string title, string subtitle)
        {
            Panel p = new Panel();
            p.Dock = DockStyle.Fill;
            p.Size = new Size(700, 340); // anchored children below are laid out for this width
            p.BackColor = card.Fill;
            p.Visible = false;
            Label t = MakeLabel(title, 16f, FontStyle.Bold, Theme.OnSurface);
            t.Location = new Point(0, 0);
            Label s = MakeLabel(subtitle, 10f, FontStyle.Regular, Theme.OnSurfaceVariant);
            s.Location = new Point(0, 36);
            s.MaximumSize = new Size(700, 0);
            p.Controls.Add(t);
            p.Controls.Add(s);
            card.Controls.Add(p);
            pages[index] = p;
            return p;
        }

        static Label MakeLabel(string text, float size, FontStyle style, Color color)
        {
            Label label = new Label();
            label.Text = text;
            label.AutoSize = true;
            label.Font = Theme.Font(size, style);
            label.ForeColor = color;
            label.BackColor = Color.Transparent;
            return label;
        }

        void Log(string message)
        {
            if (InvokeRequired)
            {
                BeginInvoke(new Action<string>(Log), message);
                return;
            }
            string line = DateTime.Now.ToString("HH:mm:ss") + "  " + message + Environment.NewLine;
            logBox.AppendText(line);
            try { File.AppendAllText(Path.Combine(adb.DataDir, "MotoButtonsTool.log"), DateTime.Now.ToString("yyyy-MM-dd ") + line); }
            catch (Exception) { }
        }

        void Ui(Action action)
        {
            if (InvokeRequired)
                Invoke(action);
            else
                action();
        }

        /* ----------------------------- navigation --------------------------- */

        void GoTo(int target)
        {
            page = target;
            for (int i = 0; i < pages.Length; i++)
                pages[i].Visible = i == target;
            stepper.Current = target;
            backButton.Visible = target > 0 && !updateDone;
            nextButton.Kind = ButtonKind.Filled;
            nextButton.Text = target == PagePhone ? "Continue" : target == PageUpdate ? "Close" : "Next";
            nextButton.Visible = target != PageUpdate || updateDone;
            if (phoneTimer != null)
                phoneTimer.Enabled = target == PagePhone;
            UpdateNextEnabled();

            if (target == PagePhone)
                RefreshPhones();
            else if (target == PageController)
                StartScan();
            else if (target == PageUpdate)
                ShowComparison();
        }

        void UpdateNextEnabled()
        {
            bool ok;
            switch (page)
            {
                case PageFirmware: ok = firmware != null; break;
                case PagePhone: ok = SelectedPhone() != null; break;
                case PageController: ok = controllerList.SelectedItem != null; break;
                default: ok = true; break;
            }
            nextButton.Enabled = ok && !busy;
            backButton.Enabled = !busy;
        }

        void OnNext()
        {
            if (busy)
                return;
            switch (page)
            {
                case PageFirmware: GoTo(PagePhone); break;
                case PagePhone: PreparePhone(); break;
                case PageController:
                    controller = (ControllerInfo)((ListRow)controllerList.SelectedItem).Tag;
                    GoTo(PageUpdate);
                    break;
                default: Close(); break;
            }
        }

        async void RunBusy(Func<Task> work)
        {
            busy = true;
            UpdateNextEnabled();
            try
            {
                await work();
            }
            catch (Exception e)
            {
                Log("ERROR: " + e.Message);
                MessageBox.Show(this, e.Message, "Moto Buttons", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
            busy = false;
            UpdateNextEnabled();
        }

        /* --------------------------- page 1: file --------------------------- */

        void BuildFirmwarePage()
        {
            Panel p = NewPage(PageFirmware, "Choose the firmware", "Select the controller firmware (.bin) you want to install.");
            dropZone = new DropZone();
            dropZone.Location = new Point(0, 84);
            dropZone.Width = 700;
            dropZone.Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top;
            dropZone.Cursor = Cursors.Hand;
            dropZone.Click += delegate { Browse(); };
            dropZone.DragEnter += delegate(object s, DragEventArgs e)
            {
                if (e.Data.GetDataPresent(DataFormats.FileDrop)) { e.Effect = DragDropEffects.Copy; dropZone.Dragging = true; }
            };
            dropZone.DragLeave += delegate { dropZone.Dragging = false; };
            dropZone.DragDrop += delegate(object s, DragEventArgs e)
            {
                dropZone.Dragging = false;
                string[] files = (string[])e.Data.GetData(DataFormats.FileDrop);
                if (files != null && files.Length > 0)
                    SelectFirmware(files[0]);
            };
            dropZone.Height = 104;
            M3Button browse = new M3Button("Browse...", ButtonKind.Tonal);
            browse.Location = new Point(0, 200);
            browse.Click += delegate { Browse(); };
            fileTitle = MakeLabel("", 12f, FontStyle.Bold, Theme.OnSurface);
            fileTitle.Location = new Point(0, 252);
            fileDetail = MakeLabel("", 10f, FontStyle.Regular, Theme.OnSurfaceVariant);
            fileDetail.Location = new Point(0, 278);
            fileDetail.MaximumSize = new Size(700, 0);
            p.Controls.Add(dropZone);
            p.Controls.Add(browse);
            p.Controls.Add(fileTitle);
            p.Controls.Add(fileDetail);
            p.Resize += delegate { dropZone.Width = p.Width; };
        }

        void Browse()
        {
            using (OpenFileDialog dialog = new OpenFileDialog())
            {
                dialog.Title = "Choose controller firmware";
                dialog.Filter = "Firmware (*.bin)|*.bin|All files (*.*)|*.*";
                if (dialog.ShowDialog(this) == DialogResult.OK)
                    SelectFirmware(dialog.FileName);
            }
        }

        void SelectFirmware(string path)
        {
            try
            {
                firmware = FirmwareInfo.Load(path);
                fileTitle.ForeColor = Theme.OnSurface;
                fileTitle.Text = firmware.Version != null ? "Version " + firmware.Version : "Version unknown";
                fileDetail.Text = Path.GetFileName(path) + "  ·  " + (firmware.Bytes.Length / 1024) + " KB";
                dropZone.Headline = Path.GetFileName(path);
                dropZone.Detail = "Drop another file to replace it";
                dropZone.Invalidate();
                Log("Firmware " + path + ", version " + (firmware.Version ?? "unknown") + ", " + firmware.Bytes.Length + " bytes");
            }
            catch (Exception e)
            {
                firmware = null;
                fileTitle.ForeColor = Theme.Error;
                fileTitle.Text = "Not a firmware file";
                fileDetail.Text = e.Message;
            }
            UpdateNextEnabled();
        }

        /* --------------------------- page 2: phone -------------------------- */

        void BuildPhonePage()
        {
            Panel p = NewPage(PagePhone, "Connect your phone",
                "Plug the phone in by USB with USB debugging on, then allow this computer on the phone. " +
                "(Settings > About phone > Software information: tap Build number 7 times; then Developer options > USB debugging.)");
            phoneList = new M3List(card.Fill);
            phoneList.Location = new Point(0, 96);
            phoneList.Size = new Size(700, 70);
            phoneList.Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top;
            phoneList.SelectedIndexChanged += delegate { UpdateNextEnabled(); };
            phoneHint = MakeLabel("Looking for phones...", 10f, FontStyle.Regular, Theme.OnSurfaceVariant);
            phoneHint.Location = new Point(0, 172);
            dmdCheck = new CheckBox();
            dmdCheck.Text = "Also set up DMD2 support on this phone (installs DMD Manage from THORK Racing)";
            dmdCheck.AutoSize = true;
            dmdCheck.Font = Theme.Font(10f, FontStyle.Regular);
            dmdCheck.ForeColor = Theme.OnSurface;
            dmdCheck.BackColor = card.Fill;
            dmdCheck.Location = new Point(0, 204);
            phoneProgress = new LinearProgress();
            phoneProgress.Location = new Point(0, 244);
            phoneProgress.Width = 700;
            phoneProgress.Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top;
            phoneProgress.Visible = false;
            phoneStatus = MakeLabel("", 10f, FontStyle.Regular, Theme.OnSurfaceVariant);
            phoneStatus.Location = new Point(0, 258);
            p.Controls.Add(phoneList);
            p.Controls.Add(phoneHint);
            p.Controls.Add(dmdCheck);
            p.Controls.Add(phoneProgress);
            p.Controls.Add(phoneStatus);
            p.Resize += delegate { phoneList.Width = p.Width; phoneProgress.Width = p.Width; };

            phoneTimer = new Timer();
            phoneTimer.Interval = 2500;
            phoneTimer.Tick += delegate { RefreshPhones(); };
        }

        PhoneInfo SelectedPhone()
        {
            ListRow row = phoneList.SelectedItem as ListRow;
            PhoneInfo phone = row == null ? null : (PhoneInfo)row.Tag;
            return phone != null && phone.State == "device" ? phone : null;
        }

        async void RefreshPhones()
        {
            if (phonePolling || busy)
                return;
            phonePolling = true;
            try
            {
                if (!adb.Ready)
                {
                    phoneHint.Text = "Preparing the Android tools (first run downloads them from Google)...";
                    await Task.Run(new Action(adb.EnsureAdb));
                }
                List<PhoneInfo> phones = await Task.Run(new Func<List<PhoneInfo>>(adb.ListPhones));
                string selected = SelectedPhone() != null ? SelectedPhone().Serial : null;
                phoneList.BeginUpdate();
                phoneList.Items.Clear();
                foreach (PhoneInfo phone in phones)
                {
                    string sub = phone.State == "device" ? "Connected  ·  " + phone.Serial
                        : phone.State == "unauthorized" ? "Allow USB debugging on the phone"
                        : phone.State;
                    phoneList.Items.Add(new ListRow(phone.Model, sub, phone));
                }
                for (int i = 0; i < phoneList.Items.Count; i++)
                {
                    PhoneInfo phone = (PhoneInfo)((ListRow)phoneList.Items[i]).Tag;
                    if (phone.Serial == selected || (selected == null && phone.State == "device"))
                    {
                        phoneList.SelectedIndex = i;
                        break;
                    }
                }
                phoneList.EndUpdate();
                phoneHint.Text = phones.Count == 0 ? "No phone found yet. Connect it by USB and switch on USB debugging."
                    : SelectedPhone() == null ? "Unlock the phone and tap Allow on the USB debugging prompt." : "";
            }
            catch (Exception e)
            {
                phoneHint.Text = e.Message;
                Log("ERROR: " + e.Message);
            }
            phonePolling = false;
            UpdateNextEnabled();
        }

        void PreparePhone()
        {
            PhoneInfo phone = SelectedPhone();
            if (phone == null)
                return;
            adb.Serial = phone.Serial;
            bool withDmd = dmdCheck.Checked;
            phoneTimer.Enabled = false;
            RunBusy(async delegate
            {
                phoneProgress.Visible = true;
                phoneProgress.Indeterminate = true;
                try
                {
                    await Task.Run(delegate { PreparePhoneWork(phone, withDmd); });
                }
                finally
                {
                    phoneProgress.Visible = false;
                    phoneStatus.Text = "";
                }
                GoTo(PageController);
            });
        }

        void Status(Label label, string text)
        {
            Ui(delegate { label.Text = text; });
            Log(text);
        }

        void PreparePhoneWork(PhoneInfo phone, bool withDmd)
        {
            Status(phoneStatus, "Checking the Moto Buttons app on " + phone.Model + "...");
            string package = adb.Shell("dumpsys package " + Config.AppPackage + " | grep versionCode");
            int installed = 0;
            int at = package.IndexOf("versionCode=");
            if (at >= 0)
            {
                int end = at + 12;
                while (end < package.Length && char.IsDigit(package[end]))
                    end++;
                int.TryParse(package.Substring(at + 12, end - at - 12), out installed);
            }
            if (installed < Config.AppVersionCode)
            {
                Status(phoneStatus, installed == 0 ? "Installing the Moto Buttons app..." : "Updating the Moto Buttons app...");
                string apk = Path.Combine(adb.DataDir, Config.AppApkResource);
                using (Stream resource = Assembly.GetExecutingAssembly().GetManifestResourceStream(Config.AppApkResource))
                {
                    if (resource == null)
                        throw new Exception("This tool was built without the Moto Buttons app inside.");
                    using (FileStream file = File.Create(apk))
                        resource.CopyTo(file);
                }
                CommandResult install = adb.Run("install -r \"" + apk + "\"", 180000);
                if (!install.Output.Contains("Success"))
                    throw new Exception("Could not install the Moto Buttons app: " + install.Output);
            }
            adb.Shell("pm grant " + Config.AppPackage + " android.permission.BLUETOOTH_CONNECT");

            if (withDmd)
                InstallDmdSupport();
        }

        void InstallDmdSupport()
        {
            string apk = Path.Combine(adb.DataDir, "DMD_Manage.apk");
            if (!adb.Shell("pm list packages " + Config.DmdManagePackage).Contains("package:" + Config.DmdManagePackage))
            {
                if (!File.Exists(apk))
                {
                    Status(phoneStatus, "Downloading DMD Manage from THORK Racing...");
                    adb.Download(Config.DmdManageUrl, apk);
                }
                byte[] head = new byte[2];
                using (FileStream stream = File.OpenRead(apk))
                    stream.Read(head, 0, 2);
                if (head[0] != 'P' || head[1] != 'K')
                {
                    File.Delete(apk);
                    throw new Exception("The DMD Manage download is not an app package. Try again later.");
                }
                Status(phoneStatus, "Installing DMD Manage...");
                CommandResult install = adb.Run("install -r \"" + apk + "\"", 180000);
                if (!install.Output.Contains("Success"))
                    throw new Exception("Could not install DMD Manage: " + install.Output);
            }
            Status(phoneStatus, "Switching on DMD support...");
            // Android 13+ locks accessibility for side-loaded apps until allowed.
            adb.Shell("cmd appops set " + Config.DmdManagePackage + " ACCESS_RESTRICTED_SETTINGS allow");
            string enabled = adb.Shell("settings get secure enabled_accessibility_services").Trim();
            if (enabled == "null")
                enabled = "";
            if (!enabled.Contains(Config.DmdManageService))
                adb.Shell("settings put secure enabled_accessibility_services " +
                          (enabled.Length == 0 ? Config.DmdManageService : enabled + ":" + Config.DmdManageService));
            adb.Shell("settings put secure accessibility_enabled 1");
            adb.Shell("dumpsys deviceidle whitelist +" + Config.DmdManagePackage);
            adb.Shell("cmd appops set " + Config.DmdManagePackage + " RUN_ANY_IN_BACKGROUND allow");
        }

        /* ------------------------- page 3: controller ----------------------- */

        void BuildControllerPage()
        {
            Panel p = NewPage(PageController, "Find your controller",
                "Switch the controller on. The phone looks for Moto Buttons controllers paired with it.");
            scanProgress = new LinearProgress();
            scanProgress.Location = new Point(0, 84);
            scanProgress.Width = 700;
            scanProgress.Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top;
            scanStatus = MakeLabel("", 10f, FontStyle.Regular, Theme.OnSurfaceVariant);
            scanStatus.Location = new Point(0, 98);
            controllerList = new M3List(card.Fill);
            controllerList.Location = new Point(0, 128);
            controllerList.Size = new Size(700, 130);
            controllerList.Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top;
            controllerList.SelectedIndexChanged += delegate { UpdateNextEnabled(); };

            pairHelp = new Panel();
            pairHelp.BackColor = card.Fill;
            pairHelp.Location = new Point(0, 128);
            pairHelp.Size = new Size(700, 170);
            pairHelp.Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top;
            pairHelp.Visible = false;
            Label help = MakeLabel(
                "No controller found. Pair it with the phone:\r\n" +
                "1.  Switch the controller on.\r\n" +
                "2.  On the phone, in Bluetooth settings, tap Scan.\r\n" +
                "3.  Tap " + Config.ControllerName + ", then tap Pair.\r\n" +
                "If it was paired before an update that added Bluetooth features, unpair it first.",
                10f, FontStyle.Regular, Theme.OnSurface);
            help.Location = new Point(0, 0);
            M3Button openBluetooth = new M3Button("Open Bluetooth on the phone", ButtonKind.Tonal);
            openBluetooth.Location = new Point(0, 116);
            openBluetooth.Click += delegate
            {
                RunBusy(async delegate { await Task.Run(delegate { adb.Shell("am start -a android.settings.BLUETOOTH_SETTINGS"); }); });
            };
            M3Button again = new M3Button("Search again", ButtonKind.Text);
            again.Location = new Point(openBluetooth.Right + 12, 116);
            again.Click += delegate { StartScan(); };
            pairHelp.Controls.Add(help);
            pairHelp.Controls.Add(openBluetooth);
            pairHelp.Controls.Add(again);

            M3Button rescan = new M3Button("Search again", ButtonKind.Text);
            rescan.Location = new Point(-8, 266);
            rescan.Click += delegate { StartScan(); };
            p.Controls.Add(scanProgress);
            p.Controls.Add(scanStatus);
            p.Controls.Add(controllerList);
            p.Controls.Add(pairHelp);
            p.Controls.Add(rescan);
            p.Resize += delegate { scanProgress.Width = p.Width; controllerList.Width = p.Width; pairHelp.Width = p.Width; };
        }

        // Sends a command to the phone app and waits for its status file to answer.
        Dictionary<string, object> PhoneCommand(string action, string extras, string until, int timeoutMs, Action<Dictionary<string, object>> onUpdate)
        {
            int mySeq = ++seq;
            adb.Shell("am start -n " + Config.AppActivity + " -a " + action + " --ei seq " + mySeq + extras);
            DateTime end = DateTime.Now.AddMilliseconds(timeoutMs);
            while (DateTime.Now < end)
            {
                System.Threading.Thread.Sleep(1000);
                string text = adb.Run("shell cat " + Config.PhoneFiles + "status.json", 15000).Output;
                if (!text.StartsWith("{"))
                    continue;
                Dictionary<string, object> status;
                try { status = json.Deserialize<Dictionary<string, object>>(text); }
                catch (Exception) { continue; }
                if (!status.ContainsKey("seq") || Convert.ToInt32(status["seq"]) != mySeq)
                    continue;
                string state = (string)status["state"];
                if (state == "failed" || Array.IndexOf(until.Split('|'), state) >= 0)
                    return status;
                if (onUpdate != null)
                    onUpdate(status);
            }
            throw new Exception("The phone did not answer in time. Keep it unlocked with the Moto Buttons app open.");
        }

        void StartScan()
        {
            RunBusy(async delegate
            {
                controllerList.Items.Clear();
                pairHelp.Visible = false;
                controllerList.Visible = true;
                scanProgress.Visible = true;
                scanProgress.Indeterminate = true;
                scanStatus.Text = "The phone is looking for controllers...";
                List<ControllerInfo> found = null;
                try
                {
                    found = await Task.Run(new Func<List<ControllerInfo>>(ScanWork));
                }
                finally
                {
                    scanProgress.Indeterminate = false;
                    scanProgress.Visible = false;
                }
                if (found.Count == 0)
                {
                    scanStatus.Text = "";
                    controllerList.Visible = false;
                    pairHelp.Visible = true;
                    return;
                }
                foreach (ControllerInfo c in found)
                    controllerList.Items.Add(new ListRow(c.Name, "Firmware v" + c.Version + "  ·  " + c.Address, c));
                controllerList.SelectedIndex = 0;
                scanStatus.Text = found.Count == 1 ? "Found your controller." : "Found " + found.Count + " controllers. Choose one.";
            });
        }

        List<ControllerInfo> ScanWork()
        {
            Dictionary<string, object> status = PhoneCommand("com.bush.motobuttons.SCAN", "", "scan_done", 120000, null);
            if ((string)status["state"] == "failed")
                throw new Exception((string)status["message"]);
            List<ControllerInfo> list = new List<ControllerInfo>();
            foreach (object item in (ArrayList)status["controllers"])
            {
                Dictionary<string, object> c = (Dictionary<string, object>)item;
                ControllerInfo info = new ControllerInfo();
                info.Name = (string)c["name"];
                info.Address = (string)c["address"];
                info.Version = (string)c["version"];
                list.Add(info);
            }
            Log("Controllers found: " + list.Count);
            return list;
        }

        /* --------------------------- page 4: update ------------------------- */

        void BuildUpdatePage()
        {
            Panel p = NewPage(PageUpdate, "Update the controller", "");
            compareTitle = MakeLabel("", 14f, FontStyle.Bold, Theme.OnSurface);
            compareTitle.Location = new Point(0, 76);
            compareDetail = MakeLabel("", 10.5f, FontStyle.Regular, Theme.OnSurfaceVariant);
            compareDetail.Location = new Point(0, 110);
            compareDetail.MaximumSize = new Size(700, 0);
            updateProgress = new LinearProgress();
            updateProgress.Location = new Point(0, 170);
            updateProgress.Width = 700;
            updateProgress.Height = 8;
            updateProgress.Anchor = AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Top;
            updateProgress.Visible = false;
            updateStatus = MakeLabel("", 10f, FontStyle.Regular, Theme.OnSurfaceVariant);
            updateStatus.Location = new Point(0, 186);
            updateStatus.MaximumSize = new Size(700, 0);
            updateButton = new M3Button("Update", ButtonKind.Filled);
            updateButton.Location = new Point(0, 236);
            updateButton.Click += delegate { StartUpdate(); };
            reinstallButton = new M3Button("Reinstall anyway", ButtonKind.Text);
            reinstallButton.Location = new Point(-8, 236);
            reinstallButton.Click += delegate { StartUpdate(); };
            p.Controls.Add(compareTitle);
            p.Controls.Add(compareDetail);
            p.Controls.Add(updateProgress);
            p.Controls.Add(updateStatus);
            p.Controls.Add(updateButton);
            p.Controls.Add(reinstallButton);
            p.Resize += delegate { updateProgress.Width = p.Width; };
        }

        void ShowComparison()
        {
            updateProgress.Visible = false;
            updateStatus.Text = "";
            compareTitle.ForeColor = Theme.OnSurface;
            string have = controller.Version, file = firmware.Version;
            int cmp = file == null ? -1 : FirmwareInfo.Compare(have, file);
            if (cmp < 0)
            {
                compareTitle.Text = file == null ? "Ready to install" : "Update available";
                compareDetail.Text = controller.Name + " runs v" + have + ". The file is " + (file == null ? "of unknown version" : "v" + file) + ".";
                updateButton.Text = file == null ? "Install" : "Update to v" + file;
                updateButton.AutoSizeWidth();
                updateButton.Visible = true;
                reinstallButton.Visible = false;
            }
            else
            {
                compareTitle.Text = cmp == 0 ? "Already up to date" : "Controller is newer";
                compareDetail.Text = cmp == 0
                    ? controller.Name + " already runs v" + have + ". Nothing to do."
                    : controller.Name + " runs v" + have + ", newer than the file (v" + file + ").";
                compareTitle.ForeColor = cmp == 0 ? Theme.Success : Theme.OnSurface;
                updateButton.Visible = false;
                reinstallButton.Text = cmp == 0 ? "Reinstall v" + file + " anyway" : "Install older v" + file + " anyway";
                reinstallButton.AutoSizeWidth();
                reinstallButton.Visible = true;
                nextButton.Text = "Close";
                nextButton.Visible = true;
                nextButton.Enabled = true;
            }
        }

        void StartUpdate()
        {
            updateButton.Visible = false;
            reinstallButton.Visible = false;
            nextButton.Visible = false;
            backButton.Visible = false;
            RunBusy(async delegate
            {
                updateProgress.Visible = true;
                updateProgress.Indeterminate = false;
                updateProgress.Value = 0;
                compareTitle.ForeColor = Theme.OnSurface;
                compareTitle.Text = "Updating...";
                compareDetail.Text = "Keep the phone near the controller and the controller powered.";
                bool ok = false;
                string message = null;
                try
                {
                    message = await Task.Run(new Func<string>(UpdateWork));
                    ok = true;
                }
                catch (Exception e)
                {
                    message = e.Message;
                    Log("ERROR: " + e.Message);
                }
                updateProgress.Visible = ok;
                compareTitle.ForeColor = ok ? Theme.Success : Theme.Error;
                compareTitle.Text = ok ? "Update complete" : "Update failed";
                updateStatus.Text = message;
                if (ok)
                {
                    updateDone = true;
                    nextButton.Text = "Close";
                    nextButton.Visible = true;
                }
                else
                {
                    updateButton.Text = "Try again";
                    updateButton.AutoSizeWidth();
                    updateButton.Visible = true;
                    backButton.Visible = true;
                    nextButton.Visible = true;
                }
            });
        }

        string UpdateWork()
        {
            Status(updateStatus, "Copying the firmware to the phone...");
            CommandResult push = adb.Run("push \"" + firmware.Path + "\" " + Config.PhoneFiles + "firmware.bin", 60000);
            if (!push.Ok)
                throw new Exception("Could not copy the firmware to the phone: " + push.Output);

            Status(updateStatus, "Connecting to the controller...");
            Dictionary<string, object> result = PhoneCommand("com.bush.motobuttons.UPDATE", " --es address " + controller.Address,
                "done", 300000, delegate(Dictionary<string, object> status)
                {
                    int percent = Convert.ToInt32(status["progress"]);
                    string text = (string)status["message"];
                    Ui(delegate
                    {
                        updateProgress.Value = percent;
                        updateStatus.Text = text + (percent > 0 ? "  " + percent + "%" : "");
                    });
                });
            if ((string)result["state"] == "failed")
                throw new Exception((string)result["message"]);
            Ui(delegate { updateProgress.Value = 100; });

            // The controller restarts; confirm it came back with the new version.
            Status(updateStatus, "The controller is restarting...");
            System.Threading.Thread.Sleep(20000);
            Status(updateStatus, "Checking the new version...");
            foreach (ControllerInfo c in ScanWork())
            {
                if (c.Address == controller.Address)
                {
                    controller.Version = c.Version;
                    return controller.Name + " now runs v" + c.Version + ".";
                }
            }
            return "The update was installed. The controller did not answer the check yet; switch it off and on if the buttons do not respond.";
        }
    }
}
