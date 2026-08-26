#include "annotate/AnnotateWindow.h"

#include "annotate/AnnotationCommands.h"
#include "annotate/HueColorSlider.h"
#include "app/Analytics.h"
#include "app/MacIcons.h"
#include "app/SignInDialog.h"
#include "app/MacPermissions.h"
#include "camera/CameraCapture.h"
#include "camera/PersonCutout.h"
#include "auth/AuthSession.h"
#include "cloud/CloudClient.h"
#include "errors/ErrorCatalog.h"
#include "export/CloudPngEncoder.h"
#include "local/LocalStore.h"
#include "update/SparkleUpdater.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QSignalBlocker>
#include <QBrush>
#include <QCloseEvent>
#include <QPointer>
#include <QDateTime>
#include <QDir>
#include <QRandomGenerator>
#include <QColor>
#include <QIcon>
#include <QMenu>
#include <QPixmap>
#include <QToolButton>
#include <QWidgetAction>
#include <QGraphicsDropShadowEffect>
#include <QLinearGradient>
#include <QSize>
#include <QDebug>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QJsonArray>
#include <QJsonObject>
#include <QProgressBar>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGuiApplication>
#include <QWindow>
#include <QKeyEvent>
#include <QLineF>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPushButton>
#include <QRectF>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QSizePolicy>
#include <QSlider>
#include <QWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QTransform>
#include <QUndoStack>

