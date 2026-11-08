using System;
using TmDq.App.Models.Processing;
using TmDq.App.Models.Settings;
using TmDq.App.Services;

namespace TmDq.App.ViewModels
{
    /// <summary>
    /// What the main window reaches outside itself: its dialogs, pickers, the run's configurator, the clock, the
    /// application's folder and the preferences. The application hands it all of them; a test hands only what it uses,
    /// and each one left out has the effect its property says.
    /// </summary>
    public sealed class MainWindowServices
    {
        /// <summary>Open's file dialog; without it Open is unavailable.</summary>
        public IRecordingPicker? Picker { get; init; }

        /// <summary>What a recording is processed with; without it, the first PCM channel with the Frame Sync Lock
        /// defaults (<see cref="FirstChannelRun"/>).</summary>
        public IStreamConfigurator? Configurator { get; init; }

        /// <summary>The progress dialog shown while a run goes on; without it none is shown.</summary>
        public IProcessingDialog? ProcessingDialog { get; init; }

        /// <summary>The log's clock; without it the system's.</summary>
        public TimeProvider? Clock { get; init; }

        /// <summary>The CSV to import and Export's Browse dialogs; without them Import CSV is unavailable.</summary>
        public IDataFilePicker? Files { get; init; }

        /// <summary>The Export dialog; without it Export is unavailable.</summary>
        public IExportDialog? ExportDialog { get; init; }

        /// <summary>What writes the plot as an image; without it an image export fails, and a batch writes no images.</summary>
        public IPlotImageExporter? ImageExporter { get; init; }

        /// <summary>Save as Template's and Apply Template's file dialogs; without them both are unavailable.</summary>
        public ITemplateFilePicker? TemplateFiles { get; init; }

        /// <summary>The batch confirmation and the choice of recording; without them Apply Template is unavailable.</summary>
        public ITemplateDialogs? TemplateDialogs { get; init; }

        /// <summary>Where <c>settings/</c> is, for a batch's runs; without it the application's folder.</summary>
        public string? AppRoot { get; init; }

        /// <summary>What is remembered between sessions (plan step 4.8); without it the defaults, kept in memory.</summary>
        public PreferencesStore? Preferences { get; init; }
    }
}
