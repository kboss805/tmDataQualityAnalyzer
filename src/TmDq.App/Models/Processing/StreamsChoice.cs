using System.Collections.Generic;
using TmDq.App.Models.Streams;
using TmDq.Core;

namespace TmDq.App.Models.Processing
{
    /// <summary>What Configure Streams settled: the run, every row (processed or not) and the time channel's place in its
    /// list, which a template captures (plan step 4.7).</summary>
    public sealed record StreamsChoice(RunRequest Request, IReadOnlyList<StreamSetup> Rows, int TimeChannelIndex);
}
