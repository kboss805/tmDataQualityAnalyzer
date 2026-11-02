using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace TmDq.App.Settings
{
    /// <summary>
    /// The Qt application's preferences, read once from its QSettings INI file (plan step 4.8: existing users'
    /// preferences migrate). The keys are the Qt application's: <c>Theme</c>, <c>SidebarVisible</c>,
    /// <c>LegendVisible</c>, <c>RecentFiles</c>, <c>LastCh10Directory</c> and <c>CalibrationSetupDialog/lastCalDir</c>.
    /// <c>LastTomlDirectory</c> is not carried over: the Qt application never wrote it, and the setup dialogs' Load
    /// starts in the shipped settings folders (plan step 4.1b).
    /// </summary>
    public static class QtSettingsImport
    {
        /// <summary>The preferences the INI file at <paramref name="path"/> holds, each missing key at its default.</summary>
        public static Preferences Read(string path)
        {
            IReadOnlyDictionary<string, QtIniValue> ini;
            try
            {
                ini = QtIni.Parse(File.ReadAllBytes(path));
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException)
            {
                return new Preferences();
            }
            var defaults = new Preferences();
            return new Preferences
            {
                Theme = ini.TryGetValue("Theme", out QtIniValue? theme) ? theme.AsString() : defaults.Theme,
                SidebarVisible = ini.TryGetValue("SidebarVisible", out QtIniValue? sidebar) ? sidebar.AsBool() : defaults.SidebarVisible,
                LegendVisible = ini.TryGetValue("LegendVisible", out QtIniValue? legend) ? legend.AsBool() : defaults.LegendVisible,
                RecentFiles = ini.TryGetValue("RecentFiles", out QtIniValue? recent) ? recent.AsList().Select(Native).ToList() : defaults.RecentFiles,
                LastFolder = ini.TryGetValue("LastCh10Directory", out QtIniValue? folder) ? Folder(folder) : null,
                LastCalibrationFolder = ini.TryGetValue("CalibrationSetupDialog/lastCalDir", out QtIniValue? cal) ? Folder(cal) : null,
            };
        }

        private static string? Folder(QtIniValue value) => value.AsString() is { Length: > 0 } s ? Native(s) : null;

        // Qt writes paths with forward slashes; a full path is given Windows' own form, anything else is kept as written.
        private static string Native(string path) => Path.IsPathFullyQualified(path) ? Path.GetFullPath(path) : path;
    }

    /// <summary>One value of a QSettings INI file: a string, a list of strings, or nothing (<c>@Invalid()</c>).</summary>
    public sealed class QtIniValue
    {
        private readonly string? _single;
        private readonly IReadOnlyList<string>? _list;

        private QtIniValue(string? single, IReadOnlyList<string>? list)
        {
            _single = single;
            _list = list;
        }

        public static QtIniValue String(string s) => new(s, null);

        public static QtIniValue List(IReadOnlyList<string> items) => new(null, items);

        public static readonly QtIniValue Invalid = new(null, null);

        public bool IsList => _list is not null;

        /// <summary>QVariant's <c>toString</c>: the string, a list of one, else empty.</summary>
        public string AsString() => _single ?? (_list is { Count: 1 } ? _list[0] : "");

        /// <summary>QVariant's <c>toStringList</c>: a string is a list of one, nothing is empty.</summary>
        public IReadOnlyList<string> AsList() => _list ?? (_single is null ? Array.Empty<string>() : new[] { _single });

        /// <summary>QVariant's <c>toBool</c> of a string: false for "", "0" and "false" in any case, else true.</summary>
        public bool AsBool()
        {
            string s = AsString();
            return !(s.Length == 0 || s == "0" || s.Equals("false", StringComparison.OrdinalIgnoreCase));
        }
    }

    /// <summary>
    /// The QSettings INI format, read as Qt 6 reads it: UTF-8; <c>[General]</c> (in any case) is the top level and any
    /// other section prefixes its keys ("Section/key"); a key's <c>%XX</c> and <c>%UXXXX</c> escapes; a value is a list
    /// when it holds a comma outside quotes; quotes keep spaces and commas; the backslash escapes
    /// (<c>\\ \" \n \t \xHHHH</c>, octal and the rest); <c>@Invalid()</c> is nothing and a leading <c>@@</c> is a
    /// literal "@". Lines starting with ';' are comments.
    /// </summary>
    public static class QtIni
    {
        public static IReadOnlyDictionary<string, QtIniValue> Parse(byte[] bytes)
        {
            string text = new UTF8Encoding(false).GetString(bytes).TrimStart('﻿');
            var values = new Dictionary<string, QtIniValue>(StringComparer.Ordinal);
            string prefix = "";
            foreach (string raw in text.Split('\n'))
            {
                string line = raw.TrimEnd('\r').Trim(' ', '\t');
                if (line.Length == 0 || line[0] == ';') continue;
                if (line[0] == '[')
                {
                    // As QSettings reads a section's name: "General" in any case is the top level; "%General" is a
                    // section called General; any other name is unescaped as a key is.
                    int close = line.IndexOf(']');
                    string section = (close < 0 ? line[1..] : line[1..close]).Trim(' ', '\t');
                    prefix = section.Equals("General", StringComparison.OrdinalIgnoreCase) ? ""
                        : section.Equals("%General", StringComparison.OrdinalIgnoreCase) ? section[1..] + "/"
                        : UnescapeKey(section) + "/";
                    continue;
                }
                int eq = line.IndexOf('=');
                if (eq <= 0) continue;
                string key = UnescapeKey(line[..eq].Trim(' ', '\t'));
                values[prefix + key] = Value(line[(eq + 1)..]);
            }
            return values;
        }

        /// <summary>A key as QSettings escapes it: "%XX" and "%UXXXX" are characters, a backslash is a slash.</summary>
        public static string UnescapeKey(string key)
        {
            var s = new StringBuilder();
            for (int i = 0; i < key.Length; i++)
            {
                char c = key[i];
                if (c == '\\')
                {
                    s.Append('/');
                }
                else if (c == '%' && i + 5 < key.Length && key[i + 1] == 'U' && IsHex(key.AsSpan(i + 2, 4)))
                {
                    s.Append((char)Convert.ToInt32(key.Substring(i + 2, 4), 16));
                    i += 5;
                }
                else if (c == '%' && i + 2 < key.Length && IsHex(key.AsSpan(i + 1, 2)))
                {
                    s.Append((char)Convert.ToInt32(key.Substring(i + 1, 2), 16));
                    i += 2;
                }
                else
                {
                    s.Append(c);
                }
            }
            return s.ToString();
        }

        private static bool IsHex(ReadOnlySpan<char> s)
        {
            foreach (char c in s)
                if (!Uri.IsHexDigit(c)) return false;
            return true;
        }

        /// <summary>A value, as QSettings' <c>iniUnescapedStringList</c> and <c>stringToVariant</c> read it.</summary>
        public static QtIniValue Value(string raw)
        {
            var items = new List<string>();
            var current = new StringBuilder();
            bool isList = false, inQuotes = false, quoted = false;
            int chopLimit = 0;
            int i = 0;
            SkipSpaces();
            while (i < raw.Length)
            {
                char c = raw[i];
                if (c == '\\')
                {
                    i++;
                    if (i >= raw.Length) break;
                    char e = raw[i++];
                    switch (e)
                    {
                        case 'a': current.Append('\a'); break;
                        case 'b': current.Append('\b'); break;
                        case 'f': current.Append('\f'); break;
                        case 'n': current.Append('\n'); break;
                        case 'r': current.Append('\r'); break;
                        case 't': current.Append('\t'); break;
                        case 'v': current.Append('\v'); break;
                        case '"': case '?': case '\'': case '\\': current.Append(e); break;
                        case 'x':
                        {
                            int value = 0, digits = 0;
                            while (i < raw.Length && Uri.IsHexDigit(raw[i])) { value = value * 16 + Convert.ToInt32(raw[i].ToString(), 16); i++; digits++; }
                            if (digits > 0) current.Append((char)value);
                            break;
                        }
                        case >= '0' and <= '7':
                        {
                            int value = e - '0';
                            while (i < raw.Length && raw[i] is >= '0' and <= '7') { value = value * 8 + (raw[i] - '0'); i++; }
                            current.Append((char)value);
                            break;
                        }
                        default: break;   // an unknown escape drops the character, as Qt's does
                    }
                    chopLimit = current.Length;
                }
                else if (c == '"')
                {
                    i++;
                    quoted = true;
                    inQuotes = !inQuotes;
                    if (!inQuotes) SkipSpaces();
                }
                else if (c == ',' && !inQuotes)
                {
                    if (!quoted) ChopTrailingSpaces();
                    isList = true;
                    items.Add(current.ToString());
                    current.Clear();
                    chopLimit = 0;
                    quoted = false;
                    i++;
                    SkipSpaces();
                }
                else
                {
                    current.Append(c);
                    i++;
                }
            }
            if (!quoted) ChopTrailingSpaces();
            if (isList)
            {
                items.Add(current.ToString());
                return QtIniValue.List(items.Select(Literal).ToList());
            }
            string single = current.ToString();
            return single == "@Invalid()" ? QtIniValue.Invalid : QtIniValue.String(Literal(single));

            void SkipSpaces()
            {
                while (i < raw.Length && raw[i] is ' ' or '\t') i++;
                chopLimit = current.Length;
            }

            void ChopTrailingSpaces()
            {
                int n = current.Length;
                while (n > chopLimit && current[n - 1] is ' ' or '\t') n--;
                current.Length = n;
            }
        }

        // A string QSettings had to escape because it began with '@' is written with a second one.
        private static string Literal(string s) => s.StartsWith("@@", StringComparison.Ordinal) ? s[1..] : s;
    }
}
