using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using Avalonia.Media;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Streams;
using TmDq.Core;

namespace TmDq.App.Models.Templates
{
    /// <summary>A series' name and colour, kept in a template so a batch run's series look as they did when it was saved.</summary>
    /// <param name="ReceiverIndex">The receiver (1-based) for an SNR series, 0 otherwise.</param>
    /// <param name="ChannelIndex">The channel within the receiver for an SNR series, 0 otherwise.</param>
    public sealed record SeriesAppearance(MetricType Metric, int ReceiverIndex, int ChannelIndex, string Name, Color Color);

    /// <summary>One stream of a template: its settings (a Configure Streams row) and its series' looks.</summary>
    public sealed record TemplateEntry(StreamSetup Config, IReadOnlyList<SeriesAppearance> Appearance);

    /// <summary>
    /// A processing template (US1.1): every row of one recording's Configure Streams, the
    /// time channel's place in its list, the byte order, and the series' names and colours. No file paths of recordings
    /// and no calibration profiles, only what a calibration was extracted from.
    /// </summary>
    public sealed record ProcessingTemplate(string AppVersion, string Name, int TimeChannelIndex, bool SwapBytes,
                                            IReadOnlyList<TemplateEntry> Entries)
    {
        public int SchemaVersion { get; init; } = TemplateFile.CurrentSchemaVersion;
    }

    /// <summary>What reading a template found.</summary>
    public enum TemplateLoad
    {
        Loaded,
        /// <summary>Not JSON at all; the message says why.</summary>
        InvalidJson,
        /// <summary>No schema version, or one newer than this application's.</summary>
        UnsupportedSchemaVersion,
        /// <summary>JSON, but not a template (not an object, or no entries array).</summary>
        InvalidFormat,
    }

    /// <summary>
    /// The template file (plan step 4.7, decision D3: template JSON is C#'s), read and written as v2.14.2 read
    /// and wrote it, so a template either application wrote loads in the other. Reading keeps v2.x's rules: a key
    /// that is missing or of the wrong type keeps its default, an entry without a channel id and a label is skipped,
    /// the sample period and slope indices are clamped, and a template from before the byte order was kept reads as
    /// swapped. Writing gives v2.14.2's bytes: keys sorted, four spaces an indent, whole numbers without a point,
    /// CR LF, UTF-8 without a byte-order mark. TemplateTests holds both to three templates v2.14.2 wrote.
    /// </summary>
    public static class TemplateFile
    {
        public const int CurrentSchemaVersion = 1;
        public const int MaxSamplePeriodIndex = 2, MaxSlopeIndex = 3;
        private const string LockMode = "FrameSyncLock", ReceiverMode = "ReceiverChannelInfo";

        // ---- reading ---------------------------------------------------------------------------------------------------

        /// <summary>Reads a template from <paramref name="text"/>; <paramref name="error"/> is the parser's word for invalid JSON.</summary>
        public static TemplateLoad Parse(string text, out ProcessingTemplate? template, out string error)
        {
            template = null;
            error = string.Empty;
            JsonDocument document;
            try
            {
                document = JsonDocument.Parse(text);
            }
            catch (JsonException e)
            {
                error = e.Message;
                return TemplateLoad.InvalidJson;
            }
            using (document)
            {
                JsonElement root = document.RootElement;
                if (root.ValueKind != JsonValueKind.Object) return TemplateLoad.InvalidFormat;
                if (!root.TryGetProperty("schemaVersion", out JsonElement version) || version.ValueKind != JsonValueKind.Number)
                    return TemplateLoad.UnsupportedSchemaVersion;
                int schemaVersion = Int(root, "schemaVersion", 0);
                if (schemaVersion > CurrentSchemaVersion) return TemplateLoad.UnsupportedSchemaVersion;
                if (!root.TryGetProperty("entries", out JsonElement entries) || entries.ValueKind != JsonValueKind.Array)
                    return TemplateLoad.InvalidFormat;

                var read = new List<TemplateEntry>();
                foreach (JsonElement entry in entries.EnumerateArray())
                {
                    if (entry.ValueKind != JsonValueKind.Object) continue;
                    if (!entry.TryGetProperty("config", out JsonElement config) || config.ValueKind != JsonValueKind.Object) continue;
                    if (ReadConfig(config) is not { } setup) continue;
                    var looks = new List<SeriesAppearance>();
                    if (entry.TryGetProperty("appearance", out JsonElement appearance) && appearance.ValueKind == JsonValueKind.Array)
                        foreach (JsonElement a in appearance.EnumerateArray())
                            if (a.ValueKind == JsonValueKind.Object) looks.Add(ReadAppearance(a));
                    read.Add(new TemplateEntry(setup, looks));
                }
                template = new ProcessingTemplate(Str(root, "appVersion", ""), Str(root, "name", ""), Int(root, "timeChannelIndex", 0),
                                                  Bool(root, "swapBytes", true), read) { SchemaVersion = schemaVersion };
                return TemplateLoad.Loaded;
            }
        }

