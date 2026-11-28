using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia.Media;

namespace TmDq.App.ViewModels.Plot;

/// <summary>
/// The series colours of US4.0. One palette of 15 hues serves both metrics, and the line tells them apart: a Frame Sync Lock
/// stream (a PCM channel, its lock percentage and its missed frames alike) is drawn solid in its hue, and a Receiver SNR stream
/// (a receiver, or a channel of a phased array antenna) dotted, with its combined channel in the full hue and a little thicker
/// and its other two channels lighter tints of it. The lock streams take the hues from the first, the SNR streams from the last,
/// so stream 1 of the two is not one colour; streams beyond the 15th take the hues again from where their own run began.
/// <para>A shade is made in 16-bit HSV, converted in <c>float</c> and rounded half away from zero: the arithmetic
/// of the colour class v2.x drew with, step for step, so every shade is the very colour v2.x drew.
/// <c>PlotTests</c> holds it to 84 shades v2.14.2 printed, round the whole hue wheel.</para>
/// </summary>
public static class PlotColors
{
    /// <summary>
    /// The 15 hues, each a saturated colour so that its two lighter tints (an SNR stream's other channels, see
    /// <see cref="Assign(IEnumerable{PlotSeries})"/>) stay different from it and from each other. Frame Sync Lock streams take
    /// them in stream order from the first and SNR streams by receiver (or antenna channel) from the last, see <see cref="SnrHue"/>;
    /// the 16th stream of either takes its first hue again.
    /// </summary>
    public static readonly IReadOnlyList<Color> StreamHues = new[]
    {
        Color.FromRgb(204, 0, 0),      // red
        Color.FromRgb(30, 90, 220),    // blue
        Color.FromRgb(20, 150, 60),    // green
        Color.FromRgb(255, 140, 0),    // orange
        Color.FromRgb(120, 40, 190),   // purple
        Color.FromRgb(0, 170, 200),    // cyan
        Color.FromRgb(210, 30, 140),   // magenta
        Color.FromRgb(130, 170, 20),   // lime
        Color.FromRgb(0, 140, 120),    // teal
        Color.FromRgb(230, 190, 0),    // gold
        Color.FromRgb(70, 50, 200),    // indigo
        Color.FromRgb(240, 80, 120),   // rose
        Color.FromRgb(160, 80, 20),    // brown
        Color.FromRgb(70, 160, 255),   // sky
        Color.FromRgb(10, 100, 50),    // forest
    };

    /// <summary>The channel index of the combined polarization (a receiver's C, an antenna's C): the strongest.</summary>
    public const int CombinedChannelIndex = 2;

    /// <summary>
    /// The hue <see cref="StreamHues"/> holds that Auto-Color never gives out: the gold, 1.8 to 1 on the light theme's white,
    /// below the 2.0 the chart draws a series at there (<c>ChartPalette.MinSeriesContrast</c>), so the chart would draw it
    /// darker than the gold it was given. Every other hue is at least 2.0 on both themes' backgrounds (<c>PlotTests</c>).
    /// </summary>
    public static readonly Color AutoColorExcluded = Color.FromRgb(230, 190, 0);

    /// <summary>
    /// The hues Auto-Color gives out (<see cref="AutoHue"/>), in the order it gives them: <see cref="StreamHues"/> less
    /// <see cref="AutoColorExcluded"/>, the red first, then each time the hue farthest from all those already taken (its
    /// least distance to them the greatest, in OKLab, where a distance reads as the same difference anywhere on the wheel; a
    /// tie to the hue earlier in the palette). However many units it colours, they are the first that many of these, so a
    /// plot of few streams gets the most different hues of the palette.
    /// </summary>
    public static readonly IReadOnlyList<Color> AutoColorHues = FarthestFirst(StreamHues.Where(h => h != AutoColorExcluded).ToList());

    /// <summary>The hue Auto-Color gives its <paramref name="unit"/>-th unit (counted from 0): <see cref="AutoColorHues"/> in
    /// order, the 15th unit the first's again, so two units of one hue are as far apart in the list as the palette allows.</summary>
    public static Color AutoHue(int unit) => AutoColorHues[unit % AutoColorHues.Count];