namespace {

void backgroundPresetColors(int preset, QColor *from, QColor *to)
{
    if (!from || !to) {
        qWarning() << "backgroundPresetColors: null color out preset=" << preset;
        return;
    }
    switch (preset) {
    case 1:
        *from = QColor(255, 59, 48);
        *to = QColor(255, 105, 180);
        break;
    case 2:
        *from = QColor(255, 204, 0);
        *to = QColor(255, 122, 0);
        break;
    case 3:
        *from = QColor(52, 199, 89);
        *to = QColor(0, 199, 190);
        break;
    case 4:
        *from = QColor(0, 122, 255);
        *to = QColor(175, 82, 222);
        break;
    case 5:
        *from = QColor(255, 45, 85);
        *to = QColor(88, 86, 214);
        break;
    default:
        *from = QColor(0, 0, 0, 0);
        *to = QColor(0, 0, 0, 0);
        break;
    }
    qInfo() << "backgroundPresetColors: preset=" << preset << "from=" << *from << "to=" << *to;
}

QIcon backgroundCircleIcon(int preset)
{
    constexpr int logical = 22;
    constexpr qreal dpr = 2.0;
    QPixmap pm(qRound(logical * dpr), qRound(logical * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF disk(1.5, 1.5, logical - 3.0, logical - 3.0);
    if (preset <= 0) {
        painter.setPen(QPen(QColor(180, 180, 180), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(disk);
        painter.drawLine(disk.bottomLeft() + QPointF(4, -4), disk.topRight() + QPointF(-4, 4));
        qInfo() << "backgroundCircleIcon: none";
    } else {
        QColor from;
        QColor to;
        backgroundPresetColors(preset, &from, &to);
        QLinearGradient gradient(disk.topLeft(), disk.bottomRight());
        gradient.setColorAt(0, from);
        gradient.setColorAt(1, to);
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawEllipse(disk);
        qInfo() << "backgroundCircleIcon: preset=" << preset;
    }
    return QIcon(pm);
}

class EditorView : public QGraphicsView {
public:
    explicit EditorView(QGraphicsScene *scene, AnnotateWindow *host)
        : QGraphicsView(scene)
        , m_host(host)
    {
        setRenderHint(QPainter::Antialiasing);
        setRenderHint(QPainter::SmoothPixmapTransform);
        setDragMode(QGraphicsView::NoDrag);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        const QPointF scenePos = mapToScene(event->pos());
        qInfo() << "EditorView: mousePress" << scenePos;
        if (m_host->viewPress(event, scenePos)) {
            QGraphicsView::mousePressEvent(event);
        }
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        const QPointF scenePos = mapToScene(event->pos());
        if (m_host->viewMove(event, scenePos)) {
            QGraphicsView::mouseMoveEvent(event);
        }
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const QPointF scenePos = mapToScene(event->pos());
        qInfo() << "EditorView: mouseRelease" << scenePos;
        if (m_host->viewRelease(event, scenePos)) {
            QGraphicsView::mouseReleaseEvent(event);
        }
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        qInfo() << "EditorView: keyPress" << event->key();
        if (m_host->viewKeyPress(event)) {
            return;
        }
        QGraphicsView::keyPressEvent(event);
    }
    void drawForeground(QPainter *painter, const QRectF &rect) override
    {
        QGraphicsView::drawForeground(painter, rect);
        m_host->paintSelectHandles(painter);
    }

private:
    AnnotateWindow *m_host = nullptr;
};

// ─── Ariadne's Thread [AT-0054] ─────────────────────
// What: Arrow head is a filled triangle from the tip via QLineF::setAngle
// Why:  Rotating around back made left.p2/right.p2 sit on the tip, so the head looked like a T-bar
// Date: 2026-08-25
// Related: [AT-0048] AnnotateWindow.cpp:makeArrow, AnnotateItems.h:AnnotateKind::Line
// ─────────────────────────────────────────────────────
QGraphicsPathItem *makeArrow(const QPointF &from, const QPointF &to, const QColor &color)
{
    auto *item = new QGraphicsPathItem;
    setAnnotateKind(item, AnnotateKind::Arrow);
    item->setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    item->setBrush(color);
    setArrowEndpoints(item, from, to);
    qInfo() << "makeArrow: from" << from << "to" << to << "color=" << color;
    return item;
}

// ─── Ariadne's Thread [AT-0071] ─────────────────────
// What: Native QPushButton helper; no stylesheet; optional default bezel
// Why:  Accent stylesheet hid the Cocoa button; palette Accent is ignored on macOS
// Date: 2026-08-25
// Related: [AT-0067] AnnotateWindow.cpp, [AT-0063] AnnotateWindow.cpp
// ─────────────────────────────────────────────────────
QPushButton *makeNativeToolbarButton(const QString &title, bool isDefault)
{
    auto *btn = new QPushButton(title);
    btn->setAutoDefault(false);
    btn->setDefault(isDefault);
    btn->setFocusPolicy(Qt::NoFocus);
    qInfo() << "makeNativeToolbarButton:" << title << "default=" << isDefault;
    return btn;
}

QGraphicsLineItem *makeLine(const QPointF &from, const QPointF &to, const QColor &color)
{
    auto *item = new QGraphicsLineItem(QLineF(from, to));
    setAnnotateKind(item, AnnotateKind::Line);
    item->setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    setAnnotateEndpoints(item, from, to);
    qInfo() << "makeLine: from" << from << "to" << to << "color=" << color;
    return item;
}

} // namespace

// ─── Ariadne's Thread [AT-0044] ─────────────────────
// What: Annotate UI for Text in-place, Highlight styles, Step, hue slider
// Why:  PRD-02 annotate tools
// Date: 2026-08-25
// Related: [AT-0012] AnnotateWindow.h, [AT-0042] HueColorSlider.h, [AT-0040] AnnotateItems.h
// ─────────────────────────────────────────────────────
AnnotateWindow::AnnotateWindow(const QImage &image, AuthSession *auth, CloudClient *cloud, QWidget *parent)
    : QMainWindow(parent)
    , m_source(image.convertToFormat(QImage::Format_ARGB32))
    , m_auth(auth)
    , m_cloud(cloud)
{
    setWindowTitle(QStringLiteral("SeenShot — Annotate"));
    m_scene = new QGraphicsScene(this);
    m_background = new QGraphicsRectItem();
    m_background->setZValue(-2);
    m_background->setPen(Qt::NoPen);
    m_background->setVisible(false);
    m_scene->addItem(m_background);
    m_photo = new ShotPhotoItem(QPixmap::fromImage(m_source));
    m_photo->setZValue(-1);
    m_scene->addItem(m_photo);
    m_photoShadow = new QGraphicsDropShadowEffect(this);
    m_photoShadow->setColor(QColor(0, 0, 0, 140));
    m_photoShadow->setEnabled(false);
    m_photo->setGraphicsEffect(m_photoShadow);
    m_scene->setSceneRect(m_source.rect());
    m_view = new EditorView(m_scene, this);
    m_undo = new QUndoStack(this);

    auto *toolbar = addToolBar(QStringLiteral("Tools"));
    // ─── Ariadne's Thread [AT-0063] ─────────────────────
    // What: Show toolbar actions as icons only; action text stays for Qt tooltips
    // Why:  Toolbar must not show labels next to tools
    // Date: 2026-08-25
    // Related: [AT-0055] MacIcons.mm:macToolbarIcon, [AT-0044] AnnotateWindow.cpp
    // ─────────────────────────────────────────────────────
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setIconSize(QSize(16, 16));
    qInfo() << "AnnotateWindow: toolbar style=ToolButtonIconOnly iconSize=16";
    // ─── Ariadne's Thread [AT-0066] ─────────────────────
    // What: Highlight styles as toolbar actions; Select uses QGraphicsItem selection
    // Why:  Style combo hid the four icons; click must select an object without drawing
    // Date: 2026-08-25
    // Related: [AT-0051] AnnotateWindow.cpp:onHighlightStyleChanged, [AT-0044] AnnotateWindow.cpp
    // ─────────────────────────────────────────────────────
    m_toolGroup = new QActionGroup(this);
    m_toolGroup->setExclusive(true);
    const auto addCheckable = [this, toolbar](QAction *&slot, const QString &symbol, const QString &tip,
                                              void (AnnotateWindow::*method)()) {
        slot = toolbar->addAction(macToolbarIcon(symbol), tip, this, method);
        slot->setCheckable(true);
        m_toolGroup->addAction(slot);
        qInfo() << "AnnotateWindow: tool action" << tip << "iconNull=" << slot->icon().isNull();
    };
    addCheckable(m_selectAction, QStringLiteral("cursorarrow"), QStringLiteral("Select"),
                 &AnnotateWindow::setToolSelect);
    // ─── Ariadne's Thread [AT-0108] ─────────────────────
    // What: One Square tool plus Steps; Stroke/Fill sliders set border and fill
    // Why:  Border / Fill / Fill+text icons duplicated the sliders
    // Date: 2026-08-26
    // Related: [AT-0046] AnnotateWindow.cpp:onStrokeChanged, docs/PRD-02-annotate-tools.md
    // ─────────────────────────────────────────────────────
    addCheckable(m_highlightAction, QStringLiteral("rectangle"), QStringLiteral("Square"),
                 &AnnotateWindow::setToolHighlight);
    const auto addStyle = [this, toolbar](QAction *&slot, const QString &symbol, const QString &tip,
                                          HighlightStyle style) {
        slot = toolbar->addAction(macToolbarIcon(symbol), tip, this, [this, style]() {
            setHighlightStyleTool(style);
        });
        slot->setCheckable(true);
        m_toolGroup->addAction(slot);
        qInfo() << "AnnotateWindow: highlight style action" << tip << "style=" << static_cast<int>(style)
                << "iconNull=" << slot->icon().isNull();
    };
    addStyle(m_styleStepsAction, QStringLiteral("list.number"), QStringLiteral("Steps"), HighlightStyle::Steps);
    addCheckable(m_arrowAction, QStringLiteral("arrow.up.right"), QStringLiteral("Arrow"),
                 &AnnotateWindow::setToolArrow);
    addCheckable(m_lineAction, QStringLiteral("line.diagonal"), QStringLiteral("Line"),
                 &AnnotateWindow::setToolLine);
    addCheckable(m_textToolAction, QStringLiteral("textformat"), QStringLiteral("Text"),
                 &AnnotateWindow::setToolText);
    // ─── Ariadne's Thread [AT-0132] ─────────────────────
    // What: Put a Blur QLabel to the left of the Blur tool icon
    // Why:  Same label+control pattern as Color / Background
    // Date: 2026-08-26
    // Related: [AT-0063] AnnotateWindow.cpp, [AT-0071] AnnotateWindow.cpp
    // ─────────────────────────────────────────────────────
    auto *blurLabel = new QLabel(QStringLiteral("Blur"));
    toolbar->addWidget(blurLabel);
    addCheckable(m_blurAction, QStringLiteral("circle.lefthalf.filled"), QStringLiteral("Blur"),
                 &AnnotateWindow::setToolBlur);
    qInfo() << "AnnotateWindow: Blur label added left of tool icon";
    // ─── Ariadne's Thread [AT-0127] ─────────────────────
    // What: Photo toolbar button is checkable: press on opens pip, press off stops camera
    // Why:  MenuButtonPopup second click opened Picture/5s and could not turn the pip off
    // Date: 2026-08-26
    // Related: [AT-0075] AnnotateWindow.cpp, docs/PRD-03-photo-cutout.md
    // ─────────────────────────────────────────────────────
    m_photoButton = new QToolButton(toolbar);
    m_photoButton->setIcon(macToolbarIcon(QStringLiteral("camera")));
    m_photoButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_photoButton->setPopupMode(QToolButton::MenuButtonPopup);
    m_photoButton->setCheckable(true);
    m_photoButton->setFocusPolicy(Qt::NoFocus);
    m_photoButton->setToolTip(QStringLiteral("Photo"));
    m_photoButton->setAccessibleName(QStringLiteral("Photo"));
    auto *photoMenu = new QMenu(m_photoButton);
    photoMenu->addAction(QStringLiteral("Picture"), this, &AnnotateWindow::startPhotoPicture);
    photoMenu->addAction(QStringLiteral("5s timer"), this, &AnnotateWindow::startPhotoTimer5);
    m_photoButton->setMenu(photoMenu);
    connect(m_photoButton, &QToolButton::clicked, this, &AnnotateWindow::togglePhotoPreview);
    toolbar->addWidget(m_photoButton);
    qInfo() << "AnnotateWindow: Photo checkable MenuButtonPopup iconNull=" << m_photoButton->icon().isNull();
    m_highlightAction->setChecked(true);
    toolbar->addSeparator();
    m_undoAction = toolbar->addAction(macToolbarIcon(QStringLiteral("arrow.uturn.backward")),
                                      QStringLiteral("Undo"), this, &AnnotateWindow::undo);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction = toolbar->addAction(macToolbarIcon(QStringLiteral("arrow.uturn.forward")),
                                      QStringLiteral("Redo"), this, &AnnotateWindow::redo);
    m_redoAction->setShortcut(QKeySequence::Redo);
    qInfo() << "AnnotateWindow: action icons Undo=" << !m_undoAction->icon().isNull()
            << "Redo=" << !m_redoAction->icon().isNull();

    // ─── Ariadne's Thread [AT-0071] ─────────────────────
    // What: Restore slider QLabel text; Save/Share Link are native QPushButton
    // Why:  Icon-only pass removed labels; stylesheet accent hid the Cocoa bezel
    // Date: 2026-08-25
    // Related: [AT-0063] AnnotateWindow.cpp, [AT-0067] AnnotateWindow.cpp
    // ─────────────────────────────────────────────────────
    auto *hueLabel = new QLabel(QStringLiteral("Color"));
    m_hue = new HueColorSlider;
    m_hue->setToolTip(QStringLiteral("Color"));
    m_hue->setAccessibleName(QStringLiteral("Color"));
    connect(m_hue, &QSlider::sliderPressed, this, &AnnotateWindow::onHuePressed);
    connect(m_hue, &QSlider::valueChanged, this, &AnnotateWindow::onHueChanged);
    connect(m_hue, &QSlider::sliderReleased, this, &AnnotateWindow::onHueReleased);
    toolbar->addWidget(hueLabel);
    toolbar->addWidget(m_hue);
    auto *strokeLabel = new QLabel(QStringLiteral("Stroke"));
    m_stroke = new QSlider(Qt::Horizontal);
    m_stroke->setRange(1, 16);
    m_stroke->setValue(m_strokeWidth);
    m_stroke->setFixedWidth(56);
    m_stroke->setToolTip(QStringLiteral("Stroke"));
    m_stroke->setAccessibleName(QStringLiteral("Stroke"));
    connect(m_stroke, &QSlider::sliderPressed, this, &AnnotateWindow::onStrokePressed);
    connect(m_stroke, &QSlider::valueChanged, this, &AnnotateWindow::onStrokeChanged);
    connect(m_stroke, &QSlider::sliderReleased, this, &AnnotateWindow::onStrokeReleased);
    toolbar->addWidget(strokeLabel);
    toolbar->addWidget(m_stroke);
    auto *fillLabel = new QLabel(QStringLiteral("Fill"));
    m_fill = new QSlider(Qt::Horizontal);
    m_fill->setRange(0, 100);
    m_fill->setValue(qRound(m_fillAlpha * 100.0 / 255.0));
    m_fill->setFixedWidth(56);
    m_fill->setToolTip(QStringLiteral("Fill"));
    m_fill->setAccessibleName(QStringLiteral("Fill"));
    connect(m_fill, &QSlider::sliderPressed, this, &AnnotateWindow::onFillPressed);
    connect(m_fill, &QSlider::valueChanged, this, &AnnotateWindow::onFillChanged);
    connect(m_fill, &QSlider::sliderReleased, this, &AnnotateWindow::onFillReleased);
    toolbar->addWidget(fillLabel);
    toolbar->addWidget(m_fill);
    // ─── Ariadne's Thread [AT-0078] ─────────────────────
    // What: Remove Amount slider from the annotate toolbar
    // Why:  Blur keeps the existing default radius; the Amount control is gone
    // Date: 2026-08-25
    // Related: [AT-0047] AnnotateWindow.cpp:onAmountChanged
    // ─────────────────────────────────────────────────────
    qInfo() << "AnnotateWindow: Amount slider omitted, blur radius=" << m_blurRadius;
    auto *bgLabel = new QLabel(QStringLiteral("Background"));
    // ─── Ariadne's Thread [AT-0073] ─────────────────────
    // What: Background is a QToolButton whose InstantPopup menu is a column of circles
    // Why:  QComboBox text list hid the gradient presets; each circle is one existing preset
    // Date: 2026-08-25
    // Related: [AT-0071] AnnotateWindow.cpp, [AT-0056] AnnotateWindow.cpp:applyCanvasChrome
    // ─────────────────────────────────────────────────────
    m_bgButton = new QToolButton(toolbar);
    m_bgButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_bgButton->setFocusPolicy(Qt::NoFocus);
    m_bgButton->setToolTip(QStringLiteral("Background"));
    m_bgButton->setAccessibleName(QStringLiteral("Background"));
    m_bgButton->setIconSize(QSize(22, 22));
    auto *bgMenu = new QMenu(m_bgButton);
    auto *bgRow = new QWidget(bgMenu);
    auto *bgLayout = new QVBoxLayout(bgRow);
    bgLayout->setContentsMargins(8, 6, 8, 6);
    bgLayout->setSpacing(6);
    bgLayout->setAlignment(Qt::AlignHCenter);
    m_bgGroup = new QActionGroup(this);
    m_bgGroup->setExclusive(true);
    const QString bgTips[] = {QStringLiteral("None"), QStringLiteral("Red-Pink"), QStringLiteral("Yellow-Orange"),
                              QStringLiteral("Green-Teal"), QStringLiteral("Blue-Violet"),
                              QStringLiteral("Magenta-Purple")};
    for (int id = 0; id <= 5; ++id) {
        auto *act = new QAction(backgroundCircleIcon(id), bgTips[id], m_bgGroup);
        act->setCheckable(true);
        act->setData(id);
        act->setToolTip(bgTips[id]);
        m_bgGroup->addAction(act);
        auto *dot = new QToolButton(bgRow);
        dot->setDefaultAction(act);
        dot->setToolButtonStyle(Qt::ToolButtonIconOnly);
        dot->setFocusPolicy(Qt::NoFocus);
        dot->setAutoRaise(true);
        dot->setIconSize(QSize(22, 22));
        bgLayout->addWidget(dot);
        connect(act, &QAction::triggered, this, [this, bgMenu, id](bool checked) {
            if (!checked) {
                qInfo() << "AnnotateWindow: background action unchecked id=" << id;
                return;
            }
            qInfo() << "AnnotateWindow: background circle picked id=" << id;
            setBackgroundPreset(id);
            bgMenu->close();
        });
        qInfo() << "AnnotateWindow: background circle action id=" << id << bgTips[id];
    }
    auto *bgWidgetAction = new QWidgetAction(bgMenu);
    bgWidgetAction->setDefaultWidget(bgRow);
    bgMenu->addAction(bgWidgetAction);
    // ─── Ariadne's Thread [AT-0131] ─────────────────────
    // What: Open the Background menu with QMenu::popup, not QToolButton::setMenu
    // Why:  InstantPopup paints a down chevron over the circle on macOS
    // Date: 2026-08-26
    // Related: [AT-0073] AnnotateWindow.cpp, docs/PRD-02-annotate-tools.md
    // ─────────────────────────────────────────────────────
    connect(m_bgButton, &QToolButton::clicked, this, [this, bgMenu]() {
        if (!m_bgButton || !bgMenu) {
            qWarning() << "AnnotateWindow: Background menu missing on click";
            return;
        }
        const QPoint pos = m_bgButton->mapToGlobal(QPoint(0, m_bgButton->height()));
        qInfo() << "AnnotateWindow: Background menu popup at" << pos;
        bgMenu->popup(pos);
    });
    qInfo() << "AnnotateWindow: Background menu has no tool-button indicator";
    m_bgPreset = 0;
    m_bgButton->setIcon(backgroundCircleIcon(0));
    if (QAction *none = m_bgGroup->actions().value(0)) {
        none->setChecked(true);
    }
    toolbar->addWidget(bgLabel);
    toolbar->addWidget(m_bgButton);
    qInfo() << "AnnotateWindow: background button + vertical circle menu";
    m_radiusLabel = new QLabel(QStringLiteral("Radius"));
    m_radius = new QSlider(Qt::Horizontal);
    m_radius->setRange(0, 80);
    m_radius->setValue(m_cornerRadius);
    m_radius->setFixedWidth(56);
    m_radius->setToolTip(QStringLiteral("Radius"));
    m_radius->setAccessibleName(QStringLiteral("Radius"));
    connect(m_radius, &QSlider::valueChanged, this, &AnnotateWindow::onRadiusChanged);
    toolbar->addWidget(m_radiusLabel);
    toolbar->addWidget(m_radius);
    m_shadowLabel = new QLabel(QStringLiteral("Shadow"));
    m_shadow = new QSlider(Qt::Horizontal);
    m_shadow->setRange(0, 40);
    m_shadow->setValue(m_shadowAmount);
    m_shadow->setFixedWidth(56);
    m_shadow->setToolTip(QStringLiteral("Shadow"));
    m_shadow->setAccessibleName(QStringLiteral("Shadow"));
    connect(m_shadow, &QSlider::valueChanged, this, &AnnotateWindow::onShadowChanged);
    toolbar->addWidget(m_shadowLabel);
    toolbar->addWidget(m_shadow);
    auto *spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    spacer->setMinimumWidth(0);
    toolbar->addWidget(spacer);
    QPushButton *saveBtn = makeNativeToolbarButton(QStringLiteral("Save"), true);
    QPushButton *shareBtn = makeNativeToolbarButton(QStringLiteral("Share Link"), false);
    connect(saveBtn, &QPushButton::clicked, this, &AnnotateWindow::saveLocal);
    connect(shareBtn, &QPushButton::clicked, this, &AnnotateWindow::share);
    toolbar->addWidget(saveBtn);
    toolbar->addWidget(shareBtn);
    qInfo() << "AnnotateWindow: slider labels on; Save/Share Link native QPushButton";

    setCentralWidget(m_view);
    m_view->setAlignment(Qt::AlignCenter);
    m_camera = new CameraCapture(this);
    connect(m_camera, &CameraCapture::stillReady, this, &AnnotateWindow::onPhotoStillReady);
    connect(m_camera, &CameraCapture::stillFailed, this, &AnnotateWindow::onPhotoStillFailed);
    m_photoTimer = new QTimer(this);
    m_photoTimer->setInterval(1000);
    connect(m_photoTimer, &QTimer::timeout, this, &AnnotateWindow::onPhotoCountdownTick);
    // ─── Ariadne's Thread [AT-0080] ─────────────────────
    // What: Camera pip is a top-level Qt::Tool window, not a viewport child
    // Why:  WA_NativeWindow on QGraphicsView viewport left a dead QContainerLayer;
    //       RegionPicker / Quit then SIGSEGV in setNeedsDisplayInRect
    // Date: 2026-08-25
    // Related: [AT-0074] AnnotateWindow.cpp:layoutPhotoOverlay, [AT-0079] CameraCapture.mm
    // ─────────────────────────────────────────────────────
    m_photoOverlay = new QWidget(this, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    m_photoOverlay->setAttribute(Qt::WA_ShowWithoutActivating);
    m_photoOverlay->setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    m_photoOverlay->setAttribute(Qt::WA_TranslucentBackground);
    m_photoOverlay->setAttribute(Qt::WA_QuitOnClose, false);
    m_photoOverlay->setAutoFillBackground(false);
    m_photoOverlay->setMouseTracking(true);
    m_photoOverlay->setCursor(Qt::OpenHandCursor);
    m_photoOverlay->installEventFilter(this);
    m_photoOverlay->hide();
    m_photoCountdown = new QLabel(m_photoOverlay);
    m_photoCountdown->setAlignment(Qt::AlignCenter);
    m_photoCountdown->setAutoFillBackground(false);
    m_photoCountdown->setStyleSheet(
        QStringLiteral("color: white; background-color: transparent; font-size: 36px; font-weight: 700;"));
    m_photoCountdown->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    qInfo() << "AnnotateWindow: photo pip is top-level Tool window";
    m_photoFlash = new QWidget(m_view->viewport());
    m_photoFlash->setStyleSheet(QStringLiteral("background: white;"));
    m_photoFlash->hide();
    qInfo() << "AnnotateWindow: photo flash parented to viewport";
    statusBar()->showMessage(QStringLiteral("Select or drag to annotate. Cmd+Z undo."));
    applyCanvasChrome();
    applyWindowScreenLayout();
    ensureUpdateCard();
    qInfo() << "AnnotateWindow: opened" << m_source.width() << "x" << m_source.height();
}

QImage AnnotateWindow::exportedImage() const
{
    const QRectF canvas = canvasRect();
    const QSize size = canvas.size().toSize();
    QImage out(qMax(1, size.width()), qMax(1, size.height()), QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing);
    m_scene->render(&painter, out.rect(), canvas);
    qInfo() << "AnnotateWindow: exported" << out.size() << "canvas=" << canvas << "bg=" << hasBackground();
    return out;
}

QImage AnnotateWindow::shotImage() const
{
    QImage out(m_source.size(), QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing);
    m_scene->render(&painter, out.rect(), shotRect());
    qInfo() << "AnnotateWindow: shot raster" << out.size();
    return out;
}

void AnnotateWindow::closeEvent(QCloseEvent *event)
{
    qInfo() << "AnnotateWindow: close, commit text if needed";
    abortPhotoCycle();
    commitTextEdit();
    if (SparkleUpdater *updater = SparkleUpdater::instance()) {
        updater->editorWillClose(this);
    }
    QMainWindow::closeEvent(event);
}

void AnnotateWindow::keyPressEvent(QKeyEvent *event)
{
    qInfo() << "AnnotateWindow: keyPress" << event->key();
    if (viewKeyPress(event)) {
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void AnnotateWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (!m_didScreenLayout) {
        m_didScreenLayout = true;
        applyWindowScreenLayout();
    }
    fitShotToWindow();
    layoutPhotoOverlay();
    layoutUpdateCard();
    qInfo() << "AnnotateWindow: showEvent layout applied";
}

void AnnotateWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    fitShotToWindow();
    layoutPhotoOverlay();
    layoutUpdateCard();
    qInfo() << "AnnotateWindow: resizeEvent" << event->size();
}

void AnnotateWindow::moveEvent(QMoveEvent *event)
{
    QMainWindow::moveEvent(event);
    layoutPhotoOverlay();
    qInfo() << "AnnotateWindow: moveEvent" << event->pos();
}

void AnnotateWindow::undo()
{
    if (m_editingText) {
        qInfo() << "AnnotateWindow: undo during text edit commits only";
        commitTextEdit();
        return;
    }
    qInfo() << "AnnotateWindow: undo canUndo=" << m_undo->canUndo();
    m_undo->undo();
}

void AnnotateWindow::redo()
{
    if (m_editingText) {
        qInfo() << "AnnotateWindow: redo during text edit commits only";
        commitTextEdit();
        return;
    }
    qInfo() << "AnnotateWindow: redo canRedo=" << m_undo->canRedo();
    m_undo->redo();
}

void AnnotateWindow::setToolSelect()
{
    commitTextEdit();
    m_tool = Tool::Select;
    m_drawing = false;
    syncToolActions();
    qInfo() << "AnnotateWindow: tool=Select";
}

void AnnotateWindow::setToolHighlight()
{
    commitTextEdit();
    m_highlightStyle = HighlightStyle::Fill;
    m_tool = Tool::Highlight;
    m_drawing = false;
    syncToolActions();
    qInfo() << "AnnotateWindow: tool=Highlight style=Fill stroke=" << m_strokeWidth
            << " fillAlpha=" << m_fillAlpha;
}

void AnnotateWindow::setToolArrow()
{
    commitTextEdit();
    m_tool = Tool::Arrow;
    syncToolActions();
    qInfo() << "AnnotateWindow: tool=Arrow";
}

void AnnotateWindow::setToolLine()
{
    commitTextEdit();
    m_tool = Tool::Line;
    syncToolActions();
    qInfo() << "AnnotateWindow: tool=Line";
}

void AnnotateWindow::setToolText()
{
    commitTextEdit();
    m_tool = Tool::Text;
    syncToolActions();
    qInfo() << "AnnotateWindow: tool=Text";
}

void AnnotateWindow::setToolBlur()
{
    commitTextEdit();
    m_tool = Tool::Blur;
    syncToolActions();
    qInfo() << "AnnotateWindow: tool=Blur";
}

void AnnotateWindow::setHighlightStyleTool(HighlightStyle style)
{
    commitTextEdit();
    m_highlightStyle = style;
    m_tool = Tool::Highlight;
    m_drawing = false;
    syncToolActions();
    qInfo() << "AnnotateWindow: tool=Highlight style=" << static_cast<int>(style)
            << "(applies to new strokes only)";
}

void AnnotateWindow::syncToolActions()
{
    QAction *target = nullptr;
    if (m_tool == Tool::Select) {
        target = m_selectAction;
    } else if (m_tool == Tool::Highlight) {
        target = (m_highlightStyle == HighlightStyle::Steps) ? m_styleStepsAction : m_highlightAction;
    } else if (m_tool == Tool::Arrow) {
        target = m_arrowAction;
    } else if (m_tool == Tool::Line) {
        target = m_lineAction;
    } else if (m_tool == Tool::Text) {
        target = m_textToolAction;
    } else if (m_tool == Tool::Blur) {
        target = m_blurAction;
    }
    if (!target) {
        qWarning() << "AnnotateWindow: syncToolActions no action tool=" << static_cast<int>(m_tool);
        return;
    }
    QSignalBlocker block(m_toolGroup);
    target->setChecked(true);
    qInfo() << "AnnotateWindow: syncToolActions checked=" << target->text();
}

QRectF AnnotateWindow::shotRect() const
{
    return QRectF(m_source.rect());
}

QRectF AnnotateWindow::canvasRect() const
{
    if (m_scene && !m_scene->sceneRect().isEmpty()) {
        return m_scene->sceneRect();
    }
    return QRectF(m_source.rect());
}

bool AnnotateWindow::hasBackground() const
{
    return m_bgPreset > 0;
}

bool AnnotateWindow::isOnShot(const QPointF &scenePos) const
{
    return shotRect().contains(scenePos);
}

QPointF AnnotateWindow::clampToShot(const QPointF &scenePos) const
{
    const QRectF r = shotRect();
    return QPointF(qBound(r.left(), scenePos.x(), r.right()), qBound(r.top(), scenePos.y(), r.bottom()));
}

// ─── Ariadne's Thread [AT-0050] ─────────────────────
// What: Annotate window 80% of available screen; shot 80% of window via view transform
// Why:  Requested editor chrome; scene stays in image pixels for export
// Date: 2026-08-25
// Related: [AT-0012] AnnotateWindow.h, [AT-0044] AnnotateWindow.cpp
// ─────────────────────────────────────────────────────
void AnnotateWindow::applyWindowScreenLayout()
{
    QScreen *scr = screen();
    if (!scr) {
        scr = QGuiApplication::primaryScreen();
    }
    if (!scr) {
        qWarning() << "AnnotateWindow: no screen for 80% layout";
        return;
    }
    const QRect avail = scr->availableGeometry();
    const int w = qMax(1, qRound(avail.width() * 0.8));
    const int h = qMax(1, qRound(avail.height() * 0.8));
    const QRect geo(avail.x() + (avail.width() - w) / 2, avail.y() + (avail.height() - h) / 2, w, h);
    setGeometry(geo);
    qInfo() << "AnnotateWindow: window 80% screen geo=" << geo << "avail=" << avail;
}

void AnnotateWindow::fitShotToWindow()
{
    if (!m_view || !m_photo || m_source.isNull()) {
        qWarning() << "AnnotateWindow: fitShot skipped, view or source missing";
        return;
    }
    const QSize win = size();
    const QSize vp = m_view->viewport()->size();
    if (win.isEmpty() || vp.isEmpty()) {
        qInfo() << "AnnotateWindow: fitShot skipped empty window=" << win << "view=" << vp;
        return;
    }
    const QRectF canvas = canvasRect();
    const qreal targetW = win.width() * 0.8;
    const qreal targetH = win.height() * 0.8;
    const qreal sx = targetW / canvas.width();
    const qreal sy = targetH / canvas.height();
    qreal scale = qMin(sx, sy);
    const qreal vsx = static_cast<qreal>(vp.width()) / canvas.width();
    const qreal vsy = static_cast<qreal>(vp.height()) / canvas.height();
    const qreal viewMax = qMin(vsx, vsy);
    if (scale > viewMax) {
        qInfo() << "AnnotateWindow: shot 80% window exceeds view, clamp scale" << scale << "->" << viewMax;
        scale = viewMax;
    }
    QTransform t;
    t.scale(scale, scale);
    m_view->setTransform(t);
    m_view->centerOn(canvas.center());
    qInfo() << "AnnotateWindow: shot scale=" << scale << "window=" << win << "view=" << vp
            << "canvas=" << canvas.size() << "shot=" << m_source.size();
}

qreal AnnotateWindow::maxTextWidthOnShot(const AnnotateTextItem *item) const
{
    if (!item) {
        return 40;
    }
    const qreal maxWidth = shotRect().right() - item->scenePos().x();
    return qMax(item->minTextWidth(), maxWidth);
}

// ─── Ariadne's Thread [AT-0051] ─────────────────────
// What: Picking a Highlight style from the combo also selects the Highlight tool
// Why:  Style choice already means Highlight; a second Highlight click is extra
// Date: 2026-08-25
// Related: [AT-0044] AnnotateWindow.cpp:setToolHighlight, [AT-0045] AnnotateWindow.cpp
// ─────────────────────────────────────────────────────
HighlightStyle AnnotateWindow::currentHighlightStyle() const
{
    qInfo() << "AnnotateWindow: currentHighlightStyle=" << static_cast<int>(m_highlightStyle);
    return m_highlightStyle;
}

void AnnotateWindow::onHuePressed()
{
    ++m_colorGestureId;
    m_colorGestureActive = true;
    qInfo() << "AnnotateWindow: hue gesture start id=" << m_colorGestureId
            << "last=" << lastColorableItem();
}

// ─── Ariadne's Thread [AT-0115] ─────────────────────
// What: Recolor editing text; draft skips undo
// Why:  Empty draft discard must not leave a color undo
// Date: 2026-08-26
// Related: [AT-0114] AnnotateWindow.cpp:lastColorableItem, [AT-0113] AnnotationCommands.cpp
// ─────────────────────────────────────────────────────
void AnnotateWindow::onHueChanged(int hue)
{
    m_color = QColor::fromHsv(hue, 255, 230);
    m_hue->update();
    qInfo() << "AnnotateWindow: hue" << hue << "color=" << m_color;
    QGraphicsItem *last = lastColorableItem();
    if (!last) {
        qInfo() << "AnnotateWindow: no last colorable item, next stroke will use slider color";
        return;
    }
    if (itemColor(last).rgb() == m_color.rgb()) {
        qInfo() << "AnnotateWindow: last item already this color";
        return;
    }
    if (m_editingText && m_textDraft) {
        applyItemColor(m_editingText, m_color);
        qInfo() << "AnnotateWindow: hue on draft text, no undo";
        return;
    }
    const int gesture = m_colorGestureActive ? m_colorGestureId : ++m_colorGestureId;
    m_undo->push(new ChangeColorCommand(last, m_color, gesture));
}

void AnnotateWindow::onHueReleased()
{
    qInfo() << "AnnotateWindow: hue gesture end id=" << m_colorGestureId;
    m_colorGestureActive = false;
}

// ─── Ariadne's Thread [AT-0046] ─────────────────────
// What: Stroke width and fill alpha sliders for last Highlight
// Why:  User can change highlight line weight and fill transparency
// Date: 2026-08-25
// Related: [AT-0045] AnnotateWindow.cpp, [AT-0011] AnnotationCommands.h
// ─────────────────────────────────────────────────────
void AnnotateWindow::onStrokePressed()
{
    ++m_strokeGestureId;
    m_strokeGestureActive = true;
    qInfo() << "AnnotateWindow: stroke gesture start id=" << m_strokeGestureId;
}

void AnnotateWindow::onStrokeChanged(int width)
{
    m_strokeWidth = qBound(1, width, 16);
    qInfo() << "AnnotateWindow: stroke width=" << m_strokeWidth;
    QGraphicsItem *last = lastHighlightItem();
    if (!last) {
        qInfo() << "AnnotateWindow: no last highlight, next stroke will use slider width";
        return;
    }
    if (highlightStrokeWidth(last) == m_strokeWidth) {
        qInfo() << "AnnotateWindow: last highlight already this stroke";
        return;
    }
    const int gesture = m_strokeGestureActive ? m_strokeGestureId : ++m_strokeGestureId;
    m_undo->push(new ChangeStrokeWidthCommand(last, m_strokeWidth, gesture));
}

void AnnotateWindow::onStrokeReleased()
{
    qInfo() << "AnnotateWindow: stroke gesture end id=" << m_strokeGestureId;
    m_strokeGestureActive = false;
}

void AnnotateWindow::onFillPressed()
{
    ++m_fillGestureId;
    m_fillGestureActive = true;
    qInfo() << "AnnotateWindow: fill gesture start id=" << m_fillGestureId;
}

void AnnotateWindow::onFillChanged(int percent)
{
    m_fillAlpha = qBound(0, qRound(percent * 255.0 / 100.0), 255);
    qInfo() << "AnnotateWindow: fill percent=" << percent << "alpha=" << m_fillAlpha;
    QGraphicsItem *last = lastHighlightItem();
    if (!last) {
        qInfo() << "AnnotateWindow: no last highlight, next stroke will use slider fill";
        return;
    }
    if (highlightFillAlpha(last) == m_fillAlpha) {
        qInfo() << "AnnotateWindow: last highlight already this fill";
        return;
    }
    const int gesture = m_fillGestureActive ? m_fillGestureId : ++m_fillGestureId;
    m_undo->push(new ChangeFillAlphaCommand(last, m_fillAlpha, gesture));
}

void AnnotateWindow::onFillReleased()
{
    qInfo() << "AnnotateWindow: fill gesture end id=" << m_fillGestureId;
    m_fillGestureActive = false;
}

void AnnotateWindow::showError(const QString &code)
{
    qWarning() << "AnnotateWindow: showError" << code;
    QMessageBox::warning(this, QStringLiteral("SeenShot"), ErrorCatalog::message(code));
}

bool AnnotateWindow::ensureOnlineSignedIn(QString *errorCode)
{
    if (!m_auth || !m_auth->hasSession()) {
        if (errorCode) {
            *errorCode = QStringLiteral("STORAGE_NEED_SIGN_IN");
        }
        return false;
    }
    if (!m_auth->isOnline()) {
        if (errorCode) {
            *errorCode = QStringLiteral("OFFLINE_CLOUD_UNAVAILABLE");
        }
        return false;
    }
    return true;
}

void AnnotateWindow::saveLocal()
{
    commitTextEdit();
    // ─── Ariadne's Thread [AT-0076] ─────────────────────
    // What: Suggested Save name is SeenShot-localTimeWithSeconds-12hex.png
    // Why:  Fixed seenshot.png collided; each local PNG must be unique
    // Date: 2026-08-25
    // Related: [AT-0057] AnnotateWindow.cpp:share, main.cpp:applicationName
    // ─────────────────────────────────────────────────────
    QString hash;
    hash.reserve(12);
    for (int i = 0; i < 12; ++i) {
        hash.append(QString::number(QRandomGenerator::global()->bounded(16), 16));
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HHmmss"));
    const QString fileName = QStringLiteral("%1-%2-%3.png")
                                 .arg(QApplication::applicationName(), stamp, hash);
    const QString suggested = QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath(fileName);
    qInfo() << "AnnotateWindow: local save dialog start suggested=" << suggested << "stamp=" << stamp
            << "hash=" << hash;
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save PNG"),
        suggested,
        QStringLiteral("PNG (*.png)"));
    if (path.isEmpty()) {
        qInfo() << "AnnotateWindow: local save cancelled";
        return;
    }
    qInfo() << "AnnotateWindow: local save chosen" << path;
    const QImage image = exportedImage();
    if (!image.save(path, "PNG")) {
        qWarning() << "AnnotateWindow: local save failed" << path;
        showError(QStringLiteral("LOCAL_SAVE_FAILED"));
        return;
    }
    qInfo() << "AnnotateWindow: saved local" << path;
    Analytics::instance().track(QStringLiteral("save"));
    statusBar()->showMessage(QStringLiteral("Saved ") + path, 4000);
}

// ─── Ariadne's Thread [AT-0057] ─────────────────────
// What: Drop Save to Cloud; Share is the only cloud upload + publish path
// Why:  Save to Cloud duplicated Share (private upload then publish)
// Date: 2026-08-25
// Related: [AT-0012] AnnotateWindow.cpp:saveLocal, CloudClient.cpp:uploadAndPublish
// ─────────────────────────────────────────────────────
void AnnotateWindow::share()
{
    commitTextEdit();
    QString code;
    if (!m_auth || !m_auth->hasSession()) {
        qInfo() << "AnnotateWindow: share needs sign-in";
        if (!SignInDialog::execShareSignIn(m_auth, this)) {
            qInfo() << "AnnotateWindow: share sign-in cancelled";
            return;
        }
    }
    if (!ensureOnlineSignedIn(&code)) {
        showError(code);
        return;
    }
    QString url;
    CloudConfirmResult result;
    if (!m_cloudShotId.isEmpty()) {
        if (!m_cloud->publishExisting(m_cloudShotId, &url, &code)) {
            showError(code);
            return;
        }
    } else {
        QString encodeCode;
        const QByteArray png = CloudPngEncoder::encode(exportedImage(), &encodeCode);
        if (png.isEmpty()) {
            showError(encodeCode.isEmpty() ? QStringLiteral("CLOUD_IMAGE_REJECTED") : encodeCode);
            return;
        }
        if (!m_cloud->uploadAndPublish(png, &url, &result, &code)) {
            showError(code);
            return;
        }
        m_cloudShotId = result.shotId;
    }
    if (!result.evictedIds.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("SeenShot"), ErrorCatalog::message(QStringLiteral("QUOTA_EVICTED")));
    }
    qInfo() << "AnnotateWindow: published" << url;
    Analytics::instance().track(QStringLiteral("share"));
    QMessageBox::information(this, QStringLiteral("SeenShot"), QStringLiteral("Share link:\n") + url);
}

QGraphicsItem *AnnotateWindow::selectedAnnotation() const
{
    if (!m_scene) {
        return nullptr;
    }
    const auto selected = m_scene->selectedItems();
    for (QGraphicsItem *item : selected) {
        if (item == m_photo || item == m_background || item == m_draft || item->parentItem()) {
            continue;
        }
        if (annotateKind(item) != AnnotateKind::None) {
            qInfo() << "AnnotateWindow: selectedAnnotation kind=" << static_cast<int>(annotateKind(item));
            return item;
        }
    }
    return nullptr;
}

QGraphicsItem *AnnotateWindow::annotationItemAt(const QPointF &scenePos) const
{
    if (!m_scene) {
        return nullptr;
    }
    const auto hits = m_scene->items(scenePos);
    qInfo() << "AnnotateWindow: annotationItemAt hits=" << hits.size() << scenePos;
    for (QGraphicsItem *item : hits) {
        if (item == m_photo || item == m_background || item == m_draft) {
            continue;
        }
        if (item->parentItem()) {
            QGraphicsItem *parent = item->parentItem();
            if (parent && annotateKind(parent) != AnnotateKind::None) {
                qInfo() << "AnnotateWindow: annotationItemAt child -> parent kind="
                        << static_cast<int>(annotateKind(parent));
                return parent;
            }
            continue;
        }
        if (annotateKind(item) != AnnotateKind::None) {
            qInfo() << "AnnotateWindow: annotationItemAt kind=" << static_cast<int>(annotateKind(item));
            return item;
        }
    }
    qInfo() << "AnnotateWindow: annotationItemAt none";
    return nullptr;
}

void AnnotateWindow::selectAnnotation(QGraphicsItem *item)
{
    if (!m_scene) {
        qWarning() << "AnnotateWindow: selectAnnotation no scene";
        return;
    }
    m_scene->clearSelection();
    if (!item) {
        qInfo() << "AnnotateWindow: selection cleared";
        return;
    }
    item->setSelected(true);
    if (m_view) {
        m_view->setFocus(Qt::MouseFocusReason);
        m_view->viewport()->update();
    }
    qInfo() << "AnnotateWindow: selected kind=" << static_cast<int>(annotateKind(item))
            << "selected=" << item->isSelected();
}

// ─── Ariadne's Thread [AT-0114] ─────────────────────
// What: Prefer the text block currently in edit for color
// Why:  Slider must recolor the caret target, not only last square
// Date: 2026-08-26
// Related: [AT-0115] AnnotateWindow.cpp:onHueChanged, [AT-0051] AnnotateWindow.cpp
// ─────────────────────────────────────────────────────
QGraphicsItem *AnnotateWindow::lastColorableItem() const
{
    if (m_editingText) {
        qInfo() << "AnnotateWindow: last colorable is editing text draft=" << m_textDraft;
        return m_editingText;
    }
    if (QGraphicsItem *selected = selectedAnnotation()) {
        if (isColorableKind(annotateKind(selected))) {
            qInfo() << "AnnotateWindow: last colorable is selection kind="
                    << static_cast<int>(annotateKind(selected));
            return selected;
        }
    }
    const auto items = m_scene->items(Qt::AscendingOrder);
    for (int i = items.size() - 1; i >= 0; --i) {
        QGraphicsItem *item = items.at(i);
        if (item == m_photo || item == m_draft || item == m_background) {
            continue;
        }
        if (item->parentItem()) {
            continue;
        }
        const AnnotateKind kind = annotateKind(item);
        if (!isColorableKind(kind)) {
            if (kind == AnnotateKind::Blur) {
                qInfo() << "AnnotateWindow: skip last candidate blur";
            }
            continue;
        }
        qInfo() << "AnnotateWindow: last colorable kind=" << static_cast<int>(annotateKind(item));
        return item;
    }
    qInfo() << "AnnotateWindow: last colorable none";
    return nullptr;
}

QGraphicsItem *AnnotateWindow::lastHighlightItem() const
{
    if (QGraphicsItem *selected = selectedAnnotation()) {
        if (annotateKind(selected) == AnnotateKind::Highlight) {
            qInfo() << "AnnotateWindow: last highlight is selection style="
                    << static_cast<int>(highlightStyle(selected));
            return selected;
        }
    }
    const auto items = m_scene->items(Qt::AscendingOrder);
    for (int i = items.size() - 1; i >= 0; --i) {
        QGraphicsItem *item = items.at(i);
        if (item == m_photo || item == m_draft || item == m_background || item->parentItem()) {
            continue;
        }
        if (annotateKind(item) == AnnotateKind::Highlight) {
            qInfo() << "AnnotateWindow: last highlight style=" << static_cast<int>(highlightStyle(item));
            return item;
        }
    }
    qInfo() << "AnnotateWindow: last highlight none";
    return nullptr;
}

void AnnotateWindow::setBackgroundPreset(int preset)
{
    const int next = qBound(0, preset, 5);
    qInfo() << "AnnotateWindow: setBackgroundPreset" << m_bgPreset << "->" << next;
    m_bgPreset = next;
    if (m_bgButton) {
        m_bgButton->setIcon(backgroundCircleIcon(m_bgPreset));
        m_bgButton->setToolTip(QStringLiteral("Background"));
    }
    if (m_bgGroup) {
        const auto acts = m_bgGroup->actions();
        for (QAction *act : acts) {
            QSignalBlocker block(act);
            act->setChecked(act->data().toInt() == m_bgPreset);
            qInfo() << "AnnotateWindow: background action check id=" << act->data().toInt()
                    << "on=" << act->isChecked();
        }
    }
    applyCanvasChrome();
    fitShotToWindow();
}

void AnnotateWindow::onRadiusChanged(int radius)
{
    m_cornerRadius = qBound(0, radius, 80);
    qInfo() << "AnnotateWindow: corner radius=" << m_cornerRadius;
    applyCanvasChrome();
}

void AnnotateWindow::onShadowChanged(int amount)
{
    m_shadowAmount = qBound(0, amount, 40);
    qInfo() << "AnnotateWindow: shadow amount=" << m_shadowAmount;
    applyCanvasChrome();
}

// ─── Ariadne's Thread [AT-0056] ─────────────────────
// What: Gradient backdrop, rounded shot clip, drop shadow between shot and backdrop
// Why:  Requested canvas chrome under the screenshot; annotations stay in shot pixels
// Date: 2026-08-25
// Related: [AT-0056] AnnotateItems.h:ShotPhotoItem, [AT-0050] AnnotateWindow.cpp:fitShotToWindow
// ─────────────────────────────────────────────────────
void AnnotateWindow::applyCanvasChrome()
{
    if (!m_photo || !m_background || !m_photoShadow) {
        qWarning() << "AnnotateWindow: applyCanvasChrome missing photo/backdrop";
        return;
    }
    const bool on = hasBackground();
    if (m_radius) {
        m_radius->setEnabled(on);
    }
    if (m_shadow) {
        m_shadow->setEnabled(on);
    }
    if (m_radiusLabel) {
        m_radiusLabel->setEnabled(on);
    }
    if (m_shadowLabel) {
        m_shadowLabel->setEnabled(on);
    }
    if (!on) {
        m_background->setVisible(false);
        m_photo->setCornerRadius(0);
        m_photoShadow->setEnabled(false);
        m_scene->setSceneRect(m_source.rect());
        qInfo() << "AnnotateWindow: canvas chrome off scene=" << m_scene->sceneRect();
        return;
    }

    const int preset = m_bgPreset;
    QColor a;
    QColor b;
    backgroundPresetColors(preset, &a, &b);

    const qreal basePad = qMax(48.0, qMin(m_source.width(), m_source.height()) * 0.12);
    const qreal pad = basePad + static_cast<qreal>(m_shadowAmount) * 1.6;
    const QRectF canvas(-pad, -pad, m_source.width() + pad * 2.0, m_source.height() + pad * 2.0);
    m_scene->setSceneRect(canvas);
    m_background->setRect(canvas);
    QLinearGradient gradient(canvas.topLeft(), canvas.bottomRight());
    gradient.setColorAt(0, a);
    gradient.setColorAt(1, b);
    m_background->setBrush(gradient);
    m_background->setVisible(true);
    m_photo->setCornerRadius(m_cornerRadius);
    m_photoShadow->setBlurRadius(m_shadowAmount);
    m_photoShadow->setOffset(0, m_shadowAmount * 0.35);
    m_photoShadow->setEnabled(m_shadowAmount > 0);
    qInfo() << "AnnotateWindow: canvas chrome on preset=" << preset << "pad=" << pad
            << "radius=" << m_cornerRadius << "shadow=" << m_shadowAmount
            << "from=" << a << "to=" << b << "scene=" << canvas;
}

// ─── Ariadne's Thread [AT-0080] ─────────────────────
// What: Place the Tool pip in global coords over the shot bottom-right
// Why:  Viewport-child native view died on occlusion; Tool window is a separate NSWindow
// Date: 2026-08-25
// Related: [AT-0068] AnnotateWindow.cpp:layoutPhotoOverlay, [AT-0079] CameraCapture.mm
// ─────────────────────────────────────────────────────
// ─── Ariadne's Thread [AT-0128] ─────────────────────
// What: Keep the live pip on shotRect and honor a user drag position
// Why:  Fixed bottom-right Tool window was easy to lose and could not be moved
// Date: 2026-08-26
// Related: [AT-0126] MacPermissions.mm:pinFloatingToolWindow, [AT-0080] AnnotateWindow.cpp
// ─────────────────────────────────────────────────────
void AnnotateWindow::layoutPhotoOverlay()
{
    if (!m_photoOverlay || !m_view || !m_view->viewport()) {
        qWarning() << "AnnotateWindow: layoutPhotoOverlay missing view";
        return;
    }
    if (m_photoPipDragging) {
        qInfo() << "AnnotateWindow: layoutPhotoOverlay skipped during pip drag";
        return;
    }
    QWidget *viewport = m_view->viewport();
    const QRect vp = viewport->rect();
    const QRect shotVp = m_view->mapFromScene(shotRect()).boundingRect();
    const int pipW = qBound(180, qRound(qMin(shotVp.width(), vp.width()) * 0.36), 360);
    const int pipH = qRound(pipW * 3.0 / 4.0);
    const int margin = 8;
    QRect pip(0, 0, pipW, pipH);
    if (m_photoPipUserMoved) {
        const QPoint topLeft = m_view->mapFromScene(m_photoPipSceneTopLeft);
        pip.moveTopLeft(topLeft);
        qInfo() << "AnnotateWindow: photo pip restore scene=" << m_photoPipSceneTopLeft << " local=" << topLeft;
    } else {
        const QPoint br = shotVp.bottomRight();
        pip.moveTopLeft(QPoint(br.x() - pipW - margin, br.y() - pipH - margin));
    }
    const QRect limit = photoPipClampRect();
    if (pip.width() >= limit.width()) {
        pip.moveLeft(limit.left());
    } else {
        pip.moveLeft(qBound(limit.left(), pip.left(), limit.right() - pip.width() + 1));
    }
    if (pip.height() >= limit.height()) {
        pip.moveTop(limit.top());
    } else {
        pip.moveTop(qBound(limit.top(), pip.top(), limit.bottom() - pip.height() + 1));
    }
    const QRect globalPip(viewport->mapToGlobal(pip.topLeft()), pip.size());
    m_photoOverlay->setGeometry(globalPip);
    if (m_photoOverlay->isVisible()) {
        MacPermissions::pinFloatingToolWindow(m_photoOverlay);
    }
    if (m_photoCountdown) {
        m_photoCountdown->setGeometry(m_photoOverlay->rect());
        m_photoCountdown->raise();
    }
    if (m_photoFlash) {
        m_photoFlash->setGeometry(vp);
    }
    if (m_camera && m_camera->isRunning()) {
        m_camera->syncPreviewLayer();
    }
    qInfo() << "AnnotateWindow: photo pip local=" << pip << "global=" << globalPip << "shotVp=" << shotVp
            << "viewport=" << vp << "visible=" << m_photoOverlay->isVisible()
            << " userMoved=" << m_photoPipUserMoved;
}

QRect AnnotateWindow::photoPipClampRect() const
{
    if (!m_view || !m_view->viewport()) {
        qWarning() << "AnnotateWindow: photoPipClampRect missing view";
        return QRect();
    }
    const QRect vp = m_view->viewport()->rect();
    const QRect shotVp = m_view->mapFromScene(shotRect()).boundingRect();
    const QRect limit = shotVp.intersected(vp);
    qInfo() << "AnnotateWindow: photo pip clamp=" << limit << " shotVp=" << shotVp << " vp=" << vp;
    return limit;
}

void AnnotateWindow::movePhotoOverlayToGlobal(const QPoint &globalTopLeft)
{
    if (!m_photoOverlay || !m_view || !m_view->viewport()) {
        qWarning() << "AnnotateWindow: movePhotoOverlayToGlobal missing overlay";
        return;
    }
    QWidget *viewport = m_view->viewport();
    const QSize size = m_photoOverlay->size();
    const QRect limitLocal = photoPipClampRect();
    const QRect limit(viewport->mapToGlobal(limitLocal.topLeft()), limitLocal.size());
    QRect pip(globalTopLeft, size);
    if (pip.width() >= limit.width()) {
        pip.moveLeft(limit.left());
    } else {
        pip.moveLeft(qBound(limit.left(), pip.left(), limit.right() - pip.width() + 1));
    }
    if (pip.height() >= limit.height()) {
        pip.moveTop(limit.top());
    } else {
        pip.moveTop(qBound(limit.top(), pip.top(), limit.bottom() - pip.height() + 1));
    }
    m_photoOverlay->setGeometry(pip);
    persistPhotoPipScenePos();
    if (m_camera && m_camera->isRunning()) {
        m_camera->syncPreviewLayer();
    }
    qInfo() << "AnnotateWindow: photo pip dragged global=" << pip << " scene=" << m_photoPipSceneTopLeft;
}

void AnnotateWindow::persistPhotoPipScenePos()
{
    if (!m_photoOverlay || !m_view || !m_view->viewport()) {
        qWarning() << "AnnotateWindow: persistPhotoPipScenePos missing overlay";
        return;
    }
    const QPoint local = m_view->viewport()->mapFromGlobal(m_photoOverlay->frameGeometry().topLeft());
    m_photoPipSceneTopLeft = m_view->mapToScene(local);
    m_photoPipUserMoved = true;
    qInfo() << "AnnotateWindow: photo pip persist scene=" << m_photoPipSceneTopLeft << " local=" << local;
}

void AnnotateWindow::syncPhotoButtonChecked()
{
    if (!m_photoButton) {
        qWarning() << "AnnotateWindow: syncPhotoButtonChecked missing button";
        return;
    }
    const bool on = m_photoCycle;
    const QSignalBlocker blocker(m_photoButton);
    m_photoButton->setChecked(on);
    qInfo() << "AnnotateWindow: Photo button checked=" << on << " cycle=" << m_photoCycle;
}

void AnnotateWindow::setPhotoPipSelected(bool selected)
{
    if (m_photoPipSelected == selected) {
        if (m_camera) {
            m_camera->setPreviewSelected(selected);
        }
        qInfo() << "AnnotateWindow: photo pip selected unchanged=" << selected;
        return;
    }
    m_photoPipSelected = selected;
    qInfo() << "AnnotateWindow: photo pip selected=" << selected;
    if (m_camera) {
        m_camera->setPreviewSelected(selected);
    }
}

// ─── Ariadne's Thread [AT-0061] ─────────────────────
// What: Hand Camera-denied Photo to System Settings without a stay-on-top modal
// Why:  QMessageBox on WindowStaysOnTopHint covered the Camera TCC pane
// Date: 2026-08-25
// Related: [AT-0061] MacPermissions.mm:openCameraSettings, docs/PRD-03-photo-cutout.md
// ─────────────────────────────────────────────────────
// ─── Ariadne's Thread [AT-0075] ─────────────────────
// What: Photo click only opens the pip; Picture / 5s timer shoot with 500ms+shot+100ms flash
// Why:  WindowStaysOnTopHint pinned the editor; 3-2-1 auto-fired before the user posed
// Date: 2026-08-25
// Related: [AT-0061] MacPermissions.mm:openCameraSettings, [AT-0068] AnnotateWindow.cpp
// ─────────────────────────────────────────────────────
bool AnnotateWindow::photoCaptureBusy() const
{
    return m_photoFlashPhase != PhotoFlash::Idle;
}

void AnnotateWindow::togglePhotoPreview()
{
    qInfo() << "AnnotateWindow: toggle Photo cycle=" << m_photoCycle
            << " flashBusy=" << photoCaptureBusy()
            << " buttonChecked=" << (m_photoButton && m_photoButton->isChecked());
    if (photoCaptureBusy()) {
        qInfo() << "AnnotateWindow: Photo toggle ignored during flash";
        syncPhotoButtonChecked();
        return;
    }
    if (m_photoCycle) {
        abortPhotoCycle();
        return;
    }
    startPhotoWithFollow(PhotoFollow::PreviewOnly);
}

void AnnotateWindow::startPhotoPicture()
{
    startPhotoWithFollow(PhotoFollow::Picture);
}

void AnnotateWindow::startPhotoTimer5()
{
    startPhotoWithFollow(PhotoFollow::Timer5);
}

void AnnotateWindow::startPhotoWithFollow(PhotoFollow follow)
{
    commitTextEdit();
    if (photoCaptureBusy()) {
        qInfo() << "AnnotateWindow: Photo follow ignored, flash phase="
                << static_cast<int>(m_photoFlashPhase);
        return;
    }
    m_photoFollow = follow;
    qInfo() << "AnnotateWindow: Photo follow=" << static_cast<int>(follow);
    if (m_photoCycle) {
        if (m_camera && m_camera->isRunning()) {
            QTimer::singleShot(0, this, [this]() {
                if (!photoCaptureBusy()) {
                    runPhotoFollow();
                }
            });
            return;
        }
        qInfo() << "AnnotateWindow: Photo follow stored while camera pending";
        return;
    }
    m_photoCycle = true;
    syncPhotoButtonChecked();
    const int token = m_photoToken;
    MacPermissions::requestCamera([this, token](bool granted) {
        if (token != m_photoToken) {
            qInfo() << "AnnotateWindow: camera callback stale token=" << token;
            return;
        }
        if (!granted) {
            qWarning() << "AnnotateWindow: camera denied, opening Camera privacy settings";
            statusBar()->showMessage(ErrorCatalog::message(QStringLiteral("CAMERA_DENIED")));
            MacPermissions::openCameraSettings();
            m_photoFollow = PhotoFollow::None;
            m_photoCycle = false;
            syncPhotoButtonChecked();
            return;
        }
        QPointer<AnnotateWindow> alive(this);
        const int startToken = m_photoToken;
        QTimer::singleShot(0, this, [alive, startToken]() {
            if (!alive) {
                qWarning() << "AnnotateWindow: photo follow after destroy";
                return;
            }
            if (startToken != alive->m_photoToken) {
                qInfo() << "AnnotateWindow: deferred preview stale token=" << startToken;
                return;
            }
            if (!alive->ensurePhotoPreview()) {
                alive->m_photoFollow = PhotoFollow::None;
                alive->syncPhotoButtonChecked();
                return;
            }
            alive->runPhotoFollow();
        });
    });
}

bool AnnotateWindow::ensurePhotoPreview()
{
    if (!m_photoOverlay || !m_camera) {
        qWarning() << "AnnotateWindow: photo overlay missing";
        showError(QStringLiteral("PHOTO_CAPTURE_FAILED"));
        return false;
    }
    if (m_photoCycle && m_camera->isRunning()) {
        qInfo() << "AnnotateWindow: preview already running";
        layoutPhotoOverlay();
        syncPhotoButtonChecked();
        return true;
    }
    m_photoCycle = true;
    if (m_photoFlash) {
        m_photoFlash->hide();
    }
    if (m_photoCountdown) {
        m_photoCountdown->hide();
    }
    layoutPhotoOverlay();
    m_photoOverlay->show();
    m_photoOverlay->raise();
    layoutPhotoOverlay();
    QString code;
    if (!m_camera->startPreview(m_photoOverlay, &code)) {
        abortPhotoCycle();
        showError(code.isEmpty() ? QStringLiteral("CAMERA_UNAVAILABLE") : code);
        return false;
    }
    layoutPhotoOverlay();
    setPhotoPipSelected(true);
    syncPhotoButtonChecked();
    qInfo() << "AnnotateWindow: preview shown pip=" << m_photoOverlay->geometry()
            << " topLevel=" << m_photoOverlay->isWindow();
    return true;
}

void AnnotateWindow::runPhotoFollow()
{
    const PhotoFollow follow = m_photoFollow;
    m_photoFollow = PhotoFollow::None;
    qInfo() << "AnnotateWindow: runPhotoFollow" << static_cast<int>(follow);
    if (follow == PhotoFollow::PreviewOnly) {
        qInfo() << "AnnotateWindow: preview only, menu stays on the Photo chevron";
        return;
    }
    if (follow == PhotoFollow::Picture) {
        if (m_photoTimer) {
            m_photoTimer->stop();
        }
        if (m_photoCountdown) {
            m_photoCountdown->hide();
        }
        beginPhotoFlashCapture();
        return;
    }
    if (follow == PhotoFollow::Timer5) {
        m_photoCount = 5;
        if (m_photoCountdown) {
            m_photoCountdown->setText(QStringLiteral("5"));
            m_photoCountdown->show();
            m_photoCountdown->raise();
        }
        if (m_photoTimer) {
            m_photoTimer->start();
        }
        qInfo() << "AnnotateWindow: 5s timer started";
    }
}

void AnnotateWindow::showPhotoCaptureMenu()
{
    if (!m_photoButton || !m_photoButton->menu()) {
        qWarning() << "AnnotateWindow: showPhotoCaptureMenu missing button/menu";
        return;
    }
    if (photoCaptureBusy()) {
        qInfo() << "AnnotateWindow: showPhotoCaptureMenu skipped, flash busy";
        return;
    }
    qInfo() << "AnnotateWindow: show Photo menu after camera preview";
    m_photoButton->showMenu();
}

void AnnotateWindow::beginPhotoFlashCapture()
{
    if (photoCaptureBusy()) {
        qInfo() << "AnnotateWindow: beginPhotoFlashCapture ignored busy";
        return;
    }
    if (!m_camera || !m_photoFlash) {
        qWarning() << "AnnotateWindow: beginPhotoFlashCapture missing camera/flash";
        return;
    }
    if (m_photoTimer) {
        m_photoTimer->stop();
    }
    if (m_photoCountdown) {
        m_photoCountdown->hide();
    }
    m_photoFlashPhase = PhotoFlash::Pre;
    m_photoFlash->show();
    m_photoFlash->raise();
    const int token = m_photoToken;
    qInfo() << "AnnotateWindow: flash pre 500ms token=" << token;
    QTimer::singleShot(500, this, [this, token]() {
        if (token != m_photoToken || m_photoFlashPhase != PhotoFlash::Pre) {
            qInfo() << "AnnotateWindow: flash pre stale token=" << token;
            return;
        }
        m_photoFlashPhase = PhotoFlash::Capturing;
        qInfo() << "AnnotateWindow: flash on, capture still";
        if (m_camera) {
            m_camera->captureStill();
        }
    });
}

void AnnotateWindow::onPhotoCountdownTick()
{
    if (photoCaptureBusy()) {
        qInfo() << "AnnotateWindow: countdown tick ignored during flash";
        return;
    }
    --m_photoCount;
    qInfo() << "AnnotateWindow: 5s timer tick=" << m_photoCount;
    if (m_photoCount > 0) {
        if (m_photoCountdown) {
            m_photoCountdown->setText(QString::number(m_photoCount));
        }
        return;
    }
    if (m_photoTimer) {
        m_photoTimer->stop();
    }
    if (m_photoCountdown) {
        m_photoCountdown->hide();
    }
    beginPhotoFlashCapture();
}

void AnnotateWindow::onPhotoStillReady(const QImage &image)
{
    qInfo() << "AnnotateWindow: still ready" << image.size() << "phase=" << static_cast<int>(m_photoFlashPhase);
    if (m_photoFlashPhase != PhotoFlash::Capturing) {
        qWarning() << "AnnotateWindow: still ready outside capture phase";
        return;
    }
    m_pendingStill = image;
    m_photoFlashPhase = PhotoFlash::Post;
    if (m_photoFlash) {
        m_photoFlash->show();
        m_photoFlash->raise();
    }
    const int token = m_photoToken;
    qInfo() << "AnnotateWindow: flash post 100ms token=" << token;
    QTimer::singleShot(100, this, [this, token]() {
        if (token != m_photoToken || m_photoFlashPhase != PhotoFlash::Post) {
            qInfo() << "AnnotateWindow: flash post stale token=" << token;
            return;
        }
        const QImage still = m_pendingStill;
        m_pendingStill = QImage();
        finishPhotoStill(still);
    });
}

void AnnotateWindow::finishPhotoStill(const QImage &image)
{
    qInfo() << "AnnotateWindow: finishPhotoStill" << image.size();
    abortPhotoCycle(false);
    QString code;
    const QImage cutout = cutOutPerson(image, &code);
    if (cutout.isNull()) {
        showError(code.isEmpty() ? QStringLiteral("PHOTO_NO_PERSON") : code);
        emit photoCycleEnded();
        return;
    }
    placePhotoCutout(cutout);
    emit photoCycleEnded();
}

void AnnotateWindow::onPhotoStillFailed(const QString &code)
{
    qWarning() << "AnnotateWindow: still failed" << code;
    abortPhotoCycle();
    showError(code.isEmpty() ? QStringLiteral("PHOTO_CAPTURE_FAILED") : code);
}

void AnnotateWindow::abortPhoto()
{
    abortPhotoCycle();
}

void AnnotateWindow::abortPhotoCycle(bool notifyEnded)
{
    const bool running = m_photoCycle || (m_camera && m_camera->isRunning()) || photoCaptureBusy();
    ++m_photoToken;
    if (m_photoTimer) {
        m_photoTimer->stop();
    }
    if (m_camera) {
        m_camera->stop();
    }
    if (m_photoOverlay) {
        m_photoOverlay->hide();
        if (QWindow *win = m_photoOverlay->windowHandle()) {
            win->destroy();
            qInfo() << "AnnotateWindow: photo Tool QWindow destroyed";
        } else {
            qInfo() << "AnnotateWindow: photo Tool had no QWindow";
        }
    }
    if (m_photoFlash) {
        m_photoFlash->hide();
    }
    if (m_photoCountdown) {
        m_photoCountdown->hide();
    }
    m_photoCycle = false;
    m_photoCount = 0;
    m_photoFollow = PhotoFollow::None;
    m_photoFlashPhase = PhotoFlash::Idle;
    m_pendingStill = QImage();
    m_photoPipUserMoved = false;
    m_photoPipDragging = false;
    if (m_photoOverlay && m_photoOverlay->mouseGrabber() == m_photoOverlay) {
        m_photoOverlay->releaseMouse();
        qInfo() << "AnnotateWindow: photo pip released mouse grab";
    }
    setPhotoPipSelected(false);
    syncPhotoButtonChecked();
    if (running) {
        qInfo() << "AnnotateWindow: photo cycle aborted token=" << m_photoToken
                << " notifyEnded=" << notifyEnded;
        if (notifyEnded) {
            emit photoCycleEnded();
        }
    }
}

void AnnotateWindow::placePhotoCutout(const QImage &cutout)
{
    auto *item = new AnnotatePhotoItem(QPixmap::fromImage(cutout));
    const QRectF shot = shotRect();
    const qreal shorter = qMin(shot.width(), shot.height());
    const qreal target = shorter * 0.3;
    const qreal pixShort = qMax(1.0, static_cast<qreal>(qMin(cutout.width(), cutout.height())));
    item->setPhotoScale(target / pixShort);
    const QSizeF logical(cutout.width() * item->photoScale(), cutout.height() * item->photoScale());
    item->setPos(shot.center().x() - logical.width() / 2.0, shot.center().y() - logical.height() / 2.0);
    clampPhotoToShot(item);
    m_undo->push(new AddItemCommand(m_scene, item));
    qInfo() << "AnnotateWindow: photo placed pos=" << item->pos() << "scale=" << item->photoScale()
            << "size=" << cutout.size();
}

void AnnotateWindow::clampPhotoToShot(AnnotatePhotoItem *item) const
{
    if (!item) {
        return;
    }
    const QRectF shot = shotRect();
    QRectF box = item->sceneBox();
    QPointF p = item->pos();
    if (box.left() < shot.left()) {
        p.rx() += shot.left() - box.left();
    }
    if (box.top() < shot.top()) {
        p.ry() += shot.top() - box.top();
    }
    box = QRectF(p, box.size());
    if (box.right() > shot.right()) {
        p.rx() -= box.right() - shot.right();
    }
    if (box.bottom() > shot.bottom()) {
        p.ry() -= box.bottom() - shot.bottom();
    }
    item->setPos(p);
    qInfo() << "AnnotateWindow: photo clamped pos=" << p << "box=" << item->sceneBox();
}

AnnotatePhotoItem *AnnotateWindow::photoItemAt(const QPointF &scenePos) const
{
    QGraphicsItem *hit = m_scene->itemAt(scenePos, m_view->transform());
    while (hit) {
        if (auto *photo = qgraphicsitem_cast<AnnotatePhotoItem *>(hit)) {
            qInfo() << "AnnotateWindow: photoItemAt hit";
            return photo;
        }
        hit = hit->parentItem();
    }
    return nullptr;
}

// ─── Ariadne's Thread [AT-0111] ─────────────────────
// What: Hit the text block box, not only glyph shape
// Why:  itemAt missed padding; Text tool stacked a second block
// Date: 2026-08-26
// Related: [AT-0040] AnnotateItems.h:AnnotateTextItem, docs/PRD-02-annotate-tools.md
// ─────────────────────────────────────────────────────
AnnotateTextItem *AnnotateWindow::textItemAt(const QPointF &scenePos) const
{
    if (!m_scene) {
        return nullptr;
    }
    const auto items = m_scene->items(Qt::DescendingOrder);
    for (QGraphicsItem *item : items) {
        auto *text = qgraphicsitem_cast<AnnotateTextItem *>(item);
        if (!text) {
            continue;
        }
        if (text->sceneBoundingRect().contains(scenePos)) {
            qInfo() << "AnnotateWindow: textItemAt hit box=" << text->sceneBoundingRect();
            return text;
        }
    }
    qInfo() << "AnnotateWindow: textItemAt none at" << scenePos;
    return nullptr;
}

void AnnotateWindow::beginNewText(const QPointF &scenePos)
{
    auto *item = new AnnotateTextItem;
    item->setPos(scenePos);
    item->setDefaultTextColor(m_color);
    item->setTextWidth(qMin(item->textWidth(), maxTextWidthOnShot(item)));
    m_scene->addItem(item);
    qInfo() << "AnnotateWindow: new text draft at" << scenePos << "color=" << m_color
            << "width=" << item->textWidth();
    beginEditText(item, true);
}

// ─── Ariadne's Thread [AT-0116] ─────────────────────
// What: Select the text being edited
// Why:  lastColorableItem and chrome follow the caret block
// Date: 2026-08-26
// Related: [AT-0114] AnnotateWindow.cpp:lastColorableItem, [AT-0111] AnnotateWindow.cpp:textItemAt
// ─────────────────────────────────────────────────────
void AnnotateWindow::beginEditText(AnnotateTextItem *item, bool draft)
{
    if (!item) {
        qWarning() << "AnnotateWindow: beginEditText null";
        return;
    }
    if (m_editingText && m_editingText != item) {
        commitTextEdit();
    }
    m_editingText = item;
    m_textDraft = draft;
    m_editOldText = item->toPlainText();
    selectAnnotation(item);
    item->beginEdit();
    if (m_undoAction) {
        m_undoAction->setShortcut(QKeySequence());
    }
    if (m_redoAction) {
        m_redoAction->setShortcut(QKeySequence());
    }
    m_view->setFocus();
    item->setFocus(Qt::MouseFocusReason);
    qInfo() << "AnnotateWindow: text edit begin draft=" << draft << "oldLen=" << m_editOldText.size();
}

void AnnotateWindow::beginFillTextCaption(QGraphicsRectItem *rect)
{
    if (!rect) {
        qWarning() << "AnnotateWindow: beginFillTextCaption null rect";
        return;
    }
    auto *caption = new AnnotateTextItem;
    caption->setParentItem(rect);
    const QRectF box = rect->rect();
    caption->setPos(box.topLeft() + QPointF(4, 4));
    caption->setTextWidth(qMax(caption->minTextWidth(), box.width() - 8));
    caption->setDefaultTextColor(m_color);
    m_fillTextRect = rect;
    qInfo() << "AnnotateWindow: Fill+text caption start rect=" << box;
    beginEditText(caption, false);
}

void AnnotateWindow::commitTextEdit()
{
    if (!m_editingText) {
        return;
    }
    AnnotateTextItem *item = m_editingText;
    const QString text = item->toPlainText();
    const bool empty = text.trimmed().isEmpty();
    const bool draft = m_textDraft;
    QGraphicsRectItem *fillRect = m_fillTextRect;
    qInfo() << "AnnotateWindow: text commit empty=" << empty << "draft=" << draft
            << "fillText=" << (fillRect != nullptr) << "len=" << text.size();
    item->endEdit();
    m_editingText = nullptr;
    m_textDraft = false;
    m_fillTextRect = nullptr;
    if (m_undoAction) {
        m_undoAction->setShortcut(QKeySequence::Undo);
    }
    if (m_redoAction) {
        m_redoAction->setShortcut(QKeySequence::Redo);
    }

    if (fillRect) {
        if (empty) {
            delete item;
            qInfo() << "AnnotateWindow: Fill+text empty caption discarded, rect kept";
            return;
        }
        if (text != m_editOldText) {
            m_undo->push(new ChangeTextCommand(item, m_editOldText, text));
            qInfo() << "AnnotateWindow: Fill+text caption committed";
        }
        return;
    }
    if (draft) {
        if (empty) {
            m_scene->removeItem(item);
            delete item;
            qInfo() << "AnnotateWindow: empty draft text discarded";
            return;
        }
        m_scene->removeItem(item);
        m_undo->push(new AddItemCommand(m_scene, item));
        qInfo() << "AnnotateWindow: committed text add";
        return;
    }
    if (empty) {
        m_undo->push(new RemoveItemCommand(m_scene, item));
        qInfo() << "AnnotateWindow: emptied text removed";
        return;
    }
    if (text != m_editOldText) {
        m_undo->push(new ChangeTextCommand(item, m_editOldText, text));
        qInfo() << "AnnotateWindow: committed text edit";
    }
}

// ─── Ariadne's Thread [AT-0072] ─────────────────────
// What: Select move/resize/delete on the current annotation
// Why:  Select only set ItemIsSelectable; Del/Backspace and grips were missing
// Date: 2026-08-25
// Related: [AT-0066] AnnotateWindow.cpp:setToolSelect, [AT-0011] RemoveItemCommand
// ─────────────────────────────────────────────────────
bool AnnotateWindow::hitsScenePoint(const QPointF &target, const QPointF &scenePos) const
{
    if (!m_view) {
        return false;
    }
    const QPoint a = m_view->mapFromScene(target);
    const QPoint b = m_view->mapFromScene(scenePos);
    const bool hit = QLineF(QPointF(a), QPointF(b)).length() <= 10.0;
    qInfo() << "AnnotateWindow: hitsScenePoint" << hit << "target=" << target << "pos=" << scenePos;
    return hit;
}

QRectF AnnotateWindow::itemSceneBox(QGraphicsItem *item) const
{
    if (!item) {
        return {};
    }
    if (auto *rect = qgraphicsitem_cast<QGraphicsRectItem *>(item)) {
        return rect->mapRectToScene(rect->rect());
    }
    return item->mapRectToScene(item->boundingRect());
}

AnnotateWindow::SelectHandle AnnotateWindow::hitSelectHandle(QGraphicsItem *item, const QPointF &scenePos) const
{
    if (!item) {
        return SelectHandle::None;
    }
    const AnnotateKind kind = annotateKind(item);
    if (kind == AnnotateKind::Arrow || kind == AnnotateKind::Line) {
        const QPointF p1 = item->mapToScene(annotateP1(item));
        const QPointF p2 = item->mapToScene(annotateP2(item));
        if (hitsScenePoint(p1, scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle P1 kind=" << static_cast<int>(kind);
            return SelectHandle::P1;
        }
        if (hitsScenePoint(p2, scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle P2 kind=" << static_cast<int>(kind);
            return SelectHandle::P2;
        }
        return SelectHandle::None;
    }
    if (kind == AnnotateKind::Highlight || kind == AnnotateKind::Blur) {
        const QRectF box = itemSceneBox(item);
        if (hitsScenePoint(box.topLeft(), scenePos)) {
            return SelectHandle::TopLeft;
        }
        if (hitsScenePoint(box.topRight(), scenePos)) {
            return SelectHandle::TopRight;
        }
        if (hitsScenePoint(box.bottomLeft(), scenePos)) {
            return SelectHandle::BottomLeft;
        }
        if (hitsScenePoint(box.bottomRight(), scenePos)) {
            return SelectHandle::BottomRight;
        }
        qInfo() << "AnnotateWindow: hitSelectHandle none on box=" << box;
        return SelectHandle::None;
    }
    return SelectHandle::None;
}

void AnnotateWindow::clampItemToShot(QGraphicsItem *item) const
{
    if (!item) {
        return;
    }
    const QRectF shot = shotRect();
    QRectF box = itemSceneBox(item);
    QPointF p = item->pos();
    if (box.left() < shot.left()) {
        p.rx() += shot.left() - box.left();
    }
    if (box.top() < shot.top()) {
        p.ry() += shot.top() - box.top();
    }
    box = itemSceneBox(item);
    box.translate(p - item->pos());
    if (box.right() > shot.right()) {
        p.rx() -= box.right() - shot.right();
    }
    if (box.bottom() > shot.bottom()) {
        p.ry() -= box.bottom() - shot.bottom();
    }
    item->setPos(p);
    qInfo() << "AnnotateWindow: clampItemToShot pos=" << p << "box=" << itemSceneBox(item);
}

void AnnotateWindow::applyBlurSceneRect(QGraphicsPixmapItem *item, const QRectF &sceneRect)
{
    if (!item) {
        qWarning() << "AnnotateWindow: applyBlurSceneRect null";
        return;
    }
    const QRect shot = shotRect().toRect();
    const QRect clipped = sceneRect.toRect().normalized().intersected(shot);
    if (clipped.width() < 8 || clipped.height() < 8) {
        qInfo() << "AnnotateWindow: applyBlurSceneRect skip tiny" << clipped;
        return;
    }
    item->setVisible(false);
    const QImage source = shotImage().copy(clipped.translated(-shot.topLeft()));
    item->setVisible(true);
    item->setPos(clipped.topLeft());
    setBlurSource(item, source);
    applyBlurRadius(item, blurRadius(item));
    qInfo() << "AnnotateWindow: applyBlurSceneRect" << clipped << "source=" << source.size();
}

void AnnotateWindow::applySelectResize(const QPointF &scenePos)
{
    if (!m_selectItem) {
        qWarning() << "AnnotateWindow: applySelectResize no item";
        return;
    }
    const QPointF clamped = clampToShot(scenePos);
    const AnnotateKind kind = annotateKind(m_selectItem);
    if (kind == AnnotateKind::Highlight) {
        auto *rect = qgraphicsitem_cast<QGraphicsRectItem *>(m_selectItem);
        if (!rect) {
            qWarning() << "AnnotateWindow: applySelectResize highlight not rect";
            return;
        }
        QRectF box = rect->mapRectToScene(rect->rect());
        switch (m_selectHandle) {
        case SelectHandle::TopLeft:
            box.setTopLeft(clamped);
            break;
        case SelectHandle::TopRight:
            box.setTopRight(clamped);
            break;
        case SelectHandle::BottomLeft:
            box.setBottomLeft(clamped);
            break;
        case SelectHandle::BottomRight:
            box.setBottomRight(clamped);
            break;
        default:
            qWarning() << "AnnotateWindow: applySelectResize bad highlight handle";
            return;
        }
        box = box.normalized().intersected(shotRect());
        if (box.width() < 8) {
            box.setWidth(8);
        }
        if (box.height() < 8) {
            box.setHeight(8);
        }
        rect->setRect(rect->mapRectFromScene(box));
        layoutHighlightChrome(rect);
        if (highlightStyle(rect) == HighlightStyle::Steps) {
            placeHighlightStepBadge(rect, shotRect(), clamped);
        }
        qInfo() << "AnnotateWindow: select resize highlight" << box;
        return;
    }
    if (kind == AnnotateKind::Blur) {
        auto *pix = qgraphicsitem_cast<QGraphicsPixmapItem *>(m_selectItem);
        if (!pix) {
            qWarning() << "AnnotateWindow: applySelectResize blur not pixmap";
            return;
        }
        QRectF box = itemSceneBox(pix);
        switch (m_selectHandle) {
        case SelectHandle::TopLeft:
            box.setTopLeft(clamped);
            break;
        case SelectHandle::TopRight:
            box.setTopRight(clamped);
            break;
        case SelectHandle::BottomLeft:
            box.setBottomLeft(clamped);
            break;
        case SelectHandle::BottomRight:
            box.setBottomRight(clamped);
            break;
        default:
            qWarning() << "AnnotateWindow: applySelectResize bad blur handle";
            return;
        }
        applyBlurSceneRect(pix, box);
        qInfo() << "AnnotateWindow: select resize blur" << box;
        return;
    }
    if (kind == AnnotateKind::Line || kind == AnnotateKind::Arrow) {
        QPointF p1 = m_selectItem->mapToScene(annotateP1(m_selectItem));
        QPointF p2 = m_selectItem->mapToScene(annotateP2(m_selectItem));
        if (m_selectHandle == SelectHandle::P1) {
            p1 = clamped;
        } else if (m_selectHandle == SelectHandle::P2) {
            p2 = clamped;
        } else {
            qWarning() << "AnnotateWindow: applySelectResize bad endpoint handle";
            return;
        }
        const QPointF local1 = m_selectItem->mapFromScene(p1);
        const QPointF local2 = m_selectItem->mapFromScene(p2);
        if (auto *line = qgraphicsitem_cast<QGraphicsLineItem *>(m_selectItem)) {
            line->setLine(QLineF(local1, local2));
            setAnnotateEndpoints(line, local1, local2);
        } else if (auto *path = qgraphicsitem_cast<QGraphicsPathItem *>(m_selectItem)) {
            setArrowEndpoints(path, local1, local2);
        }
        qInfo() << "AnnotateWindow: select resize endpoints" << local1 << local2;
        return;
    }
    qWarning() << "AnnotateWindow: applySelectResize unsupported kind=" << static_cast<int>(kind);
}

void AnnotateWindow::applySelectMove(const QPointF &scenePos)
{
    if (!m_selectItem) {
        qWarning() << "AnnotateWindow: applySelectMove no item";
        return;
    }
    const QPointF delta = scenePos - m_selectPressScene;
    m_selectItem->setPos(m_selectOldPos + delta);
    clampItemToShot(m_selectItem);
    qInfo() << "AnnotateWindow: select move pos=" << m_selectItem->pos();
}

void AnnotateWindow::commitSelectGesture()
{
    if (!m_selectItem) {
        return;
    }
    QGraphicsItem *item = m_selectItem;
    const SelectHandle handle = m_selectHandle;
    const bool didMove = m_selectDidMove;
    const bool wasSelected = m_selectWasSelected;
    const AnnotateKind kind = annotateKind(item);
    qInfo() << "AnnotateWindow: commitSelectGesture kind=" << static_cast<int>(kind)
            << "handle=" << static_cast<int>(handle) << "didMove=" << didMove
            << "wasSelected=" << wasSelected;
    if (didMove) {
        if (handle == SelectHandle::Move) {
            const QPointF now = item->pos();
            if (now != m_selectOldPos) {
                ++m_photoMoveGestureId;
                m_undo->push(new ChangePhotoPosCommand(item, m_selectOldPos, now, m_photoMoveGestureId));
                qInfo() << "AnnotateWindow: select move committed" << m_selectOldPos << "->" << now;
            }
        } else if (kind == AnnotateKind::Highlight) {
            if (auto *rect = qgraphicsitem_cast<QGraphicsRectItem *>(item)) {
                const QRectF now = rect->rect();
                if (now != m_selectOldRect) {
                    m_undo->push(new ChangeRectCommand(rect, m_selectOldRect, now));
                    qInfo() << "AnnotateWindow: select rect committed" << m_selectOldRect << "->" << now;
                }
            }
        } else if (kind == AnnotateKind::Line || kind == AnnotateKind::Arrow) {
            const QPointF p1 = annotateP1(item);
            const QPointF p2 = annotateP2(item);
            if (p1 != m_selectOldP1 || p2 != m_selectOldP2) {
                m_undo->push(new ChangeEndpointsCommand(item, m_selectOldP1, m_selectOldP2, p1, p2));
                qInfo() << "AnnotateWindow: select endpoints committed";
            }
        } else if (kind == AnnotateKind::Blur) {
            auto *pix = qgraphicsitem_cast<QGraphicsPixmapItem *>(item);
            const QImage nowSource = item->data(kAnnotateRoleBlurSource).value<QImage>();
            if (pix && (pix->pos() != m_selectOldBlurPos || nowSource.size() != m_selectOldBlurSource.size())) {
                m_undo->push(new ChangeBlurBoxCommand(pix, m_selectOldBlurPos, m_selectOldBlurSource, pix->pos(),
                                                      nowSource));
                qInfo() << "AnnotateWindow: select blur box committed";
            }
        }
    } else if (wasSelected) {
        if (auto *text = qgraphicsitem_cast<AnnotateTextItem *>(item)) {
            qInfo() << "AnnotateWindow: select second click edits text";
            beginEditText(text, false);
        } else if (auto *rect = qgraphicsitem_cast<QGraphicsRectItem *>(item)) {
            for (QGraphicsItem *child : rect->childItems()) {
                if (auto *caption = qgraphicsitem_cast<AnnotateTextItem *>(child)) {
                    qInfo() << "AnnotateWindow: select second click edits fill+text caption";
                    m_fillTextRect = rect;
                    beginEditText(caption, false);
                    break;
                }
            }
        }
    }
    m_selectItem = nullptr;
    m_selectHandle = SelectHandle::None;
    m_selectDidMove = false;
    m_selectWasSelected = false;
    if (m_view) {
        m_view->viewport()->update();
    }
}

void AnnotateWindow::deleteSelectedAnnotation()
{
    if (m_editingText || m_selectItem) {
        qInfo() << "AnnotateWindow: delete skipped edit=" << (m_editingText != nullptr)
                << "gesture=" << (m_selectItem != nullptr);
        return;
    }
    if (m_photoPipSelected && m_photoCycle) {
        qInfo() << "AnnotateWindow: delete turns off selected photo pip";
        abortPhotoCycle();
        return;
    }
    QGraphicsItem *item = selectedAnnotation();
    if (!item) {
        qInfo() << "AnnotateWindow: delete skipped, no selection";
        return;
    }
    qInfo() << "AnnotateWindow: delete selected kind=" << static_cast<int>(annotateKind(item));
    selectAnnotation(nullptr);
    m_undo->push(new RemoveItemCommand(m_scene, item));
}

void AnnotateWindow::paintSelectHandles(QPainter *painter) const
{
    if (!painter || !m_view) {
        return;
    }
    QGraphicsItem *item = m_selectItem ? m_selectItem : selectedAnnotation();
    if (!item || m_editingText) {
        return;
    }
    const AnnotateKind kind = annotateKind(item);
    const qreal scale = m_view->transform().m11();
    const qreal r = (scale > 0) ? (4.0 / scale) : 4.0;
    painter->setPen(QPen(Qt::white, 0));
    painter->setBrush(QColor(40, 40, 40, 220));
    auto paintAt = [&](const QPointF &pt) {
        painter->drawRect(QRectF(pt.x() - r, pt.y() - r, r * 2, r * 2));
    };
    if (kind == AnnotateKind::Arrow || kind == AnnotateKind::Line) {
        paintAt(item->mapToScene(annotateP1(item)));
        paintAt(item->mapToScene(annotateP2(item)));
        return;
    }
    if (kind == AnnotateKind::Highlight || kind == AnnotateKind::Blur) {
        const QRectF box = itemSceneBox(item);
        paintAt(box.topLeft());
        paintAt(box.topRight());
        paintAt(box.bottomLeft());
        paintAt(box.bottomRight());
        return;
    }
}

// ─── Ariadne's Thread [AT-0129] ─────────────────────
// What: Drag / select the live pip; Delete and toolbar unpress turn the camera off
// Why:  Pip was not a scene item, so it could not be found, moved, or deleted
// Date: 2026-08-26
// Related: [AT-0128] AnnotateWindow.cpp:layoutPhotoOverlay, [AT-0127] AnnotateWindow.cpp:togglePhotoPreview
// ─────────────────────────────────────────────────────
bool AnnotateWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_photoOverlay || !m_photoOverlay || !m_photoCycle) {
        return QMainWindow::eventFilter(watched, event);
    }
    if (photoCaptureBusy()) {
        qInfo() << "AnnotateWindow: photo pip event ignored during flash type=" << event->type();
        return QMainWindow::eventFilter(watched, event);
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton) {
            return QMainWindow::eventFilter(watched, event);
        }
        setPhotoPipSelected(true);
        m_photoPipDragging = true;
        m_photoPipDragOffset = mouse->globalPosition().toPoint() - m_photoOverlay->frameGeometry().topLeft();
        m_photoOverlay->setCursor(Qt::ClosedHandCursor);
        m_photoOverlay->grabMouse();
        qInfo() << "AnnotateWindow: photo pip press offset=" << m_photoPipDragOffset
                << " geo=" << m_photoOverlay->geometry();
        return true;
    }
    if (event->type() == QEvent::MouseMove && m_photoPipDragging) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        movePhotoOverlayToGlobal(mouse->globalPosition().toPoint() - m_photoPipDragOffset);
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && m_photoPipDragging) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton) {
            return QMainWindow::eventFilter(watched, event);
        }
        m_photoPipDragging = false;
        m_photoOverlay->releaseMouse();
        m_photoOverlay->setCursor(Qt::OpenHandCursor);
        persistPhotoPipScenePos();
        qInfo() << "AnnotateWindow: photo pip drop scene=" << m_photoPipSceneTopLeft
                << " geo=" << m_photoOverlay->geometry();
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

bool AnnotateWindow::viewPress(QMouseEvent *event, const QPointF &scenePos)
{
    if (event->button() != Qt::LeftButton) {
        qInfo() << "AnnotateWindow: viewPress ignore non-left";
        return true;
    }
    // ─── Ariadne's Thread [AT-0130] ─────────────────────
    // What: Keep annotate tools active while the live camera pip is on
    // Why:  viewPress returned early for the whole photo cycle and blocked drawing
    // Date: 2026-08-26
    // Related: [AT-0127] AnnotateWindow.cpp:togglePhotoPreview, docs/PRD-03-photo-cutout.md
    // ─────────────────────────────────────────────────────
    if (photoCaptureBusy()) {
        qInfo() << "AnnotateWindow: viewPress ignored during photo flash";
        return false;
    }
    if (m_photoPipSelected) {
        qInfo() << "AnnotateWindow: viewPress deselects photo pip";
        setPhotoPipSelected(false);
    }
    if (!isOnShot(scenePos)) {
        qInfo() << "AnnotateWindow: ignore press outside shot" << scenePos << "shot=" << shotRect();
        if (m_editingText) {
            commitTextEdit();
        }
        m_drawing = false;
        return false;
    }
    if (auto *text = textItemAt(scenePos)) {
        const QPointF itemPos = text->mapFromScene(scenePos);
        if (!text->isEditing() && text->isResizeHandle(itemPos)) {
            commitTextEdit();
            m_resizingText = text;
            m_resizeOldWidth = text->textWidth();
            if (m_resizeOldWidth < 0) {
                m_resizeOldWidth = text->boundingRect().width();
            }
            m_resizeStart = scenePos;
            qInfo() << "AnnotateWindow: text width resize start" << m_resizeOldWidth;
            return false;
        }
    }
    if (m_tool == Tool::Select) {
        commitTextEdit();
        QGraphicsItem *hit = annotationItemAt(scenePos);
        m_selectWasSelected = hit && hit->isSelected();
        selectAnnotation(hit);
        if (auto *photo = qgraphicsitem_cast<AnnotatePhotoItem *>(hit)) {
            const QPointF itemPos = photo->mapFromScene(scenePos);
            if (photo->isScaleHandle(itemPos)) {
                ++m_photoScaleGestureId;
                m_scalingPhoto = photo;
                m_photoOldScale = photo->photoScale();
                m_resizeStart = scenePos;
                qInfo() << "AnnotateWindow: select photo scale start" << m_photoOldScale;
                return false;
            }
            ++m_photoMoveGestureId;
            m_movingPhoto = photo;
            m_photoOldPos = photo->pos();
            m_resizeStart = scenePos;
            qInfo() << "AnnotateWindow: select photo move start" << m_photoOldPos;
            return false;
        }
        if (hit) {
            m_selectItem = hit;
            m_selectPressScene = scenePos;
            m_selectOldPos = hit->pos();
            m_selectDidMove = false;
            m_selectHandle = hitSelectHandle(hit, scenePos);
            if (m_selectHandle == SelectHandle::None) {
                m_selectHandle = SelectHandle::Move;
            }
            if (auto *rect = qgraphicsitem_cast<QGraphicsRectItem *>(hit)) {
                m_selectOldRect = rect->rect();
            }
            m_selectOldP1 = annotateP1(hit);
            m_selectOldP2 = annotateP2(hit);
            if (auto *pix = qgraphicsitem_cast<QGraphicsPixmapItem *>(hit)) {
                m_selectOldBlurPos = pix->pos();
                m_selectOldBlurSource = pix->data(kAnnotateRoleBlurSource).value<QImage>();
            }
            qInfo() << "AnnotateWindow: select gesture start kind=" << static_cast<int>(annotateKind(hit))
                    << "handle=" << static_cast<int>(m_selectHandle) << "wasSelected=" << m_selectWasSelected;
        }
        m_drawing = false;
        qInfo() << "AnnotateWindow: select press hit=" << (hit != nullptr);
        return false;
    }
    if (m_editingText) {
        AnnotateTextItem *hit = textItemAt(scenePos);
        if (hit == m_editingText) {
            qInfo() << "AnnotateWindow: click inside editing text";
            return true;
        }
        commitTextEdit();
    }
    if (auto *text = textItemAt(scenePos)) {
        qInfo() << "AnnotateWindow: re-enter existing text";
        m_fillTextRect = qgraphicsitem_cast<QGraphicsRectItem *>(text->parentItem());
        beginEditText(text, false);
        return true;
    }
    if (auto *photo = photoItemAt(scenePos)) {
        const QPointF itemPos = photo->mapFromScene(scenePos);
        commitTextEdit();
        if (photo->isScaleHandle(itemPos)) {
            ++m_photoScaleGestureId;
            m_scalingPhoto = photo;
            m_photoOldScale = photo->photoScale();
            m_resizeStart = scenePos;
            qInfo() << "AnnotateWindow: photo scale start" << m_photoOldScale << "gesture=" << m_photoScaleGestureId;
            return false;
        }
        ++m_photoMoveGestureId;
        m_movingPhoto = photo;
        m_photoOldPos = photo->pos();
        m_resizeStart = scenePos;
        qInfo() << "AnnotateWindow: photo move start" << m_photoOldPos << "gesture=" << m_photoMoveGestureId;
        return false;
    }
    if (m_tool == Tool::Text) {
        beginNewText(scenePos);
        return true;
    }
    m_drawing = true;
    m_dragStart = scenePos;
    m_draft = nullptr;
    qInfo() << "AnnotateWindow: press tool=" << static_cast<int>(m_tool) << scenePos;
    return false;
}

bool AnnotateWindow::viewMove(QMouseEvent *event, const QPointF &scenePos)
{
    Q_UNUSED(event);
    if (m_selectItem) {
        const qreal dist = QLineF(m_selectPressScene, scenePos).length();
        if (!m_selectDidMove && dist < 3.0) {
            qInfo() << "AnnotateWindow: select move ignore tiny dist=" << dist;
            return false;
        }
        m_selectDidMove = true;
        if (m_selectHandle == SelectHandle::Move) {
            applySelectMove(scenePos);
        } else {
            applySelectResize(scenePos);
        }
        if (m_view) {
            m_view->viewport()->update();
        }
        return false;
    }
    if (m_resizingText) {
        const qreal dx = scenePos.x() - m_resizeStart.x();
        const qreal width = qBound(m_resizingText->minTextWidth(), m_resizeOldWidth + dx,
                                  maxTextWidthOnShot(m_resizingText));
        m_resizingText->setTextWidth(width);
        qInfo() << "AnnotateWindow: text width live" << width;
        return false;
    }
    if (m_scalingPhoto) {
        const QPointF origin = m_scalingPhoto->pos();
        const qreal startDist = QLineF(origin, m_resizeStart).length();
        const qreal nowDist = QLineF(origin, scenePos).length();
        const qreal raw = m_photoOldScale * (nowDist / qMax(1.0, startDist));
        const qreal scale = qBound(m_scalingPhoto->minScale(), raw, m_scalingPhoto->maxScaleOnShot(shotRect()));
        m_scalingPhoto->setPhotoScale(scale);
        clampPhotoToShot(m_scalingPhoto);
        qInfo() << "AnnotateWindow: photo scale live" << scale;
        return false;
    }
    if (m_movingPhoto) {
        const QPointF delta = scenePos - m_resizeStart;
        m_movingPhoto->setPos(m_photoOldPos + delta);
        clampPhotoToShot(m_movingPhoto);
        qInfo() << "AnnotateWindow: photo move live" << m_movingPhoto->pos();
        return false;
    }
    if (m_editingText) {
        return true;
    }
    onSceneMoved(clampToShot(scenePos));
    return false;
}

bool AnnotateWindow::viewRelease(QMouseEvent *event, const QPointF &scenePos)
{
    Q_UNUSED(event);
    if (m_selectItem) {
        commitSelectGesture();
        return false;
    }
    if (m_resizingText) {
        const qreal newWidth = m_resizingText->textWidth();
        qInfo() << "AnnotateWindow: text width commit" << m_resizeOldWidth << "->" << newWidth;
        if (!qFuzzyCompare(newWidth + 1.0, m_resizeOldWidth + 1.0)) {
            m_undo->push(new ChangeTextWidthCommand(m_resizingText, m_resizeOldWidth, newWidth));
        }
        m_resizingText = nullptr;
        return false;
    }
    if (m_scalingPhoto) {
        const qreal newScale = m_scalingPhoto->photoScale();
        qInfo() << "AnnotateWindow: photo scale commit" << m_photoOldScale << "->" << newScale;
        if (!qFuzzyCompare(newScale + 1.0, m_photoOldScale + 1.0)) {
            m_undo->push(new ChangePhotoScaleCommand(m_scalingPhoto, m_photoOldScale, newScale, m_photoScaleGestureId));
        }
        m_scalingPhoto = nullptr;
        return false;
    }
    if (m_movingPhoto) {
        const QPointF newPos = m_movingPhoto->pos();
        qInfo() << "AnnotateWindow: photo move commit" << m_photoOldPos << "->" << newPos;
        if (newPos != m_photoOldPos) {
            m_undo->push(new ChangePhotoPosCommand(m_movingPhoto, m_photoOldPos, newPos, m_photoMoveGestureId));
        }
        m_movingPhoto = nullptr;
        return false;
    }
    if (m_editingText) {
        return true;
    }
    onSceneReleased(clampToShot(scenePos));
    return false;
}

bool AnnotateWindow::viewKeyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_photoCycle) {
        qInfo() << "AnnotateWindow: Escape cancels photo cycle";
        abortPhotoCycle();
        return true;
    }
    if (event->key() == Qt::Key_Escape && m_editingText) {
        qInfo() << "AnnotateWindow: Escape commits text";
        commitTextEdit();
        return true;
    }
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && !m_editingText) {
        QWidget *focus = QApplication::focusWidget();
        if (focus && focus != m_view && focus != m_view->viewport()) {
            qInfo() << "AnnotateWindow: delete skipped, focus=" << focus->metaObject()->className();
            return false;
        }
        qInfo() << "AnnotateWindow: delete key=" << event->key();
        deleteSelectedAnnotation();
        return true;
    }
    return false;
}

