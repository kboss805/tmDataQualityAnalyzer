/**
 * @file mainviewchrome.cpp
 * @brief MainView's window chrome: the hamburger menu, the recent-files
 *        submenu, and the glyphs drawn in code so they follow the theme.
 *
 * Split out of mainview.cpp. These are MainView's own members - the class is
 * unchanged - but menu construction is self-contained and reads better away
 * from the window's wiring. The file-local helpers here serve only this code.
 */

#include "mainview.h"

#include <QAction>
#include <QActionGroup>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QUrl>
#include <QWidgetAction>

#include "constants.h"
#include "mainviewmodel.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#endif

namespace
{

    /// Title-bar glyph foreground for the active theme.
    QColor titleGlyphColor(bool dark)
    {
        return dark ? QColor(0xC8, 0xC8, 0xC8) : QColor(0x3C, 0x3C, 0x3C);
    }

    /// Adds a grey section header to a menu, e.g. "Process".
    ///
    /// **Not `QMenu::addSection()`.** Once *any* stylesheet is set on the
    /// application, `QStyleSheetStyle` takes over menu-item painting and never
    /// draws a section's text, so `addSection()` degrades silently into a plain
    /// separator. Measured on the "Process" band: unstyled it is 12 px and shows
    /// the label; with the theme applied it is 9 px and blank - and *deleting* the
    /// `QMenu::separator` rule does not bring the text back, because the cause is
    /// the stylesheet existing at all, not what it says. `main()` installs the
    /// theme at startup, so every themed build has shown unlabelled separators
    /// while the code, the docs and the manual all described named groups.
    ///
    /// A `QWidgetAction` carrying a `QLabel` paints itself and is therefore immune.
    /// Its colour comes from the theme QSS via the object name - the same approach
    /// as `QLabel#streamHeaderLabel` in `StreamConfigDialog` - so it follows the
    /// light/dark switch instead of hard-coding one theme's grey.
    void addMenuSection(QMenu* menu, const QString& text)
    {
        auto* label = new QLabel(text, menu);
        label->setObjectName("menuSectionLabel");

        auto* action = new QWidgetAction(menu);
        action->setDefaultWidget(label);
        // A header is not a command: disabled keeps it out of both mouse
        // selection and keyboard navigation. The QSS styles the disabled state
        // explicitly so it does not also pick up the greyed-out item colour.
        action->setEnabled(false);
        menu->addAction(action);
    }

    /// Paints a thin three-line "hamburger" menu glyph (Claude Code style). Drawn in
    /// code so it adapts to the theme without shipping separate dark/light assets.
    QIcon makeMenuIcon(bool dark)
    {
        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(titleGlyphColor(dark), 2.0);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        const qreal x0 = 7.5, x1 = 24.5;
        p.drawLine(QPointF(x0, 11), QPointF(x1, 11));
        p.drawLine(QPointF(x0, 16), QPointF(x1, 16));
        p.drawLine(QPointF(x0, 21), QPointF(x1, 21));
        p.end();
        return QIcon(pm);
    }

    /// Paints a thin "toggle sidebar" glyph (Claude Code style): a rounded window
    /// outline with a divider ~1/3 in and the left panel lightly filled.
    QIcon makeSidebarIcon(bool dark)
    {
        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor c = titleGlyphColor(dark);
        QPen pen(c, 1.9);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const QRectF r(6, 8, 20, 16);
        p.drawRoundedRect(r, 3, 3);
        const qreal x = r.left() + r.width() * 0.36;
        p.fillRect(QRectF(r.left() + 1.0, r.top() + 1.0, x - r.left() - 1.0, r.height() - 2.0),
                   QColor(c.red(), c.green(), c.blue(), 70));
        p.drawLine(QPointF(x, r.top() + 0.5), QPointF(x, r.bottom() - 0.5));
        p.end();
        return QIcon(pm);
    }

} // namespace

