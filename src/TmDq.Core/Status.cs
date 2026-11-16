namespace TmDq.Core;

/// <summary>The numbers of tmdq_status.</summary>
public enum Status
{
    Ok = 0,
    BadArgument = 1,
    Prepare = 2,
    Process = 3,
    NoSamples = 4,
    Internal = 5,
    Job = 6,
    State = 7,
    Io = 8,
}
