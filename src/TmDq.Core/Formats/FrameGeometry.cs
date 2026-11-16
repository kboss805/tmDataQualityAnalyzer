namespace TmDq.Core;

/// <summary>The minor frame's arithmetic, as the core does it.</summary>
public static class FrameGeometry
{
    /// <summary>Words in a minor frame of <paramref name="bitsInMinorFrame"/> bits, rounded up: a partly filled last
    /// word still takes a slot.</summary>
    public static int WordsInMinorFrame(int bitsInMinorFrame)
    {
        Library.RequireLoaded();
        return Native.tmdq_words_in_minor_frame(bitsInMinorFrame);
    }
}
