using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// The manifest that matters is the one in the apphost exe, the process itself: the SDK's own manifest setting
    /// puts it in the managed assembly instead (found in the spike), so the build merges app.manifest into the exe
    /// with mt.exe. These tests read the manifest resource back out of the built exe.
    /// </summary>
    public sealed class ManifestTests
    {
        private const uint LoadLibraryAsDatafile = 0x2, LoadLibraryAsImageResource = 0x20;
        private static readonly IntPtr RtManifest = new IntPtr(24), ManifestId = new IntPtr(1);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool FreeLibrary(IntPtr module);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr FindResourceW(IntPtr module, IntPtr name, IntPtr type);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
        [DllImport("kernel32.dll")]
        private static extern IntPtr LockResource(IntPtr data);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint SizeofResource(IntPtr module, IntPtr resource);

        private static string ExeManifest()
        {
            string exe = Path.Combine(Repo.AppOutput, "tmDataQualityAnalyzer.exe");
            Assert.True(File.Exists(exe), "the application has not been built: " + exe);
            IntPtr module = LoadLibraryExW(exe, IntPtr.Zero, LoadLibraryAsDatafile | LoadLibraryAsImageResource);
            Assert.True(module != IntPtr.Zero, "cannot open the exe's resources: error " + Marshal.GetLastWin32Error());
            try
            {
                IntPtr found = FindResourceW(module, ManifestId, RtManifest);
                Assert.True(found != IntPtr.Zero, "the exe has no manifest resource #1");
                int size = (int)SizeofResource(module, found);
                IntPtr data = LockResource(LoadResource(module, found));
                var bytes = new byte[size];
                Marshal.Copy(data, bytes, 0, size);
                return Encoding.UTF8.GetString(bytes);
            }
            finally
            {
                FreeLibrary(module);
            }
        }

        [Fact]
        public void TheExeSelectsTheUtf8CodePage()
        {
            // Without it a recording under a non-ASCII folder cannot reach irig106's narrow open().
            string manifest = ExeManifest();
            Assert.Contains("http://schemas.microsoft.com/SMI/2019/WindowsSettings", manifest);
            Assert.Matches(@"<activeCodePage[^>]*>\s*UTF-8\s*</activeCodePage>", manifest);
        }

        [Fact]
        public void TheExeDeclaresWindows10Compatibility()
        {
            // mt.exe replaces the apphost's manifest, so anything the process needs must be in app.manifest.
            Assert.Contains("{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}", ExeManifest());
        }

        [Fact]
        public void TheManifestIsMergedIntoEveryHostTheSdkCreates()
        {
            // The exe in bin\ is only one of three hosts: a self-contained single-file publish bundles singlefilehost,
            // and other publishes use apphost_publish. Patching only the build output shipped a single-file app with the
            // SDK's default manifest (no UTF-8 code page; found in plan step 3.3). A real publish needs the runtime packs
            // (about 100 MB per CI run), so this holds the project to its three hooks, and the packaging script
            // (deploy\avalonia\build_avalonia_package.ps1) reads the manifest back out of the published exe.
            string csproj = File.ReadAllText(Repo.File(Path.Combine("dotnet", "TmDq.App", "TmDq.App.csproj")));
            foreach ((string after, string host) in new[] { ("_CreateAppHost", "AppHostIntermediatePath"),
                                                           ("_CreateSingleFileHost", "SingleFileHostIntermediatePath"),
                                                           ("_CreateAppHostForPublish", "AppHostForPublishIntermediatePath") })
            {
                Assert.Matches($"<Target [^>]*AfterTargets=\"{after}\"[^>]*>\\s*<Exec Command=\"mt -nologo -manifest [^\"]*app\\.manifest[^\"]*-outputresource:&quot;\\$\\({host}\\);#1&quot;\"", csproj);
            }
        }

        [Fact]
        public void TheMergedManifestIsTheProjectsOwn()
        {
            // The exe carries app.manifest itself, not some other manifest that happens to set the code page.
            string source = File.ReadAllText(Repo.File(Path.Combine("dotnet", "TmDq.App", "app.manifest")));
            string marker = "Merged into tmDataQualityAnalyzer.exe after the build by mt.exe";
            Assert.Contains(marker, source);
            Assert.Contains(marker, ExeManifest());
        }
    }
}
