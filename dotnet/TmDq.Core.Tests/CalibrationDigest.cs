namespace TmDq.Core.Tests;

/// <summary>
/// What the core itself makes of the committed step_cal recording, recorded by running CalibrationExtractor
/// directly (the code behind the Qt application's Extract Calibration dialog, built by the same
/// buildCalibrationRequest) with the shipped 11-step file and the shipped word map, plus a few variations.
/// Doubles are printed with 17 significant digits, so they parse back to the exact value.
/// Channel line: index|name|word|valid|hadData|outcome|plateaus|extra|relaxed|saturated|sweepLevels|edgeThreshold|
/// pointCount|sumRaw|sumDb|plateauLevelCount|sumPlateauLevels|unresolvedCount|sumUnresolvedDb.
/// Case line: name|ok|channels|calibrated|totalPlateaus|recordingSeconds|sumRaw|sumDb.
/// </summary>
internal static class CalibrationDigest
{
    public const string Summary = "Calibrated 12 of 48 channel(s).";
    public const string Seconds = "65.400000000000006";
    public const string Report = "Applied non-linear calibration to 12 of 48 channel(s).\n\nCalibrated: receivers 1, 3, 5, 6.\n\nFell back to linear calibration:\n  Receiver 2 \u2014 L, R, C: flat / no signal (held at 0)\n  Receiver 4 \u2014 L, R, C: flat / no signal (held at 0, 365, 278)\n  Receiver 7 \u2014 L, R, C: flat / no signal (held at 0)\n  Receiver 8 \u2014 L, R, C: flat / no signal (held at 0, 129)\n  Receiver 9 \u2014 L, R, C: flat / no signal (held at 1410, 2603, 1325)\n  Receiver 10 \u2014 L, R, C: flat / no signal (held at 643, 604, 88)\n  Receiver 11 \u2014 R, C: too few plateaus (1 of 11 steps resolved); L: no monotonic sweep\n  Receiver 12 \u2014 L, R, C: too few plateaus (1 of 11 steps resolved)\n  Receiver 13 \u2014 L, R: flat / no signal (held at 0); C: too few plateaus (1 of 11 steps resolved)\n  Receiver 14 \u2014 L, C: too few plateaus (3 of 11 steps resolved); R: no monotonic sweep\n  Receiver 15 \u2014 L, R, C: flat / no signal (held at 0, 318)\n  Receiver 16 \u2014 L, R, C: flat / no signal (held at 2, 0, 192)\n\nCalibrated only up to a saturated top \u2014 these receivers ran out of range partway through the sweep, so their readings hold at the highest step still measurable:\n  Receiver 1 \u2014 L, R, C: readings hold at 48 dB (no measurement for 54, 60 dB)";

    /// <summary>The same channels run as a receiver stream with the profiles this calibration found.</summary>
    public const int RunSamples = 66;
    public const int RunChannels = 48;
    public const string RunSum = "29635.407119599371";

