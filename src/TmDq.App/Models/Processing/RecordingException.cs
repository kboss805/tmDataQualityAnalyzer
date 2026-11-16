using System;

namespace TmDq.App.Models.Processing;

/// <summary>A recording the core could not process, with the reason in words for the user.</summary>
public class RecordingException : Exception
{
    public RecordingException(string message) : base(message) { }

    public RecordingException(string message, Exception inner) : base(message, inner) { }
}