void AnnotateWindow::onSceneMoved(const QPointF &pos)
{
    if (m_tool == Tool::Text || m_tool == Tool::Select) {
        return;
    }
    if (!m_drawing) {
        return;
    }
    const QRectF rect = QRectF(m_dragStart, pos).normalized();
    if (m_tool == Tool::Highlight || m_tool == Tool::Blur) {
        if (!m_draft) {
            auto *item = new QGraphicsRectItem();
            if (m_tool == Tool::Highlight) {
                setAnnotateKind(item, AnnotateKind::Highlight);
                setHighlightStyle(item, currentHighlightStyle());
                setHighlightStrokeWidth(item, m_strokeWidth);
                setHighlightFillAlpha(item, m_fillAlpha);
            } else {
                setAnnotateKind(item, AnnotateKind::Blur);
            }
            applyItemColor(item, m_color);
            m_scene->addItem(item);
            m_draft = item;
            qInfo() << "AnnotateWindow: draft created tool=" << static_cast<int>(m_tool);
        }
        if (auto *item = qgraphicsitem_cast<QGraphicsRectItem *>(m_draft)) {
            item->setRect(rect);
        }
    } else if (m_tool == Tool::Arrow || m_tool == Tool::Line) {
        if (m_draft) {
            m_scene->removeItem(m_draft);
            delete m_draft;
            m_draft = nullptr;
        }
        if (m_tool == Tool::Arrow) {
            m_draft = makeArrow(m_dragStart, pos, m_color);
        } else {
            m_draft = makeLine(m_dragStart, pos, m_color);
        }
        m_scene->addItem(m_draft);
        qInfo() << "AnnotateWindow: draft line tool=" << static_cast<int>(m_tool);
    }
}

