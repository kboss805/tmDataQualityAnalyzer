using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using Avalonia.Controls;
using Avalonia.Headless.XUnit;
using Avalonia.Media;
using Avalonia.Platform;
using Avalonia.Threading;
using TmDq.App.Views;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// The application's icon (assets\icon.ico, made from docs\design\icon_image.png by eng\gen_app_icon.py): in the exe,
    /// where Explorer, the taskbar and the shortcuts find it, and on every window, for the title bar and Alt+Tab. v3.0.0
    /// shipped with neither: the Qt application's icon came from its .rc file, which the port left behind.
    /// </summary>
    public sealed class IconTests
    {
        private const uint LoadLibraryAsDatafile = 0x2, LoadLibraryAsImageResource = 0x20;
        private static readonly IntPtr RtIcon = new IntPtr(3), RtGroupIcon = new IntPtr(14);

        private delegate bool EnumResNameProc(IntPtr module, IntPtr type, IntPtr name, IntPtr param);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool FreeLibrary(IntPtr module);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool EnumResourceNamesW(IntPtr module, IntPtr type, EnumResNameProc callback, IntPtr param);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr FindResourceW(IntPtr module, IntPtr name, IntPtr type);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
        [DllImport("kernel32.dll")]
        private static extern IntPtr LockResource(IntPtr data);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint SizeofResource(IntPtr module, IntPtr resource);

        private static string IconFile => Repo.File(Path.Combine("assets", "icon.ico"));

        /// <summary>An icon's images as its file lists them: the size (0 is 256), the bit depth and the image's bytes.</summary>
        private sealed record IconImage(int Width, int Height, int BitCount, byte[] Data);

        private static List<IconImage> ReadIconFile(byte[] file)
        {
            Assert.Equal((0, 1), (BitConverter.ToUInt16(file, 0), BitConverter.ToUInt16(file, 2)));   // reserved, type icon
            int count = BitConverter.ToUInt16(file, 4);
            var images = new List<IconImage>();
            for (int i = 0; i < count; i++)
            {
                int entry = 6 + 16 * i;
                int size = BitConverter.ToInt32(file, entry + 8), offset = BitConverter.ToInt32(file, entry + 12);
                images.Add(new IconImage(file[entry], file[entry + 1], BitConverter.ToUInt16(file, entry + 6), file[offset..(offset + size)]));
            }
            return images;
        }

        private static byte[] Resource(IntPtr module, IntPtr name, IntPtr type)
        {
            IntPtr found = FindResourceW(module, name, type);
            Assert.True(found != IntPtr.Zero, $"the exe has no resource {name} of type {type}");
            var bytes = new byte[SizeofResource(module, found)];
            Marshal.Copy(LockResource(LoadResource(module, found)), bytes, 0, bytes.Length);
            return bytes;
        }

        /// <summary>The exe's first icon group (the one Explorer shows) with its images, as an icon file's entries.</summary>
        private static List<IconImage> ReadExeIcon(string exe)
        {
            IntPtr module = LoadLibraryExW(exe, IntPtr.Zero, LoadLibraryAsDatafile | LoadLibraryAsImageResource);
            Assert.True(module != IntPtr.Zero, "cannot open the exe's resources: error " + Marshal.GetLastWin32Error());
            try
            {
                var groups = new List<IntPtr>();
                EnumResourceNamesW(module, RtGroupIcon, (_, _, name, _) => { groups.Add(name); return true; }, IntPtr.Zero);
                Assert.True(groups.Count > 0, "the exe has no icon group (RT_GROUP_ICON): Explorer and the taskbar show a generic icon");
                byte[] group = Resource(module, groups[0], RtGroupIcon);
                int count = BitConverter.ToUInt16(group, 4);
                var images = new List<IconImage>();
                for (int i = 0; i < count; i++)
                {
                    int entry = 6 + 14 * i;   // GRPICONDIRENTRY: the file's entry with a resource id for its offset
                    int id = BitConverter.ToUInt16(group, entry + 12);
                    images.Add(new IconImage(group[entry], group[entry + 1], BitConverter.ToUInt16(group, entry + 6),
                                             Resource(module, new IntPtr(id), RtIcon)));
                }
                return images;
            }
            finally
            {
                FreeLibrary(module);
            }
        }

        [Fact]
        public void TheIconIsSquareInEverySizeWindowsAsksFor()
        {
            // Windows picks the size it needs (16 for a title bar at 100 %, 24 and 32 for the taskbar, 48 and 256 for
            // Explorer's views) and stretches the nearest when the file lacks it. v3.0.0's had one image, 64 by 61.
            List<IconImage> images = ReadIconFile(File.ReadAllBytes(IconFile));
            Assert.Equal(new[] { 16, 24, 32, 48, 64, 0 }, images.Select(i => i.Width));
            Assert.All(images, i => Assert.Equal((i.Width, 32), (i.Height, i.BitCount)));
        }

        [Fact]
        public void TheExeCarriesTheIcon()
        {
            // The project's ApplicationIcon puts it in the managed assembly, and the SDK copies the assembly's resources
            // into the apphost exe (and into a single-file publish's host: the packaging script reads it back out of the
            // published exe, as it does the manifest). mt.exe's manifest merge after that must leave it in place.
            string exe = Path.Combine(Repo.AppOutput, "tmDataQualityAnalyzer.exe");
            Assert.True(File.Exists(exe), "the application has not been built: " + exe);
            List<IconImage> expected = ReadIconFile(File.ReadAllBytes(IconFile)), actual = ReadExeIcon(exe);
            Assert.Equal(expected.Select(i => (i.Width, i.Height, i.BitCount)), actual.Select(i => (i.Width, i.Height, i.BitCount)));
            for (int i = 0; i < expected.Count; i++)
                Assert.True(expected[i].Data.AsSpan().SequenceEqual(actual[i].Data), $"the exe's image {i} is not icon.ico's");
        }

        [AvaloniaFact]
        public void TheWindowsIconIsTheIconFile()
        {
            // The windows' icon is an Avalonia resource of the same file, not a copy that could drift from the exe's.
            using Stream asset = AssetLoader.Open(new Uri("avares://tmDataQualityAnalyzer/Assets/icon.ico"));
            using var bytes = new MemoryStream();
            asset.CopyTo(bytes);
            Assert.Equal(File.ReadAllBytes(IconFile), bytes.ToArray());
        }

        /// <summary>A window of each kind the application opens: those that need arguments, given some.</summary>
        private static readonly Dictionary<Type, Func<Window>> NeedArguments = new()
        {
            [typeof(ChoiceWindow)] = () => new ChoiceWindow("Choose", "One of", new[] { "a", "b" }),
            [typeof(ReportWindow)] = () => new ReportWindow("Report", "text"),
            [typeof(TextPromptWindow)] = () => new TextPromptWindow("Title", "Label", "text"),
            [typeof(NumberPromptWindow)] = () => new NumberPromptWindow("Title", "Label", 1, 0, 10, 0),
            [typeof(TimeWindowPromptWindow)] = () => new TimeWindowPromptWindow("000:00:00:00", "000:00:00:01"),
            [typeof(ColorPromptWindow)] = () => new ColorPromptWindow(Colors.Red),
        };

        [AvaloniaFact]
        public void EveryWindowShowsTheIcon()
        {
            // The title bar and Alt+Tab take the window's own icon, not the exe's. The dialogs have no taskbar button, but
            // a title bar each. A window kind added later is held here too: it must be constructible below.
            Type[] kinds = typeof(App).Assembly.GetTypes()
                                      .Where(t => typeof(Window).IsAssignableFrom(t) && !t.IsAbstract && !t.IsGenericTypeDefinition)
                                      .OrderBy(t => t.FullName).ToArray();
            Assert.Contains(typeof(MainWindow), kinds);
            foreach (Type kind in kinds)
            {
                Window window = NeedArguments.TryGetValue(kind, out Func<Window>? make) ? make()
                    : kind.GetConstructor(Type.EmptyTypes) is { } constructor ? (Window)constructor.Invoke(null)
                    : throw new InvalidOperationException(kind.Name + " needs arguments: add it to NeedArguments");
                window.Show();
                Dispatcher.UIThread.RunJobs();
                try
                {
                    Assert.True(window.Icon is not null, kind.Name + " has no icon");
                }
                finally
                {
                    window.Close();
                }
            }
        }
    }
}
