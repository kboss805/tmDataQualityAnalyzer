namespace TmDq.App.Models.Processing;

/// <summary>What a calibration is extracted with: the Receiver SNR dialog's settings as they stand, the time channel
/// chosen in Configure Streams and the recording's byte order, so the extraction reads the calibration recording
/// exactly as the main run will read the data it calibrates.</summary>
public sealed record CalibrationBase(
    int TimeChannelId,
    int PcmChannelId,
    string SyncPattern,
    string SyncMask,
    int BitsInMinorFrame,
    bool Randomized,
    bool Inverted,
    double DataRateMbps,
    bool SwapBytes,
    string ReceiverParamsToml,
    int NumReceivers,
    int ReceiverChannels);