void AnnotateWindow::onSceneReleased(const QPointF &pos)
{
    if (m_tool == Tool::Text || !m_draft) {
        m_drawing = false;
        qInfo() << "AnnotateWindow: release without draft";
        return;
    }
    if (m_tool == Tool::Blur) {
        auto *rectItem = qgraphicsitem_cast<QGraphicsRectItem *>(m_draft);
        const QRect shot = shotRect().toRect();
        const QRect rect = rectItem ? rectItem->rect().toRect().intersected(shot) : QRect();
        m_scene->removeItem(m_draft);
        delete m_draft;
        m_draft = nullptr;
        const QRect clipped = rect.intersected(shot);
        const QImage source = shotImage().copy(clipped.translated(-shot.topLeft()));
        const QImage patch = boxBlur(source, m_blurRadius);
        if (!patch.isNull()) {
            auto *pix = new QGraphicsPixmapItem(QPixmap::fromImage(patch));
            pix->setPos(clipped.topLeft());
            setAnnotateKind(pix, AnnotateKind::Blur);
            setBlurSource(pix, source);
            pix->setData(kAnnotateRoleBlurRadius, m_blurRadius);
            m_undo->push(new AddItemCommand(m_scene, pix));
            qInfo() << "AnnotateWindow: committed blur radius=" << m_blurRadius << clipped;
        }
        m_drawing = false;
        return;
    }
    QGraphicsItem *item = m_draft;
    m_scene->removeItem(item);
    m_draft = nullptr;
    const HighlightStyle style = (m_tool == Tool::Highlight) ? highlightStyle(item) : HighlightStyle::Fill;
    if (m_tool == Tool::Highlight && style == HighlightStyle::Steps) {
        if (auto *rect = qgraphicsitem_cast<QGraphicsRectItem *>(item)) {
            attachHighlightStepBadge(rect, m_color, m_nextStepSeq, shotRect(), pos);
            ++m_nextStepSeq;
            qInfo() << "AnnotateWindow: highlight Steps seq assigned next=" << m_nextStepSeq
                    << "cursor=" << pos;
        }
    }
    m_undo->push(new AddItemCommand(m_scene, item));
    m_drawing = false;
    qInfo() << "AnnotateWindow: committed item tool=" << static_cast<int>(m_tool)
            << "style=" << static_cast<int>(style);
    if (m_tool == Tool::Highlight && style == HighlightStyle::FillText) {
        beginFillTextCaption(qgraphicsitem_cast<QGraphicsRectItem *>(item));
    }
}

