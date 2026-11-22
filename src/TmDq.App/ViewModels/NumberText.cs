using System;
using System.Globalization;

namespace TmDq.App.ViewModels;

/// <summary>
/// The number fields' text. The application has no up/down incrementers: a number is typed into a plain text box, and
/// what is typed is what the dialog holds, so each dialog reads it with these, always in the invariant culture (a point
/// is the decimal separator, as in the settings files, the readouts and the CSV), and refuses what is not a number
/// as it is typed. A parse gives null for text that is not one.
/// </summary>
public static class NumberText
{
    private const NumberStyles IntegerStyle = NumberStyles.AllowLeadingSign | NumberStyles.AllowLeadingWhite | NumberStyles.AllowTrailingWhite;

    /// <summary>The integer the text is (digits with an optional sign, spaces around), else null. One beyond the int range
    /// is the nearest int, which the caller's range check then names as out of range; one past a long is no integer.</summary>
    public static int? ParseInteger(string text) =>
        long.TryParse(text, IntegerStyle, CultureInfo.InvariantCulture, out long v)
            ? (int)Math.Clamp(v, int.MinValue, int.MaxValue)
            : null;

    /// <summary>The finite number the text is (a point as the decimal separator, an exponent allowed), else null.</summary>
    public static double? ParseNumber(string text) =>
        double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out double v) && double.IsFinite(v) ? v : null;

    /// <summary>The same, as a decimal (what Extract Calibration's clips are held as).</summary>
    public static decimal? ParseDecimal(string text) =>
        decimal.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out decimal v) ? v : null;

    public static string FormatInteger(int value) => value.ToString(CultureInfo.InvariantCulture);

    /// <summary>Three decimals (5.000), unless that would not read back as the same number, then every digit it needs.</summary>
    public static string FormatNumber(double value)
    {
        string fixedPoint = value.ToString("F3", CultureInfo.InvariantCulture);
        return double.Parse(fixedPoint, CultureInfo.InvariantCulture) == value ? fixedPoint : value.ToString("R", CultureInfo.InvariantCulture);
    }

    /// <summary>One decimal (0.0), unless that would not read back as the same number, then every digit it needs.</summary>
    public static string FormatDecimal(decimal value)
    {
        string fixedPoint = value.ToString("F1", CultureInfo.InvariantCulture);
        return decimal.Parse(fixedPoint, CultureInfo.InvariantCulture) == value ? fixedPoint : value.ToString(CultureInfo.InvariantCulture);
    }

    /// <summary>A value at a number of decimals, as the plot's number prompt shows its initial value (F0, F1).</summary>
    public static string FormatFixed(double value, int decimals) =>
        value.ToString("F" + decimals.ToString(CultureInfo.InvariantCulture), CultureInfo.InvariantCulture);
}
