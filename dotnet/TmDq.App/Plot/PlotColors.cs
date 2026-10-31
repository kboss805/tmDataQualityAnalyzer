using System;
using System.Collections.Generic;
using Avalonia.Media;

namespace TmDq.App.Plot
{
    /// <summary>
    /// The series colours of US4.0: Frame Sync Lock in purple, blue and green, one per stream; Receiver SNR in red,
    /// orange and yellow, one per receiver; and beyond the primaries, progressively lighter shades of them. As the Qt
    /// <c>PlotViewModel::assignColors</c> and <c>shadeOfColor</c> do it, through QColor's own integer HSV arithmetic,
    /// so a shade is the very colour the Qt application draws (held to a table Qt printed, in the tests).
    /// </summary>
    public static class PlotColors
    {
        /// <summary>The Frame Sync Lock primaries (<c>PlotConstants::kFrameSyncLockPrimaryColors</c>).</summary>
        public static readonly IReadOnlyList<Color> FrameSyncLock = new[]
        {
            Color.FromRgb(106, 13, 173),   // purple
            Color.FromRgb(67, 97, 238),    // blue
            Color.FromRgb(46, 184, 92),    // green
        };

        /// <summary>The Receiver SNR primaries (<c>PlotConstants::kSnrPrimaryColors</c>).</summary>
        public static readonly IReadOnlyList<Color> Snr = new[]
        {
            Color.FromRgb(204, 0, 0),      // red
            Color.FromRgb(255, 140, 0),    // orange
            Color.FromRgb(230, 200, 30),   // yellow
        };

        /// <summary><paramref name="color"/> made lighter <paramref name="level"/> times: per level the saturation
        /// falls by 60 (not below 40) and the value rises by 20 (not above 255). Level 0 or below is the colour itself.</summary>
        public static Color Shade(Color color, int level)
        {
            if (level <= 0) return color;
            (int h, int s, int v) = ToQtHsv(color);
            s = Math.Max(40, s - level * 60);
            v = Math.Min(255, v + level * 20);
            return FromQtHsv(h, s, v);
        }

        // QColor keeps 16 bits a channel: an 8-bit value is stored times 0x101 and read back through qt_div_257.
        private const double Max16 = 65535.0;

        private static int Div257(int x) => (x - (x >> 8) + 0x80) >> 8;

        // qRound; every value rounded here is non-negative.
        private static int QRound(double d) => d >= 0.0 ? (int)(d + 0.5) : (int)(d - 0.5);

        /// <summary>QColor::getHsv of an RGB colour: hue 0 to 359 (-1 for no hue), saturation and value 0 to 255.</summary>
        private static (int H, int S, int V) ToQtHsv(Color c)
        {
            double r = c.R * 0x101 / Max16, g = c.G * 0x101 / Max16, b = c.B * 0x101 / Max16;
            double max = Math.Max(r, Math.Max(g, b));
            double min = Math.Min(r, Math.Min(g, b));
            double delta = max - min;
            int value = QRound(max * Max16);
            int hue, saturation;
            if (Math.Abs(delta) <= 0.000000000001)
            {
                hue = ushort.MaxValue;   // achromatic
                saturation = 0;
            }
            else
            {
                saturation = QRound(delta / max * Max16);
                double h;
                if (r == max) h = (g - b) / delta;
                else if (g == max) h = 2.0 + (b - r) / delta;
                else h = 4.0 + (r - g) / delta;
                h *= 60.0;
                if (h < 0.0) h += 360.0;
                hue = QRound(h * 100);
            }
            return (hue == ushort.MaxValue ? -1 : hue / 100, Div257(saturation), Div257(value));
        }

        /// <summary>QColor::setHsv, read back as RGB.</summary>
        private static Color FromQtHsv(int h, int s, int v)
        {
            int hue = h == -1 ? ushort.MaxValue : (h % 360) * 100;
            int saturation = s * 0x101, value = v * 0x101;
            if (saturation == 0 || hue == ushort.MaxValue)
            {
                byte grey = (byte)Div257(value);
                return Color.FromRgb(grey, grey, grey);
            }
            double hh = hue == 36000 ? 0 : hue / 6000.0;
            double ss = saturation / Max16;
            double vv = value / Max16;
            int i = (int)hh;
            double f = hh - i;
            double p = vv * (1.0 - ss);
            double r, g, b;
            if ((i & 1) != 0)
            {
                double q = vv * (1.0 - ss * f);
                (r, g, b) = i switch
                {
                    1 => (q, vv, p),
                    3 => (p, q, vv),
                    _ => (vv, p, q),
                };
            }
            else
            {
                double t = vv * (1.0 - ss * (1.0 - f));
                (r, g, b) = i switch
                {
                    0 => (vv, t, p),
                    2 => (p, vv, t),
                    _ => (t, p, vv),
                };
            }
            return Color.FromRgb((byte)Div257(QRound(r * Max16)), (byte)Div257(QRound(g * Max16)), (byte)Div257(QRound(b * Max16)));
        }
    }
}
