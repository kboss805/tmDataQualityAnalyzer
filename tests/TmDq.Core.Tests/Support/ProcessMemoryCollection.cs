using Xunit;

namespace TmDq.Core.Tests;

/// <summary>
/// The test classes with a test that reads the process's private memory before and after a loop, to see that native
/// memory is given back. That number is the whole process's, so any other test allocating at the same moment (xunit
/// runs classes in parallel) is counted as a leak: adding two unrelated tests once made one of these fail every run.
/// They share a collection that does not run in parallel with anything, so xunit runs them one at a time, after the
/// parallel ones have finished. That costs about 8 seconds of the suite's 30.
/// </summary>
[CollectionDefinition(Name, DisableParallelization = true)]
public sealed class ProcessMemoryCollection
{
    public const string Name = "Process memory";
}
