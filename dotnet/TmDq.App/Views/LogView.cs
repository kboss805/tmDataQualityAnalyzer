using System.Collections.Generic;
using System.Collections.Specialized;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Media;
using Avalonia.Media.Immutable;
using Avalonia.Threading;
using TmDq.App.Logging;

namespace TmDq.App.Views
{
    /// <summary>
    /// The log sidebar: every line of the session's log, timestamped and coloured by level as the Qt log was (an error
    /// red, a warning goldenrod, a success green, the rest in the text colour), in one block of text so a selection can
    /// run across lines. A line's own line breaks and runs of spaces are kept; the Qt log had to be told to with
    /// <c>white-space: pre-wrap</c>. It follows the newest line.
    /// </summary>
    public sealed class LogView : UserControl
    {
        public static readonly IBrush ErrorBrush = new ImmutableSolidColorBrush(Color.FromRgb(0xFF, 0x00, 0x00));
        public static readonly IBrush WarningBrush = new ImmutableSolidColorBrush(Color.FromRgb(0xDA, 0xA5, 0x20));
        public static readonly IBrush SuccessBrush = new ImmutableSolidColorBrush(Color.FromRgb(0x00, 0x80, 0x00));

        public static readonly StyledProperty<IList<LogEntry>?> EntriesProperty =
            AvaloniaProperty.Register<LogView, IList<LogEntry>?>(nameof(Entries));

        private readonly ScrollViewer _scroll;
        private readonly SelectableTextBlock _text;
        private INotifyCollectionChanged? _watched;
        private int _shown;

        public LogView()
        {
            _text = new SelectableTextBlock { Name = "LogText", TextWrapping = TextWrapping.Wrap, FontSize = 12, Margin = new Thickness(4) };
            // No horizontal scrolling (the default): a long line wraps.
            _scroll = new ScrollViewer { Name = "LogScroll", Content = _text };
            Content = _scroll;
        }

        /// <summary>The lines shown; a collection that reports its changes is followed.</summary>
        public IList<LogEntry>? Entries
        {
            get => GetValue(EntriesProperty);
            set => SetValue(EntriesProperty, value);
        }

        public static IBrush? BrushOf(LogLevel level) => level switch
        {
            LogLevel.Error => ErrorBrush,
            LogLevel.Warning => WarningBrush,
            LogLevel.Success => SuccessBrush,
            _ => null,
        };

        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);
            if (change.Property != EntriesProperty) return;
            if (_watched is not null) _watched.CollectionChanged -= OnEntriesChanged;
            _watched = Entries as INotifyCollectionChanged;
            if (_watched is not null) _watched.CollectionChanged += OnEntriesChanged;
            Rebuild();
        }

        private void OnEntriesChanged(object? sender, NotifyCollectionChangedEventArgs e)
        {
            // Lines added at the end (the log only ever grows) are appended; anything else is drawn again.
            if (e.Action == NotifyCollectionChangedAction.Add && e.NewStartingIndex == _shown)
            {
                foreach (LogEntry entry in e.NewItems!) Append(entry);
                FollowNewest();
            }
            else
            {
                Rebuild();
            }
        }

        private void Rebuild()
        {
            _text.Inlines!.Clear();
            _shown = 0;
            if (Entries is not null)
                foreach (LogEntry entry in Entries) Append(entry);
            FollowNewest();
        }

        private void Append(LogEntry entry)
        {
            InlineCollection inlines = _text.Inlines!;
            if (inlines.Count > 0) inlines.Add(new LineBreak());
            var run = new Run(entry.Line);
            if (BrushOf(entry.Level) is { } brush) run.Foreground = brush;
            inlines.Add(run);
            _shown++;
        }

        // After the layout that measures the new line, so the end is where it now is.
        private void FollowNewest() => Dispatcher.UIThread.Post(() => _scroll.ScrollToEnd(), DispatcherPriority.Background);
    }
}
