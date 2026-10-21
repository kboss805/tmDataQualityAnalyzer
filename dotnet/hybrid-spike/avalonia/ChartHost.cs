using Avalonia;
using Avalonia.Controls;
using Avalonia.Rendering.Composition;

namespace Tmdq.AvaloniaSpike
{
    /// <summary>
    /// A control whose content is a composition surface fed with imported GPU images - Avalonia's
    /// counterpart of an Image whose Source is a D3DImage. It is an ordinary visual in the tree, so
    /// whatever is laid out after it in the same panel is drawn on top of it by the compositor.
    /// </summary>
    public sealed class ChartHost : Control
    {
        private CompositionSurfaceVisual _visual;

        public Compositor Compositor { get; private set; }
        public CompositionDrawingSurface Surface { get; private set; }

        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);
            Compositor = ElementComposition.GetElementVisual(this)!.Compositor;
            Surface = Compositor.CreateDrawingSurface();
            _visual = Compositor.CreateSurfaceVisual();
            _visual.Size = new Vector(Bounds.Width, Bounds.Height);
            _visual.Surface = Surface;
            ElementComposition.SetElementChildVisual(this, _visual);
        }

        protected override void OnSizeChanged(SizeChangedEventArgs e)
        {
            base.OnSizeChanged(e);
            if (_visual != null)
            {
                _visual.Size = new Vector(e.NewSize.Width, e.NewSize.Height);
            }
        }

        /// <summary>Resizes the surface visual explicitly (the resize check changes the image, not the layout).</summary>
        public void SetVisualSize(double width, double height)
        {
            if (_visual != null)
            {
                _visual.Size = new Vector(width, height);
            }
        }
    }
}
