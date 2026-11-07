using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// The test classes that load tmdq_core.dll through <see cref="Processing.NativeCore"/>. One of them points
    /// TMDQ_CORE_DLL at a missing file for a moment, and a load from another class in that moment would fail for no
    /// reason of its own, so they share a collection and xunit runs them one at a time.
    /// </summary>
    [CollectionDefinition(Name)]
    public sealed class NativeCoreCollection
    {
        public const string Name = "Native core";
    }
}