        /// <summary>A row from a template's config, by the reading rules above; null without a channel id and a
        /// label.</summary>
        private static StreamSetup? ReadConfig(JsonElement json)
        {
            if (!json.TryGetProperty("pcmChannelId", out _) || !json.TryGetProperty("label", out _)) return null;
            StreamMode mode = Str(json, "mode", LockMode) == LockMode ? StreamMode.FrameSyncLock : StreamMode.ReceiverChannelInfo;
            string pattern = FrameSyncDefaults.Pattern, mask = FrameSyncDefaults.Mask;
            int bits = FrameSyncDefaults.BitsInMinorFrame;
            bool randomized = false, inverted = false;
            double rate = 0.0;
            if (json.TryGetProperty("sync", out JsonElement sync) && sync.ValueKind == JsonValueKind.Object)
            {
                pattern = Str(sync, "pattern", pattern);
                mask = Str(sync, "mask", mask);
                bits = Int(sync, "bitsInMinorFrame", bits);
                randomized = Bool(sync, "randomized", randomized);
                inverted = Bool(sync, "inverted", inverted);
                rate = Double(sync, "dataRateMbps", rate);
            }
            string calCh10 = "", stepToml = "";
            double clipStart = 0.0, clipEnd = 0.0;
            if (json.TryGetProperty("calibration", out JsonElement cal) && cal.ValueKind == JsonValueKind.Object)
            {
                calCh10 = Str(cal, "calCh10Path", calCh10);
                stepToml = Str(cal, "stepTomlPath", stepToml);
                clipStart = Double(cal, "clipStartSec", clipStart);
                clipEnd = Double(cal, "clipEndSec", clipEnd);
            }
            return new StreamSetup
            {
                PcmChannelId = Int(json, "pcmChannelId", -1),
                Label = Str(json, "label", ""),
                Process = Bool(json, "process", false),
                Mode = mode,
                SettingsMode = mode,
                Configured = true,
                SyncPattern = pattern,
                SyncMask = mask,
                BitsInMinorFrame = bits,
                Randomized = randomized,
                Inverted = inverted,
                DataRateMbps = rate,
                // Indices a newer or hand-edited template could carry out of range are clamped, as v2.x clamped them.
                SamplePeriod = (SamplePeriod)Math.Clamp(Int(json, "samplePeriodIndex", 0), 0, MaxSamplePeriodIndex),
                TmatsDataRateMbps = Double(json, "tmatsDataRateMbps", 0.0),
                PolarityIndex = Int(json, "polarityIndex", 0),
                SlopeIndex = Math.Clamp(Int(json, "slopeIndex", 3), 0, MaxSlopeIndex),
                ScaleDdBPerV = Double(json, "scaleDdBPerV", 20.0),
                NumReceivers = Int(json, "numReceivers", 16),
                ReceiverChannels = Int(json, "receiverChannels", 3),
                ReceiverParamsToml = Str(json, "receiverParamsToml", ""),
                CalCh10Path = calCh10,
                StepTomlPath = stepToml,
                ClipStartSec = clipStart,
                ClipEndSec = clipEnd,
            };
        }

