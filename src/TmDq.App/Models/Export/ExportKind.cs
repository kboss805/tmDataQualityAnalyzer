namespace TmDq.App.Models.Export
{
    /// <summary>What an export writes: the plot as an image, its data as CSV, or the log as text.</summary>
    public enum ExportKind
    {
        Image,
        Data,
        Log,
    }
}