void MainView::setUpChrome()
{
    // A single hamburger menu holds everything, grouped into sections (see
    // addMenuSection - NOT QMenu::addSection, which a stylesheet silently blanks).
    // Sections are preferred over submenus because the
    // whole menu is small and scannable in one glance; a submenu is used only where
    // the entries are ones you reach for rarely and by name (Recent Files, Help), so
    // hiding them behind one row costs nothing and shortens the menu for everyone
    // else. The menu hangs off the hamburger button in the custom title bar (built
    // at the end of this method).
    QMenu* menu = new QMenu(this);
    menu->setToolTipsVisible(true);

    // --- Process ---
    addMenuSection(menu, tr("Process"));

    m_open_action = menu->addAction("Open...");
    m_open_action->setShortcut(QKeySequence::Open);
    connect(m_open_action, &QAction::triggered, this, &MainView::inputFileButtonPressed);

    // Recent Files sits directly under Open -- both are "get a file onto the plot".
    m_recent_menu = menu->addMenu("Recent Files");
    updateRecentFilesMenu();

    // Save as Template: capture the current per-stream settings as a reusable,
    // file-path-independent template for Batch Apply. It needs at least one
    // finished source, so it starts disabled.
    m_save_template_action = menu->addAction("Save as Template...");
    m_save_template_action->setEnabled(false);
    connect(m_save_template_action, &QAction::triggered, this, &MainView::saveTemplateButtonPressed);

    // Apply Template to Files: always available -- it opens its own file pickers.
    m_apply_template_action = menu->addAction("Apply Template to Files...");
    connect(m_apply_template_action, &QAction::triggered, this, &MainView::applyTemplateButtonPressed);

    menu->addSeparator();

    QAction* exit_action = menu->addAction("Exit");
    connect(exit_action, &QAction::triggered, this, &QMainWindow::close);

    // --- Import/Export ---
    addMenuSection(menu, tr("Import/Export"));

    // Import a previously exported CSV straight into the plot (US6.3).
    m_import_action = menu->addAction("Import CSV...");
    m_import_action->setToolTip("Import a previously exported CSV file");
    connect(m_import_action, &QAction::triggered, this, &MainView::importFileButtonPressed);

    // Export the current plot's data / image / log. Disabled until data loads;
    // wired to PlotWidget::onExportPlot in setUpConnections().
    m_export_action = menu->addAction("Export...");
    m_export_action->setToolTip("Export plot data and images");
    m_export_action->setEnabled(false);

    // --- Settings ---
    addMenuSection(menu, tr("Settings"));

    QSettings app_settings;
    QString current_theme = app_settings.value(UIConstants::kSettingsKeyTheme, UIConstants::kThemeDark).toString();
    m_theme_action = menu->addAction(
        (current_theme == UIConstants::kThemeDark) ? "Switch to Light Theme" : "Switch to Dark Theme");
    connect(m_theme_action, &QAction::triggered, this, &MainView::onToggleTheme);

    // --- Help ---
    // A submenu rather than a section: both entries are read-once-then-forget, so
    // they cost two permanent rows in a menu whose other entries are the actual
    // workflow. A separator keeps the collapsed row from reading as part of the
    // Settings section above it, since a submenu carries no section header of its
    // own. Held as a member so the structure is assertable.
    menu->addSeparator();
    m_help_menu = menu->addMenu(tr("Help"));

    // Two manuals, and the fallback between them is a NORMAL path, not an error:
    //
    //   - the full manual is an optional install, so its absence is expected;
    //   - the base manual is compiled in as a Qt resource, so it can never be
    //     missing. That is what makes "there is always a manual" structural rather
    //     than something the installer has to get right on every path.
    //
    // The full manual is a real file on disk and opens directly. The base one is a
    // resource, and a browser cannot read qrc:/ URLs, so it is copied to temp first.
    QAction* manual_action = m_help_menu->addAction("User Manual...");
    connect(manual_action, &QAction::triggered, this, [this]() {
        const QString full = installedFullManualPath();
        if (!full.isEmpty())
        {
            QDesktopServices::openUrl(QUrl::fromLocalFile(full));
            return;
        }

        const QString target = QDir::temp().filePath("tmDataQualityAnalyzer_manual.html");
        if (QFile::exists(target) && !QFile::remove(target))
        {
            displayErrorMessage(tr("Could not open the user manual (temp file is locked)."));
            return;
        }
        if (!QFile::copy(":/resources/usermanual.html", target))
        {
            displayErrorMessage(tr("Could not open the user manual."));
            return;
        }
        // Resource copies inherit read-only permissions; make the temp copy
        // writable so the remove() above succeeds on the next open.
        QFile(target).setPermissions(QFile::ReadOwner | QFile::WriteOwner |
                                     QFile::ReadUser  | QFile::WriteUser);
        QDesktopServices::openUrl(QUrl::fromLocalFile(target));
    });

    QAction* about_action = m_help_menu->addAction("About...");
    connect(about_action, &QAction::triggered, this, [this]() {
        QMessageBox about_box(this);
        about_box.setWindowTitle("About");
        about_box.setIconPixmap(QPixmap(":/resources/icon.ico").scaled(
            UIConstants::kAboutIconSize, UIConstants::kAboutIconSize,
            Qt::KeepAspectRatio, Qt::SmoothTransformation));
        about_box.setText(
            "<h3>TM Data Quality Analyzer</h3>"
            "<p>Version " + AppVersion::toString() + "</p>"
            "<p>Analyzes framesync lock statistics and receiver SNR data from "
            "IRIG 106 Chapter 10 PCM recordings and plots the results over time.</p>");
        about_box.exec();
    });

    // --- Custom title bar (frameless window) ---
    // Replaces the native Windows title bar: the hamburger on the left opens the
    // menu above; the window's min/maximize/close buttons live on the right. The
    // empty strip between them is reported as the drag caption in nativeEvent().
    auto* title_bar = new QWidget(this);
    title_bar->setObjectName("titleBar");
    title_bar->setFixedHeight(UIConstants::kTitleBarHeight);
    title_bar->setStyleSheet(
        // NO min-width/min-height here. Every title-bar button calls setFixedSize(),
        // and a QSS min-* declaration REPLACES the widget minimum that setFixedSize
        // installed - leaving max intact, so the button collapses to its sizeHint
        // (a caption glyph is only ~13px tall). Size these in code, not in QSS.
        "#titleBar QToolButton{border:none;background:transparent;font-size:15px;}"
        "#titleBar QToolButton:hover{background:rgba(128,128,128,0.22);}"
        // The caption buttons use the Windows icon font so minimize/maximize/close
        // are drawn at identical metrics (10px is the size Windows itself uses for
        // these glyphs). MDL2 Assets is the Windows 10 fallback.
        "#titleBar QToolButton#winBtn,#titleBar QToolButton#winClose"
        "{font-family:'Segoe Fluent Icons','Segoe MDL2 Assets';font-size:10px;"
        "border-radius:5px;}"
        "#titleBar QToolButton#winClose:hover{background:#c42b1c;color:#ffffff;}"
        "#titleBar QToolButton#menuBtn::menu-indicator{image:none;}"
        // menuBtn and sidebarBtn get a fixed size + AlignVCenter in code, so their
        // hover boxes stay inset from the bar edges (QSS margin isn't honored for
        // QToolButton) and read as compact pills like Claude Code's title bar.
        "#titleBar QToolButton#menuBtn{border-radius:5px;}"
        "#titleBar QToolButton#sidebarBtn{border-radius:5px;}");

    auto* bar_layout = new QHBoxLayout(title_bar);
    bar_layout->setContentsMargins(2, 0, UIConstants::kTitleBarEdgeGap, 0);
    bar_layout->setSpacing(0);

    m_menu_button = new QToolButton(title_bar);
    m_menu_button->setObjectName("menuBtn");
    m_menu_button->setToolTip(tr("Menu"));
    m_menu_button->setPopupMode(QToolButton::InstantPopup);
    m_menu_button->setMenu(menu);
    // Thin drawn glyph + fixed centered size, matching the sidebar toggle (icon set
    // per theme in applyActionIconsForTheme). Sized to hug the glyph - see the
    // kToolGlyphButton note.
    m_menu_button->setFixedSize(UIConstants::kToolGlyphButtonW, UIConstants::kToolGlyphButtonH);
    m_menu_button->setIconSize(QSize(UIConstants::kToolGlyphIconPx, UIConstants::kToolGlyphIconPx));
    bar_layout->addWidget(m_menu_button, 0, Qt::AlignVCenter);

    // Show/hide the left sidebar (log). Checked = shown; the icon is set per theme
    // in applyActionIconsForTheme(). Wired to the dock's visibility below, once the
    // dock exists (setUpMainLayout).
    m_sidebar_toggle = new QToolButton(title_bar);
    m_sidebar_toggle->setObjectName("sidebarBtn");
    m_sidebar_toggle->setCheckable(true);
    m_sidebar_toggle->setToolTip(tr("Toggle sidebar (Ctrl+B)"));
    m_sidebar_toggle->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    // Fixed, smaller-than-the-bar size + AlignVCenter guarantees the hover box has
    // clearance from the bar's top/bottom edges (see the sidebarBtn stylesheet note).
    m_sidebar_toggle->setFixedSize(UIConstants::kToolGlyphButtonW, UIConstants::kToolGlyphButtonH);
    m_sidebar_toggle->setIconSize(QSize(UIConstants::kToolGlyphIconPx, UIConstants::kToolGlyphIconPx));
    connect(m_sidebar_toggle, &QToolButton::toggled, this, [this](bool shown) {
        if (m_sidebar_dock != nullptr)
            m_sidebar_dock->setVisible(shown);
        QSettings().setValue(UIConstants::kSettingsKeySidebarVisible, shown);
    });
    bar_layout->addWidget(m_sidebar_toggle, 0, Qt::AlignVCenter);

    bar_layout->addStretch(1);

    // Caption glyphs come from the Windows icon font (see the winBtn stylesheet
    // rule): mixing ordinary Unicode characters here made the buttons look
    // mismatched, because U+2212/U+25A1/U+2715 are drawn at quite different
    // optical sizes by the UI font. The ChromeMinimize/Maximize/Restore/Close
    // glyphs are a set designed to the same metrics, which is also exactly what
    // Windows itself uses. They stay *text* (not QIcons) so the close button's
    // white-on-red hover rule can still recolor the glyph.
    auto* min_button = new QToolButton(title_bar);
    min_button->setObjectName("winBtn");
    min_button->setText(QChar(UIConstants::kGlyphMinimize));
    min_button->setToolTip(tr("Minimize"));
    connect(min_button, &QToolButton::clicked, this, &QWidget::showMinimized);
    min_button->setFixedSize(UIConstants::kToolGlyphButtonW, UIConstants::kToolGlyphButtonH);
    bar_layout->addWidget(min_button, 0, Qt::AlignVCenter);

    m_max_button = new QToolButton(title_bar);
    m_max_button->setObjectName("winBtn");
    m_max_button->setToolTip(tr("Maximize"));
    connect(m_max_button, &QToolButton::clicked, this, [this]() {
        setWindowState(isMaximized() ? (windowState() & ~Qt::WindowMaximized)
                                     : (windowState() |  Qt::WindowMaximized));
    });
    m_max_button->setFixedSize(UIConstants::kToolGlyphButtonW, UIConstants::kToolGlyphButtonH);
    bar_layout->addWidget(m_max_button, 0, Qt::AlignVCenter);

    auto* close_button = new QToolButton(title_bar);
    close_button->setObjectName("winClose");
    close_button->setText(QChar(UIConstants::kGlyphClose));
    close_button->setToolTip(tr("Close"));
    connect(close_button, &QToolButton::clicked, this, &QWidget::close);
    close_button->setFixedSize(UIConstants::kToolGlyphButtonW, UIConstants::kToolGlyphButtonH);
    bar_layout->addWidget(close_button, 0, Qt::AlignVCenter);

    setMenuWidget(title_bar);
    updateMaximizeButton();
}

