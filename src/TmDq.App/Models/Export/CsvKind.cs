using System.Collections.Generic;

namespace TmDq.App.Models.Export;

/// <summary>What a CSV file to import is (US10.0): the user says, in the dialog every way of importing one goes through, because the
/// two layouts are told apart by who wrote the file and not guessed from its header.</summary>
public enum CsvKind
{
    /// <summary>A CSV this application exported: standalone telemetry receivers' series (US6.3).</summary>
    TelemetryReceivers,

    /// <summary>A phased array antenna's SNR log, written by the antenna's own system (US10.0).</summary>
    PhasedArrayAntenna,
}

/// <summary>The kinds in the order the dialog offers them, and the titles it gives them.</summary>
public static class CsvKinds
{
    /// <summary>Every kind, in the order the dialog lists them.</summary>
    public static IReadOnlyList<CsvKind> All { get; } = new[] { CsvKind.TelemetryReceivers, CsvKind.PhasedArrayAntenna };

    /// <summary>The choice's title, ending in U+2026 as a menu entry that asks something does.</summary>
    public static string Title(CsvKind kind) => kind switch
    {
        CsvKind.PhasedArrayAntenna => "AV Phased Array Antenna\u2026",
        _ => "Standalone Telemetry Receivers\u2026",
    };
}