void AnnotateWindow::handlePress(const QPointF &pos)
{
    qInfo() << "AnnotateWindow: handlePress" << pos;
}

void AnnotateWindow::handleMove(const QPointF &pos)
{
    Q_UNUSED(pos);
}

void AnnotateWindow::handleRelease(const QPointF &pos)
{
    Q_UNUSED(pos);
}

bool AnnotateWindow::isPhotoCaptureBusy() const
{
    const bool busy = photoCaptureBusy() || m_photoCycle;
    qInfo() << "AnnotateWindow: isPhotoCaptureBusy=" << busy << " cycle=" << m_photoCycle;
    return busy;
}

// ─── Ariadne's Thread [AT-0093] ─────────────────────
// What: Update card on the shot + persist/restore editor session
// Why:  PRD-05 overlay and Sparkle relaunch must keep the current screenshot
// Date: 2026-08-25
// Related: [AT-0091] LocalStore.cpp, [AT-0092] SparkleUpdater.h, docs/PRD-05-auto-update.md
// ─────────────────────────────────────────────────────
void AnnotateWindow::ensureUpdateCard()
{
    if (m_updateCard) {
        return;
    }
    m_updateCard = new QFrame(this);
    m_updateCard->setFrameShape(QFrame::StyledPanel);
    m_updateCard->setAutoFillBackground(true);
    m_updateCard->setFixedWidth(320);
    auto *layout = new QVBoxLayout(m_updateCard);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);
    m_updateTitle = new QLabel(QStringLiteral("A new update is available."), m_updateCard);
    m_updateTitle->setWordWrap(true);
    m_updateTitle->setAlignment(Qt::AlignCenter);
    m_updateButton = new QPushButton(QStringLiteral("Update"), m_updateCard);
    m_updateButton->setDefault(true);
    m_updateButton->setAutoDefault(false);
    m_updateBar = new QProgressBar(m_updateCard);
    m_updateBar->setRange(0, 1000);
    m_updateBar->setValue(0);
    m_updateBar->setTextVisible(true);
    m_updateStatus = new QLabel(m_updateCard);
    m_updateStatus->setAlignment(Qt::AlignCenter);
    m_updateStatus->setWordWrap(true);
    layout->addWidget(m_updateTitle);
    layout->addWidget(m_updateButton);
    layout->addWidget(m_updateBar);
    layout->addWidget(m_updateStatus);
    m_updateBar->hide();
    m_updateStatus->hide();
    m_updateCard->hide();
    connect(m_updateButton, &QPushButton::clicked, this, [this]() {
        qInfo() << "AnnotateWindow: Update clicked";
        emit updateRequested();
    });
    qInfo() << "AnnotateWindow: update card created";
}