void MainView::applyActionIconsForTheme(bool dark)
{
    // Green (export) / orange (import) menu-item icons have brighter dark-theme
    // variants and deeper light-theme variants so they read against both themes.
    const QString suffix = dark ? "-dark" : "-light";
    m_export_action->setIcon(QIcon(":/resources/export" + suffix + ".svg"));
    m_import_action->setIcon(QIcon(":/resources/import" + suffix + ".svg"));

    // The hamburger + sidebar-toggle glyphs are drawn in code (no icon font shipped)
    // so they track the title-bar foreground color for the active theme.
    if (m_menu_button != nullptr)
        m_menu_button->setIcon(makeMenuIcon(dark));
    if (m_sidebar_toggle != nullptr)
        m_sidebar_toggle->setIcon(makeSidebarIcon(dark));
}

void MainView::updateRecentFilesMenu()
{
    m_recent_menu->clear();
    QStringList recent = m_view_model->recentFiles();

    if (recent.isEmpty())
    {
        QAction* placeholder = m_recent_menu->addAction("(No recent files)");
        placeholder->setEnabled(false);
        return;
    }

    for (const QString& filepath : recent)
    {
        QString display = QFileInfo(filepath).fileName();
        QAction* action = m_recent_menu->addAction(display);
        action->setToolTip(filepath);
        connect(action, &QAction::triggered, this, [this, filepath]() {
            openPath(filepath);
        });
    }

    m_recent_menu->addSeparator();
    QAction* clear_action = m_recent_menu->addAction("Clear Recent Files");
    connect(clear_action, &QAction::triggered, this, [this]() {
        m_view_model->clearRecentFiles();
    });
}

