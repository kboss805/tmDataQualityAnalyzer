namespace TmDq.App.Models.Export;

/// <summary>What an imported CSV file turned out to be (US10.0). No one is asked: the two layouts differ from their first line, and
/// the file is read as whichever it is (<c>PlotCsv.Parse</c>).</summary>
public enum CsvKind
{
    /// <summary>A CSV this application exported: standalone telemetry receivers' series (US6.3).</summary>
    TelemetryReceivers,

    /// <summary>A phased array antenna's SNR log, written by the antenna's own system (US10.0).</summary>
    PhasedArrayAntenna,
}
