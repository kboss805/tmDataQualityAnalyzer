using System;
using System.Collections.Generic;

namespace TmDq.Core;

/// <summary>The outcome of matching a template's PCM channel ids against a file's.</summary>
/// <param name="Ok">The two sets are identical.</param>
/// <param name="Missing">Ids the template needs and the file lacks (sorted, each once).</param>
/// <param name="Extra">Ids the file has and the template does not (sorted, each once).</param>
public sealed record TemplateMatchResult(bool Ok, IReadOnlyList<int> Missing, IReadOnlyList<int> Extra);

/// <summary>Whether a recording has exactly the PCM channels a processing template was made for.</summary>
public static unsafe class TemplateMatcher
{
    /// <summary>Compares two channel-id sets exactly: order and repeats do not matter, and two empty sets match.</summary>
    public static TemplateMatchResult Match(IReadOnlyList<int> templateIds, IReadOnlyList<int> fileIds)
    {
        Library.RequireLoaded();
        ArgumentNullException.ThrowIfNull(templateIds);
        ArgumentNullException.ThrowIfNull(fileIds);

        using var arena = new Arena();
        int* template = Copy(arena, templateIds);
        int* file = Copy(arena, fileIds);
        Library.Check(Native.tmdq_template_match(template, templateIds.Count, file, fileIds.Count, out IntPtr match));
        try
        {
            MatchInfoNative info = default;
            info.StructSize = (uint)sizeof(MatchInfoNative);
            Library.Check(Native.tmdq_match_info_get(match, &info));
            return new TemplateMatchResult(
                info.Ok != 0,
                new ReadOnlySpan<int>(info.Missing, info.MissingCount).ToArray(),
                new ReadOnlySpan<int>(info.Extra, info.ExtraCount).ToArray());
        }
        finally
        {
            Native.tmdq_match_free(match);
        }
    }

    private static int* Copy(Arena arena, IReadOnlyList<int> ids)
    {
        if (ids.Count == 0)
        {
            return null;
        }

        int* copy = arena.Alloc<int>(ids.Count);
        for (int i = 0; i < ids.Count; i++)
        {
            copy[i] = ids[i];
        }
        return copy;
    }
}
