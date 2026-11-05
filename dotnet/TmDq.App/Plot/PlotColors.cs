using System;
using System.Collections.Generic;
using Avalonia.Media;

namespace TmDq.App.Plot
{
    /// <summary>
    /// The series colours of US4.0: Frame Sync Lock in purple, blue and green, one per stream; Receiver SNR in red,
    /// orange and yellow, one per receiver; and beyond the primaries, progressively lighter shades of them.
    /// <para>A shade is made in 16-bit HSV, converted in <c>float</c> and rounded half away from zero: the arithmetic
    /// of the colour class v2.x drew with, step for step, so every shade is the very colour v2.x drew.
    /// <c>PlotTests</c> holds it to 84 shades v2.14.2 printed, round the whole hue wheel.</para>
    /// </summary>
    public static class PlotColors
    {
        /// <summary>The Frame Sync Lock primaries.</summary>
        public static readonly IReadOnlyList<Color> FrameSyncLock = new[]
        {
            Color.FromRgb(106, 13, 173),   // purple
            Color.FromRgb(67, 97, 238),    // blue
            Color.FromRgb(46, 184, 92),    // green
        };

        /// <summary>The Receiver SNR primaries.</summary>
        public static readonly IReadOnlyList<Color> Snr = new[]
        {
            Color.FromRgb(204, 0, 0),      // red
            Color.FromRgb(255, 140, 0),    // orange
            Color.FromRgb(230, 200, 30),   // yellow
        };

        /// <summary>
        /// Colours every series, in order: the left-axis metrics share one colour per stream (by its stream order, the
        /// PCM channel), so a stream's lock and missed-frames curves match, and each new stream takes the next Frame
        /// Sync Lock primary, then shades of them; an SNR series takes its receiver's primary, shaded by its receiver's
        /// cycle through the primaries and by its channel.
        /// </summary>
        internal static void Assign(IEnumerable<PlotSeries> series)
        {
            var leftIndex = new Dictionary<int, int>();
            foreach (PlotSeries s in series)
            {
                if (s.IsLeftAxis)
                {
                    if (!leftIndex.TryGetValue(s.StreamOrder, out int idx))
                    {
                        idx = leftIndex.Count;
                        leftIndex[s.StreamOrder] = idx;
                    }
                    int n = FrameSyncLock.Count;
                    s.Color = Shade(FrameSyncLock[idx % n], idx / n);
                    continue;
                }
                int primaries = Snr.Count;
                int receiver = Math.Max(0, s.ReceiverIndex - 1);
                s.Color = Shade(Snr[receiver % primaries], receiver / primaries + s.ChannelIndex);
            }
        }

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
}