// ---------------------------------------------------------------------------
// Frameless window: the title bar's own behaviour
//
// The bar above is a plain widget where the window's caption used to be, so
// Windows must be told which parts of it still act like a caption - dragging,
// resizing, snapping, double-click to maximize. That is what nativeEvent()
// answers, and why the maximize glyph is kept in step here rather than in the
// window's general wiring.
// ---------------------------------------------------------------------------

void MainView::updateMaximizeButton()
{
    if (m_max_button == nullptr)
        return;
    const bool maximized = isMaximized();
    // ChromeRestore / ChromeMaximize from the Windows icon font (see setUpTitleBar).
    m_max_button->setText(QChar(maximized ? UIConstants::kGlyphRestore : UIConstants::kGlyphMaximize));
    m_max_button->setToolTip(maximized ? tr("Restore") : tr("Maximize"));
}

void MainView::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange)
        updateMaximizeButton();
    QMainWindow::changeEvent(event);
}

bool MainView::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef _WIN32
    if (eventType == "windows_generic_MSG" && message != nullptr)
    {
        MSG* msg = static_cast<MSG*>(message);
        switch (msg->message)
        {
        case WM_NCCALCSIZE:
            // Strip the native title bar: the client area becomes the whole window.
            if (msg->wParam == TRUE)
            {
                // When maximized, inset by the frame thickness so the client doesn't
                // spill off-screen or cover the taskbar.
                if (::IsZoomed(msg->hwnd))
                {
                    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
                    const int fx = ::GetSystemMetrics(SM_CXFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER);
                    const int fy = ::GetSystemMetrics(SM_CYFRAME) + ::GetSystemMetrics(SM_CXPADDEDBORDER);
                    params->rgrc[0].left   += fx;
                    params->rgrc[0].right  -= fx;
                    params->rgrc[0].top    += fy;
                    params->rgrc[0].bottom -= fy;
                }
                *result = 0;
                return true;
            }
            break;

        case WM_NCHITTEST:
        {
            // Report resize borders + the draggable caption so Windows still does
            // move/resize/snap/double-click-maximize natively.
            RECT rc;
            ::GetWindowRect(msg->hwnd, &rc);
            const long gx = GET_X_LPARAM(msg->lParam);
            const long gy = GET_Y_LPARAM(msg->lParam);
            const long lx = gx - rc.left;
            const long ly = gy - rc.top;
            const long w  = rc.right - rc.left;
            const long h  = rc.bottom - rc.top;

            const double dpr     = devicePixelRatioF();
            const long   border  = static_cast<long>(8 * dpr);
            const long   titleH  = static_cast<long>(UIConstants::kTitleBarHeight * dpr);

            if (!::IsZoomed(msg->hwnd))
            {
                const bool onL = lx < border, onR = lx >= w - border;
                const bool onT = ly < border, onB = ly >= h - border;
                if (onT && onL) { *result = HTTOPLEFT;     return true; }
                if (onT && onR) { *result = HTTOPRIGHT;    return true; }
                if (onB && onL) { *result = HTBOTTOMLEFT;  return true; }
                if (onB && onR) { *result = HTBOTTOMRIGHT; return true; }
                if (onL)        { *result = HTLEFT;   return true; }
                if (onR)        { *result = HTRIGHT;  return true; }
                if (onT)        { *result = HTTOP;    return true; }
                if (onB)        { *result = HTBOTTOM; return true; }
            }

            // Client-relative coordinates (not window-relative): when maximized,
            // WM_NCCALCSIZE insets the client by the frame thickness, so lx/ly would
            // be offset by that inset for the strip check and childAt() below.
            POINT client_pt{ gx, gy };
            ::ScreenToClient(msg->hwnd, &client_pt);

            if (client_pt.y >= 0 && client_pt.y < titleH)
            {
                // Any actual button on the strip (hamburger, sidebar toggle, window
                // controls) must receive clicks; the empty space between them is the
                // draggable caption. Detecting the child widget under the cursor keeps
                // this correct no matter how many buttons the title bar grows.
                const QPoint local(static_cast<int>(client_pt.x / dpr),
                                   static_cast<int>(client_pt.y / dpr));
                const bool on_button = qobject_cast<QToolButton*>(childAt(local)) != nullptr;
                *result = on_button ? HTCLIENT : HTCAPTION;
                return true;
            }

            *result = HTCLIENT;
            return true;
        }
        default:
            break;
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}