    /// <summary><paramref name="pool"/> reordered: its first, then each time the one whose least OKLab distance to those
    /// already taken is the greatest (the first in <paramref name="pool"/> of equals).</summary>
    private static List<Color> FarthestFirst(List<Color> pool)
    {
        var order = new List<Color>(pool.Count);
        if (pool.Count == 0) return order;
        var rest = pool.Skip(1).ToList();
        order.Add(pool[0]);
        var nearest = rest.Select(c => OkDistance(c, pool[0])).ToList();
        while (rest.Count > 0)
        {
            int best = 0;
            for (int i = 1; i < rest.Count; i++)
                if (nearest[i] > nearest[best]) best = i;
            Color taken = rest[best];
            order.Add(taken);
            rest.RemoveAt(best);
            nearest.RemoveAt(best);
            for (int i = 0; i < rest.Count; i++) nearest[i] = Math.Min(nearest[i], OkDistance(rest[i], taken));
        }
        return order;
    }

    /// <summary>The distance of two colours in OKLab (Björn Ottosson's): 0 for one colour, about 0.1 for two that read as
    /// clearly different.</summary>
    public static double OkDistance(Color a, Color b)
    {
        (double l1, double a1, double b1) = OkLab(a);
        (double l2, double a2, double b2) = OkLab(b);
        return Math.Sqrt((l1 - l2) * (l1 - l2) + (a1 - a2) * (a1 - a2) + (b1 - b2) * (b1 - b2));
    }

    private static (double L, double A, double B) OkLab(Color c)
    {
        static double Linear(byte v) { double s = v / 255.0; return s <= 0.04045 ? s / 12.92 : Math.Pow((s + 0.055) / 1.055, 2.4); }
        double r = Linear(c.R), g = Linear(c.G), b = Linear(c.B);
        double l = Math.Cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
        double m = Math.Cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
        double s = Math.Cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
        return (0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s);
    }

    /// <summary>
    /// Colours every series, in order: the left-axis metrics share one colour per stream (by its stream order, the
    /// PCM channel), so a stream's lock and missed-frames curves match, and each new stream takes the next of the hues,
    /// drawn solid; an SNR series takes its receiver's hue, lighter by its channel, drawn dotted.
    /// </summary>
    internal static void Assign(IEnumerable<PlotSeries> series) => Assign(series, new Dictionary<int, int>());

    /// <summary>
    /// Colours <paramref name="series"/> only, as <see cref="Assign(IEnumerable{PlotSeries})"/> colours a whole plot, with
    /// <paramref name="leftIndex"/> the Frame Sync Lock colour each stream order has already been given: a stream order in it
    /// keeps its colour, and one not in it takes the next and is added. The plot keeps it between calls, so a recording or a
    /// stream added later is coloured on its own and the series already shown keep the colours they have (the user's, or a
    /// template's); colouring one recording's streams one at a time, in job order, gives the colours colouring them together
    /// does.
    /// </summary>
    internal static void Assign(IEnumerable<PlotSeries> series, Dictionary<int, int> leftIndex)
    {
        foreach (PlotSeries s in series)
        {
            if (s.IsLeftAxis)
            {
                if (!leftIndex.TryGetValue(s.StreamOrder, out int idx))
                {
                    idx = leftIndex.Count;
                    leftIndex[s.StreamOrder] = idx;
                }
                // A PCM channel's Frame Sync Lock and missed frames: solid, in the next hue, the 16th stream the first's again.
                s.Color = StreamHues[idx % StreamHues.Count];
                s.Dotted = false;
                s.Strong = false;
                continue;
            }
            // Receiver SNR, of a recording or an antenna's log: always dotted, so it never reads as a lock curve of the same hue,
            // and in the hues from the last end, so receiver 1 is not the colour of PCM channel 1 either.
            int stream = Math.Max(0, s.ReceiverIndex - 1);
            s.Color = Shade(SnrHue(stream), Weakness(s.ChannelIndex));
            s.Dotted = true;
            s.Strong = s.ChannelIndex == CombinedChannelIndex;
        }
    }

    /// <summary>The hue of a Receiver SNR stream (counted from 0): the palette taken from its last hue back, the 16th stream the
    /// last again. A Frame Sync Lock stream takes the hues from the first on, so stream 1 of the two (and every stream number
    /// but the 8th, which is the middle hue of both) is told apart by its colour as well as its line. A lock stream k and an SNR
    /// stream 16 - k do share a hue; a plot of fewer than eight of each never shows it.</summary>
    public static Color SnrHue(int stream) => StreamHues[StreamHues.Count - 1 - (stream % StreamHues.Count)];