        private static SeriesAppearance ReadAppearance(JsonElement json)
        {
            MetricType metric = Str(json, "metricType", "") switch
            {
                "FrameSyncLock" => MetricType.FrameSyncLock,
                "AccumulatedMissedFrames" => MetricType.AccumulatedMissedFrames,
                _ => MetricType.Snr,
            };
            // A colour is hex ("#aarrggbb", "#rrggbb") or a name; anything else reads as black, as v2.x read it.
            Color color = Color.TryParse(Str(json, "color", ""), out Color c) ? c : Colors.Black;
            return new SeriesAppearance(metric, Int(json, "receiverIndex", 0), Int(json, "channelIndex", 0), Str(json, "name", ""), color);
        }

        // Typed reads, as v2.x's: the default unless the value is of the type (for an int, a whole number in range).
        private static int Int(JsonElement o, string key, int fallback) =>
            o.TryGetProperty(key, out JsonElement v) && v.ValueKind == JsonValueKind.Number && v.TryGetDouble(out double d) &&
            d == Math.Floor(d) && d >= int.MinValue && d <= int.MaxValue ? (int)d : fallback;

        private static double Double(JsonElement o, string key, double fallback) =>
            o.TryGetProperty(key, out JsonElement v) && v.ValueKind == JsonValueKind.Number ? v.GetDouble() : fallback;

        private static bool Bool(JsonElement o, string key, bool fallback) =>
            o.TryGetProperty(key, out JsonElement v) && v.ValueKind is JsonValueKind.True or JsonValueKind.False ? v.GetBoolean() : fallback;

        private static string Str(JsonElement o, string key, string fallback) =>
            o.TryGetProperty(key, out JsonElement v) && v.ValueKind == JsonValueKind.String ? v.GetString()! : fallback;

        /// <summary>Reads the template at <paramref name="path"/>.</summary>
        /// <exception cref="IOException">The file cannot be read.</exception>
        public static TemplateLoad Read(string path, out ProcessingTemplate? template, out string error) =>
            Parse(File.ReadAllText(path, Encoding.UTF8), out template, out error);

        // ---- writing ---------------------------------------------------------------------------------------------------

        /// <summary>The template as v2.14.2 wrote it, with "\n" line ends (<see cref="Write"/> makes them
        /// CR LF).</summary>
        public static string ToJson(ProcessingTemplate template)
        {
            var root = new SortedDictionary<string, object>(StringComparer.Ordinal)
            {
                ["schemaVersion"] = CurrentSchemaVersion,
                ["appVersion"] = template.AppVersion,
                ["name"] = template.Name,
                ["timeChannelIndex"] = template.TimeChannelIndex,
                ["swapBytes"] = template.SwapBytes,
                ["entries"] = template.Entries.Select(EntryToJson).ToList<object>(),
            };
            var text = new StringBuilder();
            WriteValue(text, root, 0);
            return text.Append('\n').ToString();
        }

        /// <summary>Writes <paramref name="template"/> to <paramref name="path"/> as v2.14.2 wrote it.</summary>
        /// <exception cref="IOException">The file cannot be written.</exception>
        public static void Write(string path, ProcessingTemplate template) =>
            File.WriteAllText(path, ToJson(template).Replace("\n", "\r\n", StringComparison.Ordinal), new UTF8Encoding(false));

        private static object EntryToJson(TemplateEntry entry)
        {
            var o = new SortedDictionary<string, object>(StringComparer.Ordinal) { ["config"] = ConfigToJson(entry.Config) };
            // Appearance is left out entirely when nothing was captured.
            if (entry.Appearance.Count > 0)
                o["appearance"] = entry.Appearance.Select(a => (object)new SortedDictionary<string, object>(StringComparer.Ordinal)
                {
                    ["metricType"] = a.Metric switch
                    {
                        MetricType.FrameSyncLock => "FrameSyncLock",
                        MetricType.AccumulatedMissedFrames => "AccumulatedMissedFrames",
                        _ => "SNR",
                    },
                    ["receiverIndex"] = a.ReceiverIndex,
                    ["channelIndex"] = a.ChannelIndex,
                    ["name"] = a.Name,
                    ["color"] = string.Create(CultureInfo.InvariantCulture, $"#{a.Color.A:x2}{a.Color.R:x2}{a.Color.G:x2}{a.Color.B:x2}"),
                }).ToList();
            return o;
        }