    public static readonly string[] Channels =
    {
        "0|L_RCVR1|0|1|1|0|9|0|0|1|9|39.852288000001295|9|265191.55199999997|216|9|302105.152|2|114",
        "1|R_RCVR1|1|1|1|0|11|0|0|1|10|28.465919999982201|9|255941.75999999998|216|11|321413.76000000001|2|114",
        "2|C_RCVR1|2|1|1|0|11|0|0|1|10|39.852287999979936|9|262884.41600000003|216|11|328356.41600000003|2|114",
        "3|L_RCVR2|3|0|1|2|1|0|0|0|0|2|0|0|0|1|0|0|0",
        "4|R_RCVR2|4|0|1|2|1|0|0|0|0|2|0|0|0|1|0|0|0",
        "5|C_RCVR2|5|0|1|2|1|0|0|0|0|2|0|0|0|1|0|0|0",
        "6|L_RCVR3|6|1|1|0|12|1|0|0|11|34.159103999993206|11|186225.02400000003|330|12|186225.02400000003|0|0",
        "7|R_RCVR3|7|1|1|0|13|1|0|0|11|28.465920000000658|11|192699.64800000002|330|12|192826.45333333334|0|0",
        "8|C_RCVR3|8|1|1|0|12|1|0|0|11|45.545471999999535|11|195182.016|330|12|195368.14222222223|0|0",
        "9|L_RCVR4|9|0|1|2|1|0|0|0|0|2|0|0|0|1|0|0|0",
        "10|R_RCVR4|10|0|1|2|1|0|0|0|0|28.465919999999898|0|0|0|1|364.77866666666665|0|0",
        "11|C_RCVR4|11|0|1|2|1|0|0|0|0|28.465919999999898|0|0|0|1|277.58222222222219|0|0",
        "12|L_RCVR5|12|1|1|0|12|1|0|0|11|39.852287999973868|11|185895.23200000002|330|12|186348.67911111112|0|0",
        "13|R_RCVR5|13|1|1|0|13|1|0|0|11|28.465919999974105|11|180245.696|330|12|180249.09511111112|0|0",
        "14|C_RCVR5|14|1|1|0|12|1|0|0|11|39.852288000012294|11|195361.408|330|12|195765.21244444445|0|0",
        "15|L_RCVR6|15|1|1|0|15|1|0|0|11|28.465920000002932|11|212732.416|330|12|212863.14666666667|0|0",
        "16|R_RCVR6|16|1|1|0|12|1|0|0|11|39.852288000000158|11|208217.34399999998|330|12|208217.34399999998|0|0",
        "17|C_RCVR6|17|1|1|0|12|1|0|0|11|34.159103999968927|11|217435.84|330|12|217748.57244444443|0|0",
        "18|L_RCVR7|18|0|1|2|1|0|0|0|0|2|0|0|0|1|0|0|0",
        "19|R_RCVR7|19|0|1|2|1|0|0|0|0|2|0|0|0|1|0|0|0",
        "20|C_RCVR7|20|0|1|2|1|0|0|0|0|2|0|0|0|1|0|0|0",
        "21|L_RCVR8|21|0|1|2|1|0|0|0|0|2|0|0|0|1|0|0|0",
        "22|R_RCVR8|22|0|1|2|1|0|0|0|0|5.6931839999998779|0|0|0|1|128.51911111111113|0|0",
        "23|C_RCVR8|23|0|1|2|1|0|0|0|0|2|0|0|0|1|0|0|0",
        "24|L_RCVR9|24|0|1|2|1|0|0|0|0|330.20467199999905|0|0|0|1|1409.7777777777776|0|0",
        "25|R_RCVR9|25|0|1|2|1|0|0|0|0|358.67059200000142|0|0|0|1|2602.9084444444443|0|0",
        "26|C_RCVR9|26|0|1|2|1|0|0|0|0|358.67059199999738|0|0|0|1|1325.4897777777776|0|0",
        "27|L_RCVR10|27|0|1|2|1|0|0|0|0|592.09113599999944|0|0|0|1|643.43466666666677|0|0",
        "28|R_RCVR10|28|0|1|2|1|0|0|0|0|666.10252799999989|0|0|0|1|604.42311111111098|0|0",
        "29|C_RCVR10|29|0|1|2|1|0|0|0|0|831.20486400000004|0|0|0|1|87.623111111111115|0|0",
        "30|L_RCVR11|30|0|1|4|1|0|0|0|0|1947.0689280000011|0|0|0|1|9299.4773333333324|0|0",
        "31|R_RCVR11|31|0|1|3|2|0|0|0|0|2226.0349439999982|0|0|0|2|17472.476444444444|0|0",
        "32|C_RCVR11|32|0|1|3|1|0|0|0|0|2055.2394239999912|0|0|0|1|10236.700444444446|0|0",
        "33|L_RCVR12|33|0|1|3|1|0|0|0|0|1286.6595839999948|0|0|0|1|13036.295111111112|0|0",
        "34|R_RCVR12|34|0|1|3|2|0|0|0|0|1019.0799360000116|0|0|0|2|19811.804444444446|0|0",
        "35|C_RCVR12|35|0|1|3|2|0|0|0|0|1132.943616000005|0|0|0|2|19981.319111111112|0|0",
        "36|L_RCVR13|36|0|1|2|1|0|0|0|0|2|0|0|0|1|0|0|0",
        "37|R_RCVR13|37|0|1|2|1|0|0|0|0|2|0|0|0|1|0|0|0",
        "38|C_RCVR13|38|0|1|3|6|0|0|0|0|239.11372799997667|0|0|0|6|126547.82577777779|0|0",
        "39|L_RCVR14|39|0|1|3|3|0|0|0|0|853.97759999999994|0|0|0|3|32803.249777777775|0|0",
        "40|R_RCVR14|40|0|1|4|13|0|0|0|0|278.96601600000008|0|0|0|13|57995.839999999997|0|0",
        "41|C_RCVR14|41|0|1|3|3|0|0|0|0|723.03436799999963|0|0|0|3|31348.871111111108|0|0",
        "42|L_RCVR15|42|0|1|2|1|0|0|0|0|2|0|0|0|1|0|0|0",
        "43|R_RCVR15|43|0|1|2|1|0|0|0|0|11.386368000000264|0|0|0|1|317.5964444444445|0|0",
        "44|C_RCVR15|44|0|1|2|1|0|0|0|0|2|0|0|0|1|0|0|0",
        "45|L_RCVR16|45|0|1|2|1|0|0|0|0|5.6931840000000005|0|0|0|1|1.8844444444444446|0|0",
        "46|R_RCVR16|46|0|1|2|1|0|0|0|0|2|0|0|0|1|0|0|0",
        "47|C_RCVR16|47|0|1|2|1|0|0|0|0|5.6931839999998779|0|0|0|1|191.92888888888888|0|0",
    };

    public static readonly string[] Cases =
    {
        "base|1|48|12|204|65.400000000000006|2558012.3520000009|3618",
        "clips|1|48|12|189|65.400000000000006|2558008.7680000011|3618",
        "random|0|0|0|0|0|0|0",
        "invert|0|0|0|0|0|0|0",
        "rate|1|48|12|286|130.80000000000001|2557751.6800000002|3618",
        "mask|0|0|0|0|0|0|0",
        "bits|1|48|12|228|65.400000000000006|2557985.6640000008|3618",
        "steps-short|1|48|12|204|65.400000000000006|2687282.8800000008|3186",
        "grid-2x1|1|2|2|20|65.400000000000006|521133.31199999998|432",
        "grid-1x2|1|2|2|20|65.400000000000006|521133.31199999998|432",
    };
}
