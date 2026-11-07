using System;
using System.Collections.Generic;

namespace TmDq.Core;

/// <summary>One channel of a recording.</summary>
/// <param name="Id">The Chapter 10 channel id.</param>
/// <param name="Name">The TMATS name, without the channel number.</param>
/// <param name="TmatsDataRateBps">For a PCM channel, the bit rate TMATS declares (0 if it declares none); 0 for a time channel.</param>
public sealed record ChannelInfo(int Id, string Name, double TmatsDataRateBps);

/// <summary>
/// What a Chapter 10 recording offers: its time and PCM channels, in the order the application lists them.
/// Reading it touches the TMATS metadata only. Immutable; nothing native is held after <see cref="Open"/>.
/// </summary>
public sealed unsafe class Ch10File
{
    private Ch10File(IReadOnlyList<ChannelInfo> time, IReadOnlyList<ChannelInfo> pcm, int defaultTimeChannel)
    {
        TimeChannels = time;
        PcmChannels = pcm;
        DefaultTimeChannel = defaultTimeChannel;
    }

    /// <summary>The time channels.</summary>
    public IReadOnlyList<ChannelInfo> TimeChannels { get; }

    /// <summary>The PCM channels.</summary>
    public IReadOnlyList<ChannelInfo> PcmChannels { get; }

    /// <summary>The time channel the application selects first, or -1 if the file has none.</summary>
    public int DefaultTimeChannel { get; }

    /// <summary>Opens <paramref name="path"/> and reads its channels. Safe to call from several threads.</summary>
    /// <exception cref="TmdqException">The file would not open, or its TMATS did not resolve (<see cref="Status.Prepare"/>).</exception>
    public static Ch10File Open(string path)
    {
        Library.RequireLoaded();
        ArgumentNullException.ThrowIfNull(path);

        using var arena = new Arena();
        Library.Check(Native.tmdq_file_open(arena.Utf8(path), out IntPtr handle));
        try
        {
            return new Ch10File(
                ReadChannels(handle, Native.tmdq_file_time_channel_count(handle), isPcm: false),
                ReadChannels(handle, Native.tmdq_file_pcm_channel_count(handle), isPcm: true),
                Native.tmdq_file_default_time_channel(handle));
        }
        finally
        {
            Native.tmdq_file_close(handle);
        }
    }

    private static ChannelInfo[] ReadChannels(IntPtr handle, int count, bool isPcm)
    {
        var channels = new ChannelInfo[count];
        for (int i = 0; i < count; i++)
        {
            ChannelInfoNative info = default;
            info.StructSize = (uint)sizeof(ChannelInfoNative);
            Library.Check(isPcm
                ? Native.tmdq_file_pcm_channel(handle, i, &info)
                : Native.tmdq_file_time_channel(handle, i, &info));
            channels[i] = new ChannelInfo(info.ChannelId, Text.Read(info.NameUtf8), info.TmatsDataRateBps);
        }
        return channels;
    }
}
