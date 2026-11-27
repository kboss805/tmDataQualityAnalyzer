using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Streams;
using TmDq.App.Services;
using TmDq.Core;

namespace TmDq.App.Tests;

/// <summary>
/// Configure Streams answered without its dialog: cancelled, a run given whatever the recording, a run chosen from
/// the recording, or an answer that waits on a task the test settles.
/// </summary>
internal sealed class FakeConfigurator : IStreamConfigurator
{
    private readonly Func<string, Ch10File, Task<StreamsChoice?>> _answer;

    private FakeConfigurator(Func<string, Ch10File, Task<StreamsChoice?>> answer) => _answer = answer;

    /// <summary>Configure Streams cancelled.</summary>
    public static FakeConfigurator Cancelled() => new((_, _) => Task.FromResult<StreamsChoice?>(null));

    /// <summary>What the dialog settled, whatever the recording.</summary>
    public static FakeConfigurator Settled(StreamsChoice choice) => new((_, _) => Task.FromResult<StreamsChoice?>(choice));

    /// <summary>The run a test gives, with no rows and the first time channel, whatever the recording.</summary>
    public static FakeConfigurator Given(RunRequest request) => Settled(new StreamsChoice(request, Array.Empty<StreamSetup>(), 0));

    /// <summary>What <paramref name="choose"/> makes of the recording's path and its channels.</summary>
    public static FakeConfigurator Choosing(Func<string, Ch10File, StreamsChoice> choose) =>
        new((path, file) => Task.FromResult<StreamsChoice?>(choose(path, file)));

    /// <summary>Up until <paramref name="answer"/> completes: the dialog still open. <paramref name="onAsked"/> is called when
    /// the dialog is asked for, so a test can wait for that instead of polling for what the run logged before it.</summary>
    public static FakeConfigurator Waiting(Task<StreamsChoice?> answer, Action? onAsked = null) =>
        new((_, _) =>
        {
            onAsked?.Invoke();
            return answer;
        });

    /// <summary>Whatever <paramref name="answer"/> gives for the recording, null (a cancel) included: a test that changes its
    /// mind between runs (the defaults, then a cancel) closes over its own state.</summary>
    public static FakeConfigurator From(Func<string, Ch10File, Task<StreamsChoice?>> answer) => new(answer);

    /// <summary>The run with no dialog (<see cref="FirstChannelRun"/>) for every recording but Safran's sample, which is run as
    /// 4.5's oracle runs it (<see cref="StreamsChoices.Safran"/>): the two together are recordings of different days.</summary>
    public static FakeConfigurator SafranOrDefault() =>
        new((path, file) => Path.GetFileName(path).StartsWith("safran", StringComparison.Ordinal)
            ? Task.FromResult<StreamsChoice?>(StreamsChoices.Safran(path, file))
            : new FirstChannelRun().ConfigureAsync(path, file));

    public Task<StreamsChoice?> ConfigureAsync(string path, Ch10File file) => _answer(path, file);
}

/// <summary>Open's file dialog: gives the queued answers in turn, then <c>answer</c> (null is a cancel), and counts how often it
/// was asked.</summary>
internal sealed class FakeRecordingPicker : IRecordingPicker
{
    private readonly string? _answer;

    public FakeRecordingPicker(string? answer = null) => _answer = answer;

    public System.Collections.Generic.Queue<string?> Answers { get; } = new();

    public int Asked { get; private set; }

    public Task<string?> PickRecordingAsync()
    {
        Asked++;
        return Task.FromResult(Answers.Count > 0 ? Answers.Dequeue() : _answer);
    }
}

/// <summary>Recordings the tests make from the committed ones.</summary>
internal static class Recordings
{
    /// <summary>
    /// The first packets of the PRN sample, up to <paramref name="maxBytes"/>, cut where a packet starts, written to
    /// <paramref name="path"/>: a recording of the same streams and clock that processes in a fraction of the time, for tests that
    /// need a recording that processes rather than these numbers (Open stacking, imports kept). Every packet starts with the
    /// Chapter 10 sync (0xEB25) and gives its length at byte 4, so the walk checks each start and refuses a file it cannot follow.
    /// </summary>
    public static string WritePrnPrefix(string path, long maxBytes)
    {
        byte[] bytes = File.ReadAllBytes(Repo.Recording("prn_testfile, sample.ch10"));
        long at = 0;
        while (at + 8 <= bytes.Length)
        {
            if (bytes[at] != 0x25 || bytes[at + 1] != 0xEB)
                throw new InvalidOperationException($"no packet sync at byte {at} of the PRN sample");
            long next = at + BitConverter.ToUInt32(bytes, (int)at + 4);
            if (next > maxBytes || next > bytes.Length) break;
            at = next;
        }
        if (at == 0) throw new InvalidOperationException("the PRN sample's first packet is longer than " + maxBytes + " bytes");
        using (var file = File.Create(path)) file.Write(bytes, 0, (int)at);
        return path;
    }

    /// <summary>
    /// The PRN sample with CH-04 retyped as a time channel in TMATS (<c>R-1\CDT-9:PCMIN</c> to <c>11</c>, a
    /// same-length edit, as the native reader's tests make it), written to <paramref name="path"/>: two time
    /// channels (1 and 16) and three PCM ones.
    /// </summary>
    public static string WriteTwoTimeChannels(string path)
    {
        byte[] bytes = File.ReadAllBytes(Repo.Recording("prn_testfile, sample.ch10"));
        byte[] from = Encoding.ASCII.GetBytes(@"R-1\CDT-9:PCMIN;"), to = Encoding.ASCII.GetBytes(@"R-1\CDT-9:11   ;");
        int at = bytes.AsSpan().IndexOf(from);
        if (at < 0 || bytes.AsSpan(at + 1).IndexOf(from) >= 0)
            throw new InvalidOperationException("CH-04's channel-type key is not in the PRN sample exactly once");
        to.CopyTo(bytes, at);
        File.WriteAllBytes(path, bytes);
        return path;
    }
}

/// <summary>Configure Streams' answers the tests give for recordings the defaults do not process.</summary>
internal static class StreamsChoices
{
    /// <summary>Safran's channel 13 (PCM03), PRN-15 recorded in the other byte order (Legacy Chapter 10 on), over the recording's
    /// default time channel, as 4.5's oracle runs it.</summary>
    public static StreamsChoice Safran(string path, Ch10File file, int timeChannelIndex = 0)
    {
        var stream = new StreamConfig
        {
            PcmChannelId = 13, Label = "PCM03", Mode = StreamMode.FrameSyncLock, SyncPattern = "334AABBF", SyncMask = "FFFFFFFF",
            BitsInMinorFrame = 32767, SamplePeriod = FrameSyncDefaults.Period,
        };
        var request = new RunRequest(path, file.DefaultTimeChannel, new[] { stream }) { SwapBytes = false };
        return new StreamsChoice(request, Array.Empty<StreamSetup>(), timeChannelIndex);
    }
}
