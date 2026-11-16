namespace TmDq.App
{
    /// <summary>The marks the application writes before a status, a row or a reason, once each: the tick, the warning
    /// sign and the cross, as v2.x wrote them. Written as escapes, so that no editor shows another character.</summary>
    public static class Glyphs
    {
        /// <summary>U+2713 CHECK MARK.</summary>
        public const string Ok = "\u2713";

        /// <summary>U+26A0 WARNING SIGN.</summary>
        public const string Warn = "\u26A0";

        /// <summary>U+2717 BALLOT X.</summary>
        public const string Fail = "\u2717";
    }
}
