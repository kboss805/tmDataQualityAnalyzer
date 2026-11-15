using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Layout;
using Avalonia.Markup.Xaml.MarkupExtensions;
using Avalonia.Metadata;
using Avalonia.Styling;

namespace TmDq.App.Views
{
    /// <summary>
    /// A dialog's footer, as Fluent's ContentDialog lays its command space out: a band across the bottom of the window in the
    /// base colour under a divider, the buttons at the right in equal widths with the spacing Fluent gives them, and the
    /// default button (Enter) in the accent. Every dialog has this one footer, in XAML or built in code, so none lays its own.
    /// <see cref="Leading"/> is for what belongs to the choice rather than to the answer ("Apply to all", "All"), at the left.
    /// </summary>
    public sealed class DialogFooter : Border
    {
        private readonly UniformGrid _buttons = new() { Rows = 1, HorizontalAlignment = HorizontalAlignment.Right };
        private readonly Grid _row = new() { ColumnDefinitions = new ColumnDefinitions("*,Auto") };
        private Control? _leading;

        public DialogFooter()
        {
            this[!BackgroundProperty] = new DynamicResourceExtension("SolidBackgroundFillColorBaseBrush");
            this[!BorderBrushProperty] = new DynamicResourceExtension("DividerStrokeColorDefaultBrush");
            BorderThickness = new Thickness(0, 1, 0, 0);
            // 24 at the sides as the content has, 16 above and below.
            Padding = new Thickness(24, 16);
            _buttons[!UniformGrid.ColumnSpacingProperty] = new DynamicResourceExtension("ContentDialogButtonSpacing");
            Grid.SetColumn(_buttons, 1);
            _row.Children.Add(_buttons);
            Child = _row;
            // The buttons' widths are the footer's: equal, and not narrower than Fluent's.
            var button = new Style(x => x.OfType<Button>());
            button.Setters.Add(new Setter(MinWidthProperty, 96.0));
            button.Setters.Add(new Setter(Button.HorizontalContentAlignmentProperty, HorizontalAlignment.Center));
            Styles.Add(button);
            _buttons.Children.CollectionChanged += (_, _) => AccentTheDefault();
        }

        /// <summary>A dialog built in code: <paramref name="body"/> in Fluent's 24 px of padding above a footer of
        /// <paramref name="buttons"/>, the same anatomy the XAML dialogs have.</summary>
        public static Control Compose(Control body, params Button[] buttons)
        {
            var footer = new DialogFooter();
            foreach (Button b in buttons) footer.Buttons.Add(b);
            body[!MarginProperty] = new DynamicResourceExtension("ContentDialogPadding");
            var layout = new DockPanel();
            DockPanel.SetDock(footer, Dock.Bottom);
            layout.Children.Add(footer);
            layout.Children.Add(body);
            return layout;
        }

        /// <summary>The buttons, in order; the one that is the default takes the accent.</summary>
        [Content]
        public Avalonia.Controls.Controls Buttons => _buttons.Children;

        /// <summary>What sits at the left, opposite the buttons.</summary>
        public Control? Leading
        {
            get => _leading;
            set
            {
                if (_leading is not null) _row.Children.Remove(_leading);
                _leading = value;
                if (value is null) return;
                value.VerticalAlignment = VerticalAlignment.Center;
                Grid.SetColumn(value, 0);
                _row.Children.Add(value);
            }
        }

        private void AccentTheDefault()
        {
            foreach (Control c in _buttons.Children)
                if (c is Button { IsDefault: true } b && !b.Classes.Contains("accent")) b.Classes.Add("accent");
        }
    }
}