void AnnotateWindow::layoutUpdateCard()
{
    if (!m_updateCard || !m_updateCard->isVisible() || !m_view) {
        return;
    }
    m_updateCard->adjustSize();
    const QRect view = m_view->geometry();
    const int x = view.x() + (view.width() - m_updateCard->width()) / 2;
    const int y = view.y() + (view.height() - m_updateCard->height()) / 2;
    m_updateCard->move(x, y);
    m_updateCard->raise();
    qInfo() << "AnnotateWindow: update card pos=" << m_updateCard->pos() << " view=" << view;
}

void AnnotateWindow::showUpdateOffer()
{
    ensureUpdateCard();
    m_updateTitle->setText(QStringLiteral("A new update is available."));
    m_updateButton->show();
    m_updateBar->hide();
    m_updateStatus->hide();
    m_updateCard->show();
    layoutUpdateCard();
    qInfo() << "AnnotateWindow: update offer shown";
}

void AnnotateWindow::showUpdateProgress(qint64 received, qint64 expected, const QString &status)
{
    ensureUpdateCard();
    m_updateButton->hide();
    m_updateBar->show();
    m_updateStatus->show();
    m_updateStatus->setText(status);
    if (expected > 0) {
        m_updateBar->setRange(0, 1000);
        const int value = static_cast<int>(qBound(0.0, (double)received / (double)expected, 1.0) * 1000.0);
        m_updateBar->setValue(value);
    } else {
        m_updateBar->setRange(0, 0);
    }
    m_updateCard->show();
    layoutUpdateCard();
    qInfo() << "AnnotateWindow: update progress received=" << received << " expected=" << expected
            << " status=" << status;
}