    /// <summary>How many times lighter a channel is than its stream's hue: the combined channel (index 2) none, the first
    /// (L or H) once and the second (R or V) twice, then one more for each channel beyond them.</summary>
    public static int Weakness(int channelIndex) => channelIndex switch
    {
        CombinedChannelIndex => 0,
        0 => 1,
        1 => 2,
        _ => Math.Max(0, channelIndex),
    };

    /// <summary><paramref name="color"/> made lighter <paramref name="level"/> times: per level the saturation
    /// falls by 60 (not below 40) and the value rises by 20 (not above 255). Level 0 or below is the colour itself.</summary>
    public static Color Shade(Color color, int level)
    {
        if (level <= 0) return color;
        (int h, int s, int v) = ToHsv16(color);
        s = Math.Max(40, s - level * 60);
        v = Math.Min(255, v + level * 20);
        return FromHsv16(h, s, v);
    }

    // 16 bits a channel: an 8-bit value is stored times 0x101 and read back through Div257. The conversion between
    // RGB and HSV is in float, not double, or a shade comes out one off.
    private const float Max16 = 65535f;

    // A 16-bit channel back to 8 bits, rounded.
    private static int Div257(int x) => (x - (x >> 8) + 0x80) >> 8;

    // Rounding half away from zero; every value rounded here is non-negative.
    private static int RoundHalfAway(float d) => d >= 0f ? (int)(d + 0.5f) : (int)(d - 0.5f);

    /// <summary>An RGB colour in 8-bit HSV, through 16-bit channels: hue 0 to 359 (-1 for no hue), saturation and
    /// value 0 to 255.</summary>
    private static (int H, int S, int V) ToHsv16(Color c)
    {
        float r = c.R * 0x101 / Max16, g = c.G * 0x101 / Max16, b = c.B * 0x101 / Max16;
        float max = Math.Max(r, Math.Max(g, b));
        float min = Math.Min(r, Math.Min(g, b));
        float delta = max - min;
        int value = RoundHalfAway(max * Max16);
        int hue, saturation;
        if (Math.Abs(delta) <= 0.00001f)
        {
            hue = ushort.MaxValue;   // achromatic
            saturation = 0;
        }
        else
        {
            saturation = RoundHalfAway(delta / max * Max16);
            float h;
            if (r == max) h = (g - b) / delta;
            else if (g == max) h = 2f + (b - r) / delta;
            else h = 4f + (r - g) / delta;
            h *= 60f;
            if (h < 0f) h += 360f;
            hue = RoundHalfAway(h * 100);
        }
        return (hue == ushort.MaxValue ? -1 : hue / 100, Div257(saturation), Div257(value));
    }

    /// <summary>An 8-bit HSV colour, set through 16-bit channels and read back as RGB.</summary>
    private static Color FromHsv16(int h, int s, int v)
    {
        int hue = h == -1 ? ushort.MaxValue : (h % 360) * 100;
        int saturation = s * 0x101, value = v * 0x101;
        if (saturation == 0 || hue == ushort.MaxValue)
        {
            byte grey = (byte)Div257(value);
            return Color.FromRgb(grey, grey, grey);
        }
        float hh = hue == 36000 ? 0f : hue / 6000f;
        float ss = saturation / Max16;
        float vv = value / Max16;
        int i = (int)hh;
        float f = hh - i;
        float p = vv * (1f - ss);
        float r, g, b;
        if ((i & 1) != 0)
        {
            float q = vv * (1f - ss * f);
            (r, g, b) = i switch
            {
                1 => (q, vv, p),
                3 => (p, q, vv),
                _ => (vv, p, q),
            };
        }
        else
        {
            float t = vv * (1f - ss * (1f - f));
            (r, g, b) = i switch
            {
                0 => (vv, t, p),
                2 => (p, vv, t),
                _ => (t, p, vv),
            };
        }
        return Color.FromRgb((byte)Div257(RoundHalfAway(r * Max16)), (byte)Div257(RoundHalfAway(g * Max16)), (byte)Div257(RoundHalfAway(b * Max16)));
    }
}
