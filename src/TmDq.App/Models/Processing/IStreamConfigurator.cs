using System.Threading.Tasks;
using TmDq.Core;

namespace TmDq.App.Models.Processing;

/// <summary>Decides what a recording is processed with: Configure Streams, or (with no one to ask) the defaults.</summary>
public interface IStreamConfigurator
{
    /// <summary>The run to start for <paramref name="file"/>, and the choices it came from; null when the user cancelled.</summary>
    Task<StreamsChoice?> ConfigureAsync(string path, Ch10File file);
}