void AnnotateWindow::showUpdateExtracting(double progress)
{
    ensureUpdateCard();
    m_updateButton->hide();
    m_updateBar->show();
    m_updateStatus->show();
    m_updateStatus->setText(QStringLiteral("Extracting…"));
    m_updateBar->setRange(0, 1000);
    m_updateBar->setValue(static_cast<int>(qBound(0.0, progress, 1.0) * 1000.0));
    m_updateCard->show();
    layoutUpdateCard();
    qInfo() << "AnnotateWindow: update extracting=" << progress;
}

void AnnotateWindow::showUpdateInstalling()
{
    ensureUpdateCard();
    m_updateButton->hide();
    m_updateBar->show();
    m_updateStatus->show();
    m_updateStatus->setText(QStringLiteral("Installing…"));
    m_updateBar->setRange(0, 0);
    m_updateCard->show();
    layoutUpdateCard();
    qInfo() << "AnnotateWindow: update installing";
}

void AnnotateWindow::resetUpdateOffer()
{
    qInfo() << "AnnotateWindow: reset update offer";
    showUpdateOffer();
}

void AnnotateWindow::hideUpdateCard()
{
    if (!m_updateCard) {
        return;
    }
    m_updateCard->hide();
    qInfo() << "AnnotateWindow: update card hidden";
}

