using System.Collections.Generic;
using TmDq.Core;

namespace TmDq.App.Models.Processing;

/// <summary>The channels a Chapter 10 recording offers, as Extract Calibration lists them for the calibration recording:
/// its time channels, its PCM channels and the time channel the application selects first (-1 when there is none).
/// A calibration recording is often a different file from the one the stream was set up on, and the step cals can be on
/// another channel of it, so the dialog lets the operator choose.</summary>
public sealed record RecordingChannels(IReadOnlyList<ChannelInfo> Time, IReadOnlyList<ChannelInfo> Pcm, int DefaultTimeChannel)
{
    /// <summary>Opens <paramref name="path"/> through the core and lists its channels (TMATS only: no data is read).</summary>
    /// <exception cref="RecordingException">The file would not open, or has no PCM channel.</exception>
    public static RecordingChannels Read(string path)
    {
        Ch10File file = StreamsRun.Open(path);
        return new RecordingChannels(file.TimeChannels, file.PcmChannels, file.DefaultTimeChannel);
    }
}
