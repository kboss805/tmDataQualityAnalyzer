using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Streams;
using TmDq.App.Services;
using TmDq.Core;

namespace TmDq.App.Tests
{
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

        /// <summary>Up until <paramref name="answer"/> completes: the dialog still open.</summary>
        public static FakeConfigurator Waiting(Task<StreamsChoice?> answer) => new((_, _) => answer);

        public Task<StreamsChoice?> ConfigureAsync(string path, Ch10File file) => _answer(path, file);
    }

    /// <summary>Open's file dialog: gives <c>answer</c> (null is a cancel) and counts how often it was asked.</summary>
    internal sealed class FakeRecordingPicker : IRecordingPicker
    {
        private readonly string? _answer;

        public FakeRecordingPicker(string? answer = null) => _answer = answer;

        public int Asked { get; private set; }

        public Task<string?> PickRecordingAsync()
        {
            Asked++;
            return Task.FromResult(_answer);
        }
    }

    /// <summary>Recordings the tests make from the committed ones.</summary>
    internal static class Recordings
    {
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
}
