using TmDq.Core;

namespace TmDq.App.Models.Processing;

/// <summary>The product's runner: the core's <see cref="CalibrationRun"/>, member for member.</summary>
public sealed class CoreCalibrationRunner : ICalibrationRunner
{
    public static readonly CoreCalibrationRunner Instance = new();

    public ICalibrationRunning Start(CalibrationRequest request, IRunObserver observer) =>
        new Running(CalibrationRun.Start(request, observer));

    private sealed class Running(CalibrationRun run) : ICalibrationRunning
    {
        public bool IsRunning => run.IsRunning;
        public void Cancel() => run.Cancel();
        public void Wait() => run.Wait();
        public CalibrationResult GetResult() => run.GetResult();
        public void Dispose() => run.Dispose();
    }
}
