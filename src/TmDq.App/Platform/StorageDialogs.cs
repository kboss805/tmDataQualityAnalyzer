using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using Avalonia.Platform.Storage;

namespace TmDq.App.Platform
{
    /// <summary>What the storage pickers share: the folder a dialog opens at, and the path a dialog's answer is.</summary>
    public static class StorageDialogs
    {
        /// <summary>The folder a dialog opens at: <paramref name="folder"/> when it exists, else the platform's own choice.</summary>
        public static async Task<IStorageFolder?> StartFolderAsync(IStorageProvider storage, string? folder) =>
            string.IsNullOrEmpty(folder) || !Directory.Exists(folder)
                ? null
                : await storage.TryGetFolderFromPathAsync(new Uri(Path.GetFullPath(folder)));

        /// <summary>The local path of the first item chosen, or null when nothing was.</summary>
        public static string? FirstPath(IReadOnlyList<IStorageItem> chosen) => chosen.Count > 0 ? chosen[0].TryGetLocalPath() : null;
    }
}