        private static object ConfigToJson(StreamSetup s)
        {
            var o = new SortedDictionary<string, object>(StringComparer.Ordinal)
            {
                ["pcmChannelId"] = s.PcmChannelId,
                ["label"] = s.Label,
                ["process"] = s.Process,
                ["mode"] = s.Mode == StreamMode.FrameSyncLock ? LockMode : ReceiverMode,
                ["sync"] = new SortedDictionary<string, object>(StringComparer.Ordinal)
                {
                    ["pattern"] = s.SyncPattern,
                    ["mask"] = s.SyncMask,
                    ["bitsInMinorFrame"] = s.BitsInMinorFrame,
                    ["randomized"] = s.Randomized,
                    ["inverted"] = s.Inverted,
                    ["dataRateMbps"] = s.DataRateMbps,
                },
                ["samplePeriodIndex"] = (int)s.SamplePeriod,
                ["tmatsDataRateMbps"] = s.TmatsDataRateMbps,
                ["polarityIndex"] = s.PolarityIndex,
                ["slopeIndex"] = s.SlopeIndex,
                ["scaleDdBPerV"] = s.ScaleDdBPerV,
                ["numReceivers"] = s.NumReceivers,
                ["receiverChannels"] = s.ReceiverChannels,
                ["receiverParamsToml"] = s.ReceiverParamsToml,
            };
            // What a calibration was extracted from, never its profiles; and only when there was one.
            if (s.CalCh10Path.Length > 0)
                o["calibration"] = new SortedDictionary<string, object>(StringComparer.Ordinal)
                {
                    ["calCh10Path"] = s.CalCh10Path,
                    ["stepTomlPath"] = s.StepTomlPath,
                    ["clipStartSec"] = s.ClipStartSec,
                    ["clipEndSec"] = s.ClipEndSec,
                };
            return o;
        }

        /// <summary>The indented form v2.14.2 wrote: four spaces a level, "key": value, an empty array's bracket on the
        /// next line at the key's indent.</summary>
        private static void WriteValue(StringBuilder text, object value, int level)
        {
            string inner = new(' ', 4 * (level + 1)), outer = new(' ', 4 * level);
            switch (value)
            {
                case SortedDictionary<string, object> o:
                    text.Append("{\n");
                    int i = 0;
                    foreach ((string key, object v) in o)
                    {
                        text.Append(inner);
                        WriteString(text, key);
                        text.Append(": ");
                        WriteValue(text, v, level + 1);
                        text.Append(++i < o.Count ? ",\n" : "\n");
                    }
                    text.Append(outer).Append('}');
                    break;
                case List<object> a:
                    text.Append("[\n");
                    for (int j = 0; j < a.Count; j++)
                    {
                        text.Append(inner);
                        WriteValue(text, a[j], level + 1);
                        text.Append(j + 1 < a.Count ? ",\n" : "\n");
                    }
                    text.Append(outer).Append(']');
                    break;
                case string s:
                    WriteString(text, s);
                    break;
                case bool b:
                    text.Append(b ? "true" : "false");
                    break;
                case int n:
                    text.Append(n.ToString(CultureInfo.InvariantCulture));
                    break;
                case double d:
                    text.Append(Number(d));
                    break;
                default:
                    throw new ArgumentException("Not a JSON value: " + value.GetType().Name);
            }
        }

        /// <summary>A double as v2.14.2 wrote one: the shortest text that reads back as it, a whole number without
        /// a point.</summary>
        public static string Number(double d) => d.ToString("R", CultureInfo.InvariantCulture).Replace('E', 'e');

        /// <summary>A string as v2.14.2 escaped one: the quote, the backslash and the control characters; everything
        /// else as it is, in UTF-8.</summary>
        private static void WriteString(StringBuilder text, string s)
        {
            text.Append('"');
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"': text.Append("\\\""); break;
                    case '\\': text.Append("\\\\"); break;
                    case '\b': text.Append("\\b"); break;
                    case '\f': text.Append("\\f"); break;
                    case '\n': text.Append("\\n"); break;
                    case '\r': text.Append("\\r"); break;
                    case '\t': text.Append("\\t"); break;
                    default:
                        if (c < 0x20) text.Append("\\u").Append(((int)c).ToString("x4", CultureInfo.InvariantCulture));
                        else text.Append(c);
                        break;
                }
            }
            text.Append('"');
        }
    }
}
