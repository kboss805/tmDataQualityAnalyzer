using System;
using TmDq.Core;

namespace TmDq.App.Models.Processing;

/// <summary>Starts a calibration extraction. The Extract Calibration view model starts its run through this, so a test
/// can stand in for the core and make a run fail where the core cannot be made to (audit B4); the product's is the
/// core's <see cref="CalibrationRun"/>.</summary>
public interface ICalibrationRunner
{
    /// <summary>Starts <paramref name="request"/>, reporting to <paramref name="observer"/>.</summary>
    /// <exception cref="TmdqException">A setting the core refuses; nothing has started.</exception>
    ICalibrationRunning Start(CalibrationRequest request, IRunObserver observer);
}

/// <summary>A calibration run in flight: <see cref="CalibrationRun"/>'s members the view model uses.</summary>
public interface ICalibrationRunning : IDisposable
{
    bool IsRunning { get; }

    void Cancel();

    void Wait();

    /// <summary>The exception the run's observer threw, caught at the boundary and kept, else null: read after
    /// <see cref="Wait"/>, because a report the observer could not handle leaves what it recorded untrustworthy.</summary>
    Exception? ObserverException { get; }

    /// <exception cref="TmdqException">The core could not give the result (out of memory, an internal error).</exception>
    CalibrationResult GetResult();
}