void AnnotateWindow::showUpdateError(const QString &code)
{
    qWarning() << "AnnotateWindow: update error" << code;
    showError(code);
}

QJsonObject AnnotateWindow::serializeSession(QHash<QString, QImage> *assets) const
{
    QJsonObject root;
    root.insert(QStringLiteral("bgPreset"), m_bgPreset);
    root.insert(QStringLiteral("cornerRadius"), m_cornerRadius);
    root.insert(QStringLiteral("shadowAmount"), m_shadowAmount);
    root.insert(QStringLiteral("nextStepSeq"), m_nextStepSeq);
    QJsonArray items;
    int assetIndex = 0;
    const auto sceneItems = m_scene ? m_scene->items(Qt::AscendingOrder) : QList<QGraphicsItem *>();
    for (QGraphicsItem *item : sceneItems) {
        if (!item || item == m_photo || item == m_background || item == m_draft || item->parentItem()) {
            continue;
        }
        const AnnotateKind kind = annotateKind(item);
        if (kind == AnnotateKind::None) {
            continue;
        }
        QJsonObject obj;
        obj.insert(QStringLiteral("kind"), static_cast<int>(kind));
        obj.insert(QStringLiteral("posX"), item->pos().x());
        obj.insert(QStringLiteral("posY"), item->pos().y());
        obj.insert(QStringLiteral("color"), itemColor(item).name(QColor::HexArgb));
        if (kind == AnnotateKind::Highlight) {
            auto *rect = qgraphicsitem_cast<QGraphicsRectItem *>(item);
            if (!rect) {
                continue;
            }
            obj.insert(QStringLiteral("style"), static_cast<int>(highlightStyle(item)));
            obj.insert(QStringLiteral("stroke"), highlightStrokeWidth(item));
            obj.insert(QStringLiteral("fillAlpha"), highlightFillAlpha(item));
            obj.insert(QStringLiteral("stepSeq"), item->data(kAnnotateRoleStepSeq).toInt());
            obj.insert(QStringLiteral("rectX"), rect->rect().x());
            obj.insert(QStringLiteral("rectY"), rect->rect().y());
            obj.insert(QStringLiteral("rectW"), rect->rect().width());
            obj.insert(QStringLiteral("rectH"), rect->rect().height());
            for (QGraphicsItem *child : rect->childItems()) {
                if (auto *caption = qgraphicsitem_cast<AnnotateTextItem *>(child)) {
                    obj.insert(QStringLiteral("caption"), caption->toPlainText());
                    obj.insert(QStringLiteral("captionWidth"), caption->textWidth());
                }
            }
        } else if (kind == AnnotateKind::Arrow || kind == AnnotateKind::Line) {
            const QPointF p1 = annotateP1(item);
            const QPointF p2 = annotateP2(item);
            obj.insert(QStringLiteral("p1x"), p1.x());
            obj.insert(QStringLiteral("p1y"), p1.y());
            obj.insert(QStringLiteral("p2x"), p2.x());
            obj.insert(QStringLiteral("p2y"), p2.y());
        } else if (kind == AnnotateKind::Text) {
            auto *text = qgraphicsitem_cast<AnnotateTextItem *>(item);
            if (!text) {
                continue;
            }
            obj.insert(QStringLiteral("text"), text->toPlainText());
            obj.insert(QStringLiteral("textWidth"), text->textWidth());
        } else if (kind == AnnotateKind::Blur) {
            auto *pix = qgraphicsitem_cast<QGraphicsPixmapItem *>(item);
            if (!pix || !assets) {
                continue;
            }
            const QImage source = item->data(kAnnotateRoleBlurSource).value<QImage>();
            const QString name = QStringLiteral("blur-%1.png").arg(assetIndex++);
            assets->insert(name, source.isNull() ? pix->pixmap().toImage() : source);
            obj.insert(QStringLiteral("asset"), name);
            obj.insert(QStringLiteral("radius"), blurRadius(item));
        } else if (kind == AnnotateKind::Photo) {
            auto *photo = qgraphicsitem_cast<AnnotatePhotoItem *>(item);
            if (!photo || !assets) {
                continue;
            }
            const QString name = QStringLiteral("photo-%1.png").arg(assetIndex++);
            assets->insert(name, photo->pixmap().toImage());
            obj.insert(QStringLiteral("asset"), name);
            obj.insert(QStringLiteral("scale"), photo->photoScale());
        } else {
            continue;
        }
        items.append(obj);
    }
    root.insert(QStringLiteral("items"), items);
    qInfo() << "AnnotateWindow: serialize items=" << items.size() << " assets=" << (assets ? assets->size() : 0);
    return root;
}

bool AnnotateWindow::persistSession(QString *errorCode)
{
    commitTextEdit();
    if (m_source.isNull()) {
        qWarning() << "AnnotateWindow: persistSession empty source";
        if (errorCode) {
            *errorCode = QStringLiteral("UPDATE_PERSIST_FAILED");
        }
        return false;
    }
    QHash<QString, QImage> assets;
    const QJsonObject json = serializeSession(&assets);
    const bool ok = LocalStore::writeEditorSession(json, m_source, assets, errorCode);
    qInfo() << "AnnotateWindow: persistSession ok=" << ok << " assets=" << assets.size();
    return ok;
}

bool AnnotateWindow::restoreItems(const QJsonArray &items, const QHash<QString, QImage> &assets, QString *errorCode)
{
    int restored = 0;
    for (const QJsonValue &value : items) {
        const QJsonObject obj = value.toObject();
        const auto kind = static_cast<AnnotateKind>(obj.value(QStringLiteral("kind")).toInt());
        const QColor color(obj.value(QStringLiteral("color")).toString());
        const QPointF pos(obj.value(QStringLiteral("posX")).toDouble(), obj.value(QStringLiteral("posY")).toDouble());
        if (kind == AnnotateKind::Highlight) {
            auto *item = new QGraphicsRectItem();
            setAnnotateKind(item, AnnotateKind::Highlight);
            setHighlightStyle(item, static_cast<HighlightStyle>(obj.value(QStringLiteral("style")).toInt()));
            setHighlightStrokeWidth(item, obj.value(QStringLiteral("stroke")).toInt(2));
            setHighlightFillAlpha(item, obj.value(QStringLiteral("fillAlpha")).toInt(80));
            item->setRect(QRectF(obj.value(QStringLiteral("rectX")).toDouble(),
                                 obj.value(QStringLiteral("rectY")).toDouble(),
                                 obj.value(QStringLiteral("rectW")).toDouble(),
                                 obj.value(QStringLiteral("rectH")).toDouble()));
            item->setPos(pos);
            applyItemColor(item, color.isValid() ? color : m_color);
            m_scene->addItem(item);
            if (highlightStyle(item) == HighlightStyle::Steps) {
                attachHighlightStepBadge(item, itemColor(item), obj.value(QStringLiteral("stepSeq")).toInt(1),
                                         shotRect());
            }
            const QString caption = obj.value(QStringLiteral("caption")).toString();
            if (!caption.isEmpty()) {
                auto *text = new AnnotateTextItem(caption);
                text->setParentItem(item);
                const QRectF box = item->rect();
                text->setPos(box.topLeft() + QPointF(4, 4));
                text->setTextWidth(obj.value(QStringLiteral("captionWidth")).toDouble(box.width() - 8));
                text->setDefaultTextColor(color.isValid() ? color : m_color);
            }
            ++restored;
        } else if (kind == AnnotateKind::Arrow || kind == AnnotateKind::Line) {
            const QPointF p1(obj.value(QStringLiteral("p1x")).toDouble(), obj.value(QStringLiteral("p1y")).toDouble());
            const QPointF p2(obj.value(QStringLiteral("p2x")).toDouble(), obj.value(QStringLiteral("p2y")).toDouble());
            QGraphicsItem *item = (kind == AnnotateKind::Arrow) ? static_cast<QGraphicsItem *>(makeArrow(p1, p2, color))
                                                                : static_cast<QGraphicsItem *>(makeLine(p1, p2, color));
            item->setPos(pos);
            m_scene->addItem(item);
            ++restored;
        } else if (kind == AnnotateKind::Text) {
            auto *text = new AnnotateTextItem(obj.value(QStringLiteral("text")).toString());
            text->setPos(pos);
            text->setDefaultTextColor(color.isValid() ? color : m_color);
            text->setTextWidth(obj.value(QStringLiteral("textWidth")).toDouble(text->textWidth()));
            m_scene->addItem(text);
            ++restored;
        } else if (kind == AnnotateKind::Blur) {
            const QString name = obj.value(QStringLiteral("asset")).toString();
            const QImage source = assets.value(name);
            if (source.isNull()) {
                qWarning() << "AnnotateWindow: restore blur missing asset";
                if (errorCode) {
                    *errorCode = QStringLiteral("UPDATE_PERSIST_FAILED");
                }
                return false;
            }
            const int radius = obj.value(QStringLiteral("radius")).toInt(8);
            auto *pix = new QGraphicsPixmapItem(QPixmap::fromImage(boxBlur(source, radius)));
            pix->setPos(pos);
            setAnnotateKind(pix, AnnotateKind::Blur);
            setBlurSource(pix, source);
            pix->setData(kAnnotateRoleBlurRadius, radius);
            m_scene->addItem(pix);
            ++restored;
        } else if (kind == AnnotateKind::Photo) {
            const QString name = obj.value(QStringLiteral("asset")).toString();
            const QImage image = assets.value(name);
            if (image.isNull()) {
                qWarning() << "AnnotateWindow: restore photo missing asset";
                if (errorCode) {
                    *errorCode = QStringLiteral("UPDATE_PERSIST_FAILED");
                }
                return false;
            }
            auto *photo = new AnnotatePhotoItem(QPixmap::fromImage(image));
            photo->setPhotoScale(obj.value(QStringLiteral("scale")).toDouble(1.0));
            photo->setPos(pos);
            m_scene->addItem(photo);
            ++restored;
        }
    }
    qInfo() << "AnnotateWindow: restored items=" << restored;
    return true;
}

bool AnnotateWindow::restoreSession(const QJsonObject &json, const QHash<QString, QImage> &assets, QString *errorCode)
{
    if (!m_scene) {
        qWarning() << "AnnotateWindow: restoreSession no scene";
        if (errorCode) {
            *errorCode = QStringLiteral("UPDATE_PERSIST_FAILED");
        }
        return false;
    }
    m_nextStepSeq = qMax(1, json.value(QStringLiteral("nextStepSeq")).toInt(1));
    m_cornerRadius = json.value(QStringLiteral("cornerRadius")).toInt(m_cornerRadius);
    m_shadowAmount = json.value(QStringLiteral("shadowAmount")).toInt(m_shadowAmount);
    if (m_radius) {
        QSignalBlocker block(m_radius);
        m_radius->setValue(m_cornerRadius);
    }
    if (m_shadow) {
        QSignalBlocker block(m_shadow);
        m_shadow->setValue(m_shadowAmount);
    }
    setBackgroundPreset(json.value(QStringLiteral("bgPreset")).toInt(0));
    if (!restoreItems(json.value(QStringLiteral("items")).toArray(), assets, errorCode)) {
        return false;
    }
    if (m_undo) {
        m_undo->clear();
    }
    applyCanvasChrome();
    fitShotToWindow();
    qInfo() << "AnnotateWindow: restoreSession items=" << json.value(QStringLiteral("items")).toArray().size()
            << " assets=" << assets.size();
    return true;
}
