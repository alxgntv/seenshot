#include "annotate/AnnotateWindow.h"

#include "annotate/AnnotationCommands.h"
#include "app/Analytics.h"
#include "app/Config.h"
#include "app/MacIcons.h"
#include "app/MacPermissions.h"
#include "camera/CameraCapture.h"
#include "camera/PersonCutout.h"
#include "auth/AuthSession.h"
#include "cloud/CloudClient.h"
#include "errors/ErrorCatalog.h"
#include "export/CloudPngEncoder.h"
#include "local/LocalStore.h"
#include "redact/SensitiveRedact.h"
#include "update/SparkleUpdater.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QSignalBlocker>
#include <QBuffer>
#include <QByteArray>
#include <QBrush>
#include <QClipboard>
#include <QIODevice>
#include <QMimeData>
#include <QCheckBox>
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
#include <QFile>
#include <QFileDialog>
#include <QIODevice>
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
#include <QUrl>
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
#include <QStyleHints>
#include <QWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QTextCursor>
#include <QThread>
#include <QToolBar>
#include <QMetaObject>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTransform>
#include <QUndoStack>
#include <QVariant>

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

// ─── Ariadne's Thread [AT-0184] ─────────────────────
// What: 12-hex id at capture, used in SeenShot-date-id.png and /screenshot/{id}
// Why:  Share URL must match the id already in the local filename
// Date: 2026-08-27
// Related: [AT-0076] AnnotateWindow.cpp:saveLocal, [AT-0185] backend→quota.ts:publicShareUrl
// ─────────────────────────────────────────────────────
QString makeShotFileId()
{
    QString hash;
    hash.reserve(12);
    for (int i = 0; i < 12; ++i) {
        hash.append(QString::number(QRandomGenerator::global()->bounded(16), 16));
    }
    qInfo() << "AnnotateWindow: makeShotFileId hash=" << hash;
    return hash;
}

bool looksLikeShotFileId(const QString &id)
{
    if (id.size() != 12) {
        qInfo() << "AnnotateWindow: looksLikeShotFileId reject size=" << id.size();
        return false;
    }
    for (const QChar c : id) {
        if (!c.isDigit() && (c < QLatin1Char('a') || c > QLatin1Char('f'))) {
            qInfo() << "AnnotateWindow: looksLikeShotFileId reject char=" << c;
            return false;
        }
    }
    return true;
}

// ─── Ariadne's Thread [AT-0159] ─────────────────────
// What: Fifth Color swatch is Purple (88,86,214), not Magenta (255,45,85)
// Why:  Magenta from Background preset 5 reads as a second red next to (255,59,48)
// Date: 2026-08-26
// Related: [AT-0158] AnnotateWindow.cpp:annotatePresetColor, [AT-0073] AnnotateWindow.cpp:backgroundPresetColors
// ─────────────────────────────────────────────────────
QColor annotatePresetColor(int id)
{
    switch (id) {
    case 0:
        return QColor(255, 59, 48);
    case 1:
        return QColor(255, 204, 0);
    case 2:
        return QColor(52, 199, 89);
    case 3:
        return QColor(0, 122, 255);
    case 4:
        return QColor(88, 86, 214);
    case 5:
        return QColor(255, 255, 255);
    case 6:
        return QColor(0, 0, 0);
    default:
        qWarning() << "annotatePresetColor: unknown id=" << id;
        return QColor(255, 59, 48);
    }
}

QIcon colorCircleIcon(const QColor &color)
{
    constexpr int logical = 22;
    constexpr qreal dpr = 2.0;
    QPixmap pm(qRound(logical * dpr), qRound(logical * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF disk(1.5, 1.5, logical - 3.0, logical - 3.0);
    const QColor fill = color.isValid() ? color : QColor(255, 59, 48);
    painter.setPen(QPen(QColor(180, 180, 180), 1.5));
    painter.setBrush(fill);
    painter.drawEllipse(disk);
    qInfo() << "colorCircleIcon: color=" << fill;
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
        // ─── Ariadne's Thread [AT-0162] ─────────────────────
        // What: FullViewportUpdate so drawForeground Select handles do not ghost
        // Why:  MinimalViewportUpdate only dirties the shrinking text; old handle pixels stay
        // Date: 2026-08-26
        // Related: [AT-0156] AnnotateWindow.cpp:paintSelectHandles, Qt QGraphicsView::ViewportUpdateMode
        // ─────────────────────────────────────────────────────
        setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
        qInfo() << "EditorView: viewport update mode=" << viewportUpdateMode();
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
        m_host->paintShotBorder(painter);
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

void styleGrayToolbarButton(QPushButton *btn, bool darkChrome)
{
    if (!btn) {
        qWarning() << "styleGrayToolbarButton: null";
        return;
    }
    if (darkChrome) {
        btn->setAttribute(Qt::WA_StyledBackground, true);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton {"
            " background-color: #ffffff;"
            " color: #000000;"
            " border: none;"
            " border-radius: 6px;"
            " padding: 4px 10px;"
            "}"
            "QPushButton:pressed {"
            " background-color: #e5e5ea;"
            " color: #000000;"
            "}"
            "QPushButton:disabled {"
            " background-color: #ffffff;"
            " color: #8e8e93;"
            "}"));
    } else {
        btn->setAttribute(Qt::WA_StyledBackground, false);
        btn->setStyleSheet(QString());
    }
    qInfo() << "styleGrayToolbarButton title=" << btn->text() << "darkChrome=" << darkChrome
            << "styled=" << darkChrome;
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
    , m_fileId(makeShotFileId())
{
    qInfo() << "AnnotateWindow: constructed fileId=" << m_fileId;
    setWindowTitle(QStringLiteral("SeenShot — Annotate"));
    // ─── Ariadne's Thread [AT-0202] ─────────────────────
    // What: Annotate window does not participate in last-window-closed quit
    // Why:  Qt 6.11 ties WA_QuitOnClose to lastWindowClosed; the agent must stay in the tray
    // Date: 2026-08-27
    // Related: [AT-0204] Application.cpp:eventFilter, client/src/main.cpp
    // ─────────────────────────────────────────────────────
    setAttribute(Qt::WA_QuitOnClose, false);
    qInfo() << "AnnotateWindow: WA_QuitOnClose=" << testAttribute(Qt::WA_QuitOnClose);
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
    // ─── Ariadne's Thread [AT-0398] ─────────────────────
    // What: Ignore QGraphicsView scene sizeHint so the Blur column stays on-screen
    // Why:  Scene-pixel sizeHint pushed the 240px sidebar past the window edge
    // Date: 2026-09-03
    // Related: [AT-0396] AnnotateWindow.cpp:ensureBlurSidebar, [AT-0050] AnnotateWindow.cpp:fitShotToWindow
    // ─────────────────────────────────────────────────────
    m_view->setMinimumSize(0, 0);
    m_view->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    qInfo() << "AnnotateWindow: view sizePolicy ignored scene=" << m_source.size();
    m_undo = new QUndoStack(this);
    connect(m_undo, &QUndoStack::indexChanged, this, [this](int index) {
        qInfo() << "AnnotateWindow: undo indexChanged=" << index;
        updateStrokeFillVisibility();
    });

    const QColor toolTint(245, 245, 247);
    auto *toolbar = new QToolBar(this);
    m_toolsBar = toolbar;
    // ─── Ariadne's Thread [AT-0149] ─────────────────────
    // What: Floating dark pill QToolBar over the canvas, not addToolBar
    // Why:  Native unified toolbar on macOS squeezed and stretched the SF icons
    // Date: 2026-08-26
    // Related: [AT-0063] AnnotateWindow.cpp, [AT-0055] MacIcons.mm:macToolbarIcon
    // ─────────────────────────────────────────────────────
    toolbar->setObjectName(QStringLiteral("AnnotateTools"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setAllowedAreas(Qt::NoToolBarArea);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // ─── Ariadne's Thread [AT-0150] ─────────────────────
    // What: Compact 16px tools and one Background text+icon button
    // Why:  20px padding overflowed into the QToolBar extension chevron
    // Date: 2026-08-26
    // Related: [AT-0149] AnnotateWindow.cpp, [AT-0131] AnnotateWindow.cpp:m_bgButton
    // ─────────────────────────────────────────────────────
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setAttribute(Qt::WA_StyledBackground, true);
    toolbar->setStyleSheet(QStringLiteral(
        "#AnnotateTools { background: #2c2c2e; border: 1px solid #6e6e73; border-radius: 14px; padding: 3px 6px; "
        "spacing: 2px; }"
        "#AnnotateTools QToolButton { background: transparent; border: none; border-radius: 6px; padding: 3px; "
        "min-width: 22px; min-height: 22px; }"
        "#AnnotateTools QToolButton#BgPreset { padding: 2px 6px; min-width: 0; color: #f2f2f7; }"
        "#AnnotateTools QToolButton:hover { background: #3a3a3c; }"
        "#AnnotateTools QToolButton:checked { background: #0a84ff; }"
        "#AnnotateTools QToolBar::separator { background: #636366; width: 1px; margin: 4px 3px; }"
        "#AnnotateTools QLabel { color: #f2f2f7; background: transparent; font-size: 11px; }"
        "#AnnotateTools QCheckBox { color: #f2f2f7; background: transparent; spacing: 2px; font-size: 11px; }"));
    auto *barShadow = new QGraphicsDropShadowEffect(toolbar);
    barShadow->setBlurRadius(22);
    barShadow->setOffset(0, 4);
    barShadow->setColor(QColor(0, 0, 0, 150));
    toolbar->setGraphicsEffect(barShadow);
    qInfo() << "AnnotateWindow: floating tools bar iconSize=16 tint=white";
    // ─── Ariadne's Thread [AT-0066] ─────────────────────
    // What: Highlight styles as toolbar actions; Select uses QGraphicsItem selection
    // Why:  Style combo hid the four icons; click must select an object without drawing
    // Date: 2026-08-25
    // Related: [AT-0051] AnnotateWindow.cpp:onHighlightStyleChanged, [AT-0044] AnnotateWindow.cpp
    // ─────────────────────────────────────────────────────
    m_toolGroup = new QActionGroup(this);
    m_toolGroup->setExclusive(true);
    const auto addCheckable = [this, toolbar, toolTint](QAction *&slot, const QString &symbol, const QString &tip,
                                                        void (AnnotateWindow::*method)()) {
        slot = toolbar->addAction(macToolbarIcon(symbol, toolTint), tip, this, method);
        slot->setCheckable(true);
        slot->setToolTip(tip);
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
    const auto addStyle = [this, toolbar, toolTint](QAction *&slot, const QString &symbol, const QString &tip,
                                                    HighlightStyle style) {
        slot = toolbar->addAction(macToolbarIcon(symbol, toolTint), tip, this, [this, style]() {
            setHighlightStyleTool(style);
        });
        slot->setCheckable(true);
        slot->setToolTip(tip);
        m_toolGroup->addAction(slot);
        qInfo() << "AnnotateWindow: highlight style action" << tip << "style=" << static_cast<int>(style)
                << "iconNull=" << slot->icon().isNull();
    };
    addStyle(m_styleStepsAction, QStringLiteral("list.number"), QStringLiteral("Steps"), HighlightStyle::Steps);
    addCheckable(m_arrowAction, QStringLiteral("arrow.up.right"), QStringLiteral("Arrow"),
                 &AnnotateWindow::setToolArrow);
    addCheckable(m_lineAction, QStringLiteral("line.diagonal"), QStringLiteral("Line"),
                 &AnnotateWindow::setToolLine);
    // ─── Ariadne's Thread [AT-0345] ─────────────────────
    // What: Text, Size, and Outline share one gray toolbar pad when text is selected
    // Why:  Size and Outline sat outside the Aa selected background
    // Date: 2026-08-28
    // Related: [AT-0334] AnnotateWindow.cpp:updateTextSizeVisibility, [AT-0344] AnnotateWindow.cpp:m_textOutlineBox
    // ─────────────────────────────────────────────────────
    m_textToolAction = new QAction(macToolbarIcon(QStringLiteral("textformat"), toolTint),
                                   QStringLiteral("Text"), this);
    m_textToolAction->setCheckable(true);
    m_textToolAction->setToolTip(QStringLiteral("Text"));
    m_toolGroup->addAction(m_textToolAction);
    connect(m_textToolAction, &QAction::triggered, this, &AnnotateWindow::setToolText);
    m_textGroup = new QFrame(toolbar);
    m_textGroup->setObjectName(QStringLiteral("TextGroup"));
    m_textGroup->setAttribute(Qt::WA_StyledBackground, true);
    m_textGroup->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    auto *textGroupLay = new QHBoxLayout(m_textGroup);
    textGroupLay->setContentsMargins(0, 0, 0, 0);
    textGroupLay->setSpacing(4);
    m_textButton = new QToolButton(m_textGroup);
    m_textButton->setDefaultAction(m_textToolAction);
    m_textButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_textButton->setFocusPolicy(Qt::NoFocus);
    m_textButton->setIconSize(QSize(16, 16));
    m_textButton->setToolTip(QStringLiteral("Text"));
    m_textButton->setAccessibleName(QStringLiteral("Text"));
    m_textSizeLabel = new QLabel(QStringLiteral("Size"), m_textGroup);
    m_textSizeSlider = new QSlider(Qt::Horizontal, m_textGroup);
    m_textSizeSlider->setRange(10, 48);
    m_textSizeSlider->setValue(m_textSize);
    m_textSizeSlider->setFixedWidth(40);
    m_textSizeSlider->setToolTip(QStringLiteral("Text size"));
    m_textSizeSlider->setAccessibleName(QStringLiteral("Text size"));
    m_textSizeSlider->setFocusPolicy(Qt::NoFocus);
    connect(m_textSizeSlider, &QSlider::sliderPressed, this, &AnnotateWindow::onTextSizePressed);
    connect(m_textSizeSlider, &QSlider::valueChanged, this, &AnnotateWindow::onTextSizeChanged);
    connect(m_textSizeSlider, &QSlider::sliderReleased, this, &AnnotateWindow::onTextSizeReleased);
    m_textOutlineBox = new QCheckBox(QStringLiteral("Outline"), m_textGroup);
    m_textOutlineBox->setChecked(m_textOutline);
    m_textOutlineBox->setToolTip(QStringLiteral("Text outline"));
    m_textOutlineBox->setAccessibleName(QStringLiteral("Text outline"));
    m_textOutlineBox->setFocusPolicy(Qt::NoFocus);
    connect(m_textOutlineBox, &QCheckBox::toggled, this, &AnnotateWindow::onTextOutlineToggled);
    textGroupLay->addWidget(m_textButton);
    textGroupLay->addWidget(m_textSizeLabel);
    textGroupLay->addWidget(m_textSizeSlider);
    textGroupLay->addWidget(m_textOutlineBox);
    m_textSizeLabel->hide();
    m_textSizeSlider->hide();
    m_textOutlineBox->hide();
    toolbar->addWidget(m_textGroup);
    qInfo() << "AnnotateWindow: Text group Aa+Size+Outline iconNull=" << m_textButton->icon().isNull()
            << "outline=" << m_textOutline;
    // ─── Ariadne's Thread [AT-0338] ─────────────────────
    // What: Blur is one QToolButton, icon then text, same as Background
    // Why:  Separate QLabel sat on the left and did not share the click with the icon
    // Date: 2026-08-28
    // Related: [AT-0131] AnnotateWindow.cpp:m_bgButton, [AT-0337] AnnotateWindow.cpp:blurLabel
    // ─────────────────────────────────────────────────────
    m_blurAction = new QAction(macToolbarIcon(QStringLiteral("circle.lefthalf.filled"), toolTint),
                               QStringLiteral("Blur"), this);
    m_blurAction->setCheckable(true);
    m_blurAction->setToolTip(QStringLiteral("Blur"));
    m_toolGroup->addAction(m_blurAction);
    connect(m_blurAction, &QAction::triggered, this, &AnnotateWindow::setToolBlur);
    m_blurButton = new QToolButton(toolbar);
    m_blurButton->setObjectName(QStringLiteral("BgPreset"));
    m_blurButton->setDefaultAction(m_blurAction);
    m_blurButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_blurButton->setFocusPolicy(Qt::NoFocus);
    m_blurButton->setIconSize(QSize(16, 16));
    m_blurButton->setToolTip(QStringLiteral("Blur"));
    m_blurButton->setAccessibleName(QStringLiteral("Blur"));
    toolbar->addWidget(m_blurButton);
    qInfo() << "AnnotateWindow: Blur text+icon button style=" << m_blurButton->toolButtonStyle()
            << "text=" << m_blurButton->text() << "iconNull=" << m_blurButton->icon().isNull()
            << "checkable=" << m_blurButton->isCheckable();
    // ─── Ariadne's Thread [AT-0341] ─────────────────────
    // What: Blur QSlider sits on the bar immediately right of the Blur button
    // Why:  Overlay under the icon sat off the bar; Size already uses this slot
    // Date: 2026-08-28
    // Related: [AT-0330] AnnotateWindow.cpp:m_blurSlider, [AT-0333] AnnotateWindow.cpp:m_textSizeSlider
    // ─────────────────────────────────────────────────────
    m_blurSlider = new QSlider(Qt::Horizontal);
    m_blurSlider->setRange(0, 20);
    m_blurSlider->setValue(m_blurRadius);
    m_blurSlider->setFixedWidth(40);
    m_blurSlider->setToolTip(QStringLiteral("Blur"));
    m_blurSlider->setAccessibleName(QStringLiteral("Blur"));
    m_blurSlider->setFocusPolicy(Qt::NoFocus);
    connect(m_blurSlider, &QSlider::sliderPressed, this, &AnnotateWindow::onBlurRadiusPressed);
    connect(m_blurSlider, &QSlider::valueChanged, this, &AnnotateWindow::onBlurRadiusChanged);
    connect(m_blurSlider, &QSlider::sliderReleased, this, &AnnotateWindow::onBlurRadiusReleased);
    m_blurSliderAction = toolbar->addWidget(m_blurSlider);
    m_blurSliderAction->setVisible(false);
    m_blurSlider->hide();
    qInfo() << "AnnotateWindow: blur slider after Blur button radius=" << m_blurRadius;
    // ─── Ariadne's Thread [AT-0127] ─────────────────────
    // What: Photo toolbar button is checkable: press on opens pip, press off stops camera
    // Why:  MenuButtonPopup second click opened Picture/5s and could not turn the pip off
    // Date: 2026-08-26
    // Related: [AT-0075] AnnotateWindow.cpp, docs/PRD-03-photo-cutout.md
    // ─────────────────────────────────────────────────────
    m_photoButton = new QToolButton(toolbar);
    m_photoButton->setObjectName(QStringLiteral("Photo"));
    m_photoButton->setIcon(macToolbarIcon(QStringLiteral("camera"), toolTint));
    m_photoButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_photoButton->setIconSize(QSize(16, 16));
    m_photoButton->setCheckable(true);
    m_photoButton->setFocusPolicy(Qt::NoFocus);
    m_photoButton->setToolTip(QStringLiteral("Photo"));
    m_photoButton->setAccessibleName(QStringLiteral("Photo"));
    connect(m_photoButton, &QToolButton::clicked, this, &AnnotateWindow::togglePhotoPreview);
    toolbar->addWidget(m_photoButton);
    qInfo() << "AnnotateWindow: Photo button no menu chevron iconNull=" << m_photoButton->icon().isNull()
            << "sizeHint=" << m_photoButton->sizeHint();
    // ─── Ariadne's Thread [AT-0165] ─────────────────────
    // What: Center Photo choice card with Picture and 5s timer while the camera is on
    // Why:  Chevron menu is gone; 5s replaces the two buttons with 5..1 then the existing flash
    // Date: 2026-08-26
    // Related: [AT-0075] AnnotateWindow.cpp:runPhotoFollow, [AT-0127] AnnotateWindow.cpp:togglePhotoPreview
    // ─────────────────────────────────────────────────────
    m_photoChoice = new QFrame(this);
    m_photoChoice->setObjectName(QStringLiteral("PhotoChoice"));
    m_photoChoice->setFrameShape(QFrame::StyledPanel);
    m_photoChoice->setStyleSheet(QStringLiteral(
        "#PhotoChoice { background: #2c2c2e; border: 1px solid #6e6e73; border-radius: 12px; }"
        "#PhotoChoice QLabel { color: #f2f2f7; background: transparent; font-size: 28px; }"));
    auto *choiceLay = new QHBoxLayout(m_photoChoice);
    choiceLay->setContentsMargins(16, 12, 16, 12);
    choiceLay->setSpacing(10);
    m_photoPictureButton = makeNativeToolbarButton(QStringLiteral("Picture"), false);
    m_photoTimer5Button = makeNativeToolbarButton(QStringLiteral("5s timer"), false);
    m_photoChoiceCount = new QLabel(QStringLiteral("5"), m_photoChoice);
    m_photoChoiceCount->setAlignment(Qt::AlignCenter);
    m_photoChoiceCount->setMinimumWidth(48);
    m_photoChoiceCount->hide();
    choiceLay->addWidget(m_photoPictureButton);
    choiceLay->addWidget(m_photoTimer5Button);
    choiceLay->addWidget(m_photoChoiceCount);
    connect(m_photoPictureButton, &QPushButton::clicked, this, &AnnotateWindow::startPhotoPicture);
    connect(m_photoTimer5Button, &QPushButton::clicked, this, &AnnotateWindow::startPhotoTimer5);
    m_photoChoice->hide();
    qInfo() << "AnnotateWindow: Photo choice card created";
    // ─── Ariadne's Thread [AT-0379] ─────────────────────
    // What: Copy-success hint is the same QFrame chrome as Photo choice under the agent icon
    // Why:  QToolTip was the system gray box and sat under Save instead of the clicked agent
    // Date: 2026-08-29
    // Related: [AT-0165] AnnotateWindow.cpp:m_photoChoice, [AT-0372] AnnotateWindow.cpp:copyExportedImageToClipboard
    // ─────────────────────────────────────────────────────
    m_copyHint = new QFrame(this);
    m_copyHint->setObjectName(QStringLiteral("CopyHint"));
    m_copyHint->setFrameShape(QFrame::StyledPanel);
    m_copyHint->setAttribute(Qt::WA_StyledBackground, true);
    m_copyHint->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_copyHint->setStyleSheet(QStringLiteral(
        "#CopyHint { background: #2c2c2e; border: 1px solid #6e6e73; border-radius: 12px; }"
        "#CopyHint QLabel { color: #f2f2f7; background: transparent; font-size: 13px; }"));
    auto *copyHintLay = new QHBoxLayout(m_copyHint);
    copyHintLay->setContentsMargins(12, 8, 12, 8);
    copyHintLay->setSpacing(0);
    m_copyHintLabel = new QLabel(m_copyHint);
    m_copyHintLabel->setAlignment(Qt::AlignCenter);
    m_copyHintLabel->setWordWrap(false);
    copyHintLay->addWidget(m_copyHintLabel);
    auto *copyHintShadow = new QGraphicsDropShadowEffect(m_copyHint);
    copyHintShadow->setBlurRadius(22);
    copyHintShadow->setOffset(0, 4);
    copyHintShadow->setColor(QColor(0, 0, 0, 150));
    m_copyHint->setGraphicsEffect(copyHintShadow);
    m_copyHint->hide();
    m_copyHintTimer = new QTimer(this);
    m_copyHintTimer->setSingleShot(true);
    connect(m_copyHintTimer, &QTimer::timeout, this, [this]() {
        qInfo() << "AnnotateWindow: copy hint timeout";
        hideCopyHint();
    });
    qInfo() << "AnnotateWindow: copy hint card created";
    m_highlightAction->setChecked(true);
    toolbar->addSeparator();
    m_undoAction = toolbar->addAction(macToolbarIcon(QStringLiteral("arrow.uturn.backward"), toolTint),
                                      QStringLiteral("Undo"), this, &AnnotateWindow::undo);
    m_undoAction->setToolTip(QStringLiteral("Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction = toolbar->addAction(macToolbarIcon(QStringLiteral("arrow.uturn.forward"), toolTint),
                                      QStringLiteral("Redo"), this, &AnnotateWindow::redo);
    m_redoAction->setToolTip(QStringLiteral("Redo"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    qInfo() << "AnnotateWindow: action icons Undo=" << !m_undoAction->icon().isNull()
            << "Redo=" << !m_redoAction->icon().isNull();
    // ─── Ariadne's Thread [AT-0371] ─────────────────────
    // What: QToolBar separator after Redo
    // Why:  Undo/Redo sat flush against Color without a trailing divider
    // Date: 2026-08-28
    // Related: [AT-0072] AnnotateWindow.cpp:undo, [AT-0370] AnnotateWindow.cpp:m_toolsBar
    // ─────────────────────────────────────────────────────
    toolbar->addSeparator();
    qInfo() << "AnnotateWindow: Redo trailing separator";

    // ─── Ariadne's Thread [AT-0071] ─────────────────────
    // What: Restore slider QLabel text; Save/Share Link are native QPushButton
    // Why:  Icon-only pass removed labels; stylesheet accent hid the Cocoa bezel
    // Date: 2026-08-25
    // Related: [AT-0063] AnnotateWindow.cpp, [AT-0067] AnnotateWindow.cpp
    // ─────────────────────────────────────────────────────
    // ─── Ariadne's Thread [AT-0158] ─────────────────────
    // What: Color is a QToolButton plus a column of solid circles, same popup as Background
    // Why:  Hue slider is gone; presets are the five Background from-colors plus White and Black
    // Date: 2026-08-26
    // Related: [AT-0131] AnnotateWindow.cpp:m_bgButton, [AT-0073] AnnotateWindow.cpp
    // ─────────────────────────────────────────────────────
    m_colorButton = new QToolButton(toolbar);
    m_colorButton->setObjectName(QStringLiteral("ColorPreset"));
    m_colorButton->setText(QStringLiteral("Color"));
    m_colorButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_colorButton->setFocusPolicy(Qt::NoFocus);
    m_colorButton->setToolTip(QStringLiteral("Color"));
    m_colorButton->setAccessibleName(QStringLiteral("Color"));
    m_colorButton->setIconSize(QSize(16, 16));
    auto *colorMenu = new QMenu(m_colorButton);
    auto *colorRow = new QWidget(colorMenu);
    auto *colorLayout = new QVBoxLayout(colorRow);
    colorLayout->setContentsMargins(8, 6, 8, 6);
    colorLayout->setSpacing(6);
    colorLayout->setAlignment(Qt::AlignHCenter);
    m_colorGroup = new QActionGroup(this);
    m_colorGroup->setExclusive(true);
    const QString colorTips[] = {QStringLiteral("Red"), QStringLiteral("Yellow"), QStringLiteral("Green"),
                                 QStringLiteral("Blue"), QStringLiteral("Purple"), QStringLiteral("White"),
                                 QStringLiteral("Black")};
    for (int id = 0; id <= 6; ++id) {
        const QColor swatch = annotatePresetColor(id);
        auto *act = new QAction(colorCircleIcon(swatch), colorTips[id], m_colorGroup);
        act->setCheckable(true);
        act->setData(QVariant::fromValue(swatch));
        act->setToolTip(colorTips[id]);
        m_colorGroup->addAction(act);
        auto *dot = new QToolButton(colorRow);
        dot->setDefaultAction(act);
        dot->setToolButtonStyle(Qt::ToolButtonIconOnly);
        dot->setFocusPolicy(Qt::NoFocus);
        dot->setAutoRaise(true);
        dot->setIconSize(QSize(22, 22));
        colorLayout->addWidget(dot);
        connect(act, &QAction::triggered, this, [this, colorMenu, swatch, id](bool checked) {
            if (!checked) {
                qInfo() << "AnnotateWindow: color action unchecked id=" << id << "rgb=" << swatch.rgb();
                return;
            }
            qInfo() << "AnnotateWindow: color circle picked id=" << id << "color=" << swatch;
            setAnnotateColor(swatch);
            colorMenu->close();
        });
        qInfo() << "AnnotateWindow: color circle action id=" << id << colorTips[id] << swatch;
    }
    auto *colorWidgetAction = new QWidgetAction(colorMenu);
    colorWidgetAction->setDefaultWidget(colorRow);
    colorMenu->addAction(colorWidgetAction);
    connect(m_colorButton, &QToolButton::clicked, this, [this, colorMenu]() {
        if (!m_colorButton || !colorMenu) {
            qWarning() << "AnnotateWindow: Color menu missing on click";
            return;
        }
        const QPoint pos = m_colorButton->mapToGlobal(QPoint(0, m_colorButton->height()));
        qInfo() << "AnnotateWindow: Color menu popup at" << pos << "current=" << m_color;
        colorMenu->popup(pos);
    });
    m_colorButton->setIcon(colorCircleIcon(m_color));
    if (QAction *first = m_colorGroup->actions().value(0)) {
        first->setChecked(true);
    }
    toolbar->addWidget(m_colorButton);
    qInfo() << "AnnotateWindow: Color text+icon button style=" << m_colorButton->toolButtonStyle()
            << "text=" << m_colorButton->text() << "iconNull=" << m_colorButton->icon().isNull()
            << "default=" << m_color;
    m_strokeLabel = new QLabel(QStringLiteral("Stroke"));
    m_stroke = new QSlider(Qt::Horizontal);
    m_stroke->setRange(1, 16);
    m_stroke->setValue(m_strokeWidth);
    m_stroke->setFixedWidth(40);
    m_stroke->setToolTip(QStringLiteral("Stroke"));
    m_stroke->setAccessibleName(QStringLiteral("Stroke"));
    m_stroke->setFocusPolicy(Qt::NoFocus);
    connect(m_stroke, &QSlider::sliderPressed, this, &AnnotateWindow::onStrokePressed);
    connect(m_stroke, &QSlider::valueChanged, this, &AnnotateWindow::onStrokeChanged);
    connect(m_stroke, &QSlider::sliderReleased, this, &AnnotateWindow::onStrokeReleased);
    m_strokeLabelAction = toolbar->addWidget(m_strokeLabel);
    m_strokeAction = toolbar->addWidget(m_stroke);
    m_fillLabel = new QLabel(QStringLiteral("Fill"));
    m_fill = new QSlider(Qt::Horizontal);
    m_fill->setRange(0, 100);
    m_fill->setValue(qRound(m_fillAlpha * 100.0 / 255.0));
    m_fill->setFixedWidth(40);
    m_fill->setToolTip(QStringLiteral("Fill"));
    m_fill->setAccessibleName(QStringLiteral("Fill"));
    m_fill->setFocusPolicy(Qt::NoFocus);
    connect(m_fill, &QSlider::sliderPressed, this, &AnnotateWindow::onFillPressed);
    connect(m_fill, &QSlider::valueChanged, this, &AnnotateWindow::onFillChanged);
    connect(m_fill, &QSlider::sliderReleased, this, &AnnotateWindow::onFillReleased);
    m_fillLabelAction = toolbar->addWidget(m_fillLabel);
    m_fillAction = toolbar->addWidget(m_fill);
    qInfo() << "AnnotateWindow: Stroke/Fill actions created, hidden until Square or Steps is current";
    // ─── Ariadne's Thread [AT-0073] ─────────────────────
    // What: Background is a QToolButton whose InstantPopup menu is a column of circles
    // Why:  QComboBox text list hid the gradient presets; each circle is one existing preset
    // Date: 2026-08-25
    // Related: [AT-0071] AnnotateWindow.cpp, [AT-0056] AnnotateWindow.cpp:applyCanvasChrome
    // ─────────────────────────────────────────────────────
    m_bgButton = new QToolButton(toolbar);
    m_bgButton->setObjectName(QStringLiteral("BgPreset"));
    m_bgButton->setText(QStringLiteral("Background"));
    m_bgButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_bgButton->setFocusPolicy(Qt::NoFocus);
    m_bgButton->setToolTip(QStringLiteral("Background"));
    m_bgButton->setAccessibleName(QStringLiteral("Background"));
    m_bgButton->setIconSize(QSize(16, 16));
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
    toolbar->addWidget(m_bgButton);
    qInfo() << "AnnotateWindow: Background text+icon button style=" << m_bgButton->toolButtonStyle()
            << "text=" << m_bgButton->text() << "iconNull=" << m_bgButton->icon().isNull();
    m_radiusLabel = new QLabel(QStringLiteral("Radius"));
    m_radius = new QSlider(Qt::Horizontal);
    m_radius->setRange(0, 80);
    m_radius->setValue(m_cornerRadius);
    m_radius->setFixedWidth(40);
    m_radius->setToolTip(QStringLiteral("Radius"));
    m_radius->setAccessibleName(QStringLiteral("Radius"));
    m_radius->setFocusPolicy(Qt::NoFocus);
    connect(m_radius, &QSlider::valueChanged, this, &AnnotateWindow::onRadiusChanged);
    m_radiusLabelAction = toolbar->addWidget(m_radiusLabel);
    m_radiusAction = toolbar->addWidget(m_radius);
    m_shadowLabel = new QLabel(QStringLiteral("Shadow"));
    m_shadow = new QSlider(Qt::Horizontal);
    m_shadow->setRange(0, 40);
    m_shadow->setValue(m_shadowAmount);
    m_shadow->setFixedWidth(40);
    m_shadow->setToolTip(QStringLiteral("Shadow"));
    m_shadow->setAccessibleName(QStringLiteral("Shadow"));
    m_shadow->setFocusPolicy(Qt::NoFocus);
    connect(m_shadow, &QSlider::valueChanged, this, &AnnotateWindow::onShadowChanged);
    m_shadowLabelAction = toolbar->addWidget(m_shadowLabel);
    m_shadowAction = toolbar->addWidget(m_shadow);
    qInfo() << "AnnotateWindow: Radius/Shadow actions created, hidden until Background is on";
    auto *spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    spacer->setMinimumWidth(0);
    toolbar->addWidget(spacer);
    // ─── Ariadne's Thread [AT-0368] ─────────────────────
    // What: Separator then Claude/Cursor/Codex/OpenCode copy the exported screenshot
    // Why:  Paste the full annotated image into Cursor, VS Code, Claude, and other chats
    // Date: 2026-08-28
    // Related: [AT-0368] MacIcons.mm:macResourceIcon, [AT-0076] AnnotateWindow.cpp:exportedImage
    // ─────────────────────────────────────────────────────
    toolbar->addSeparator();
    // ─── Ariadne's Thread [AT-0382] ─────────────────────
    // What: Toolbar order is Claude, Codex blob, Cursor cube, OpenCode
    // Why:  Cube is Cursor; the blob to its left is Codex; copy hint must match the mark
    // Date: 2026-08-29
    // Related: [AT-0381] packaging/macos/agents, [AT-0379] AnnotateWindow.cpp:showCopyHint
    // ─────────────────────────────────────────────────────
    const struct {
        const char *file;
        const char *name;
    } agents[] = {{"claude.svg", "Claude"},
                   {"codex.svg", "Codex"},
                   {"cursor.svg", "Cursor"},
                   {"opencode.svg", "OpenCode"}};
    for (const auto &agent : agents) {
        auto *btn = new QToolButton(toolbar);
        btn->setIcon(macResourceIcon(QString::fromUtf8(agent.file)));
        btn->setIconSize(QSize(16, 16));
        btn->setAccessibleName(QString::fromUtf8(agent.name));
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setCheckable(false);
        btn->setAutoRaise(true);
        const QString agentName = QString::fromUtf8(agent.name);
        const QString agentFile = QString::fromUtf8(agent.file);
        connect(btn, &QToolButton::clicked, this, [this, agentName, agentFile, btn]() {
            qInfo() << "AnnotateWindow: agent copy click name=" << agentName
                    << "file=" << agentFile
                    << "btn=" << static_cast<void *>(btn) << "size=" << btn->size();
            copyExportedImageToClipboard(btn, agentName);
        });
        toolbar->addWidget(btn);
        qInfo() << "AnnotateWindow: agent button name=" << agent.name
                << "file=" << agent.file << "iconNull=" << btn->icon().isNull();
    }
    // ─── Ariadne's Thread [AT-0370] ─────────────────────
    // What: Trailing QToolBar separator after the agent copy icons
    // Why:  Agents sat flush against Save without a divider on the right
    // Date: 2026-08-28
    // Related: [AT-0368] AnnotateWindow.cpp:copyExportedImageToClipboard, [AT-0150] AnnotateWindow.cpp:m_toolsBar
    // ─────────────────────────────────────────────────────
    toolbar->addSeparator();
    qInfo() << "AnnotateWindow: agent icons trailing separator";
    // ─── Ariadne's Thread [AT-0311] ─────────────────────
    // What: Share Link is the default (blue) bezel; Save is the gray button
    // Why:  Share is the primary action; Save was drawing the accent by mistake
    // Date: 2026-08-28
    // Related: [AT-0071] AnnotateWindow.cpp:makeNativeToolbarButton
    // ─────────────────────────────────────────────────────
    QPushButton *saveBtn = makeNativeToolbarButton(QStringLiteral("Save"), false);
    m_saveBtn = saveBtn;
    m_shareBtn = makeNativeToolbarButton(QStringLiteral("Share Link"), true);
    qInfo() << "AnnotateWindow: Share Link default=true Save default=false";
    m_shareBtn->setMinimumWidth(m_shareBtn->sizeHint().width());
    m_shareBusy = new QProgressBar(m_shareBtn);
    m_shareBusy->setRange(0, 0);
    m_shareBusy->setTextVisible(false);
    m_shareBusy->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_shareBusy->hide();
    m_shareProgress = new QProgressBar(toolbar);
    m_shareProgress->setRange(0, 100);
    m_shareProgress->setValue(0);
    m_shareProgress->setTextVisible(true);
    m_shareProgress->setFixedWidth(88);
    m_shareProgress->setFixedHeight(16);
    m_shareProgress->hide();
    connect(m_saveBtn, &QPushButton::clicked, this, &AnnotateWindow::saveLocal);
    connect(m_shareBtn, &QPushButton::clicked, this, &AnnotateWindow::share);
    if (m_auth) {
        connect(m_auth, &AuthSession::websiteSignInSettled, this, &AnnotateWindow::onWebsiteSignInSettled);
        qInfo() << "AnnotateWindow: connected websiteSignInSettled";
    } else {
        qWarning() << "AnnotateWindow: no AuthSession, Share cannot start website sign-in";
    }
    toolbar->addWidget(m_saveBtn);
    toolbar->addWidget(m_shareProgress);
    toolbar->addWidget(m_shareBtn);
    qInfo() << "AnnotateWindow: slider labels on; Save/Share Link native QPushButton shareProgress=on";

    // ─── Ariadne's Thread [AT-0396] ─────────────────────
    // What: Blur sidebar in a right-hand column next to the view
    // Why:  Overlay would cover the shot; viewport shrink must re-fit
    // Date: 2026-09-03
    // Related: [AT-0390] app→LocalStore.cpp:blurAutomatic, [AT-0338] AnnotateWindow.cpp:m_blurButton
    // ─────────────────────────────────────────────────────
    ensureBlurSidebar();
    m_editorBody = new QWidget(this);
    auto *bodyLay = new QHBoxLayout(m_editorBody);
    bodyLay->setContentsMargins(0, 0, 0, 0);
    bodyLay->setSpacing(0);
    bodyLay->setSizeConstraint(QLayout::SetNoConstraint);
    bodyLay->addWidget(m_view, 1);
    bodyLay->addWidget(m_blurSidebar, 0);
    m_blurSidebar->hide();
    setCentralWidget(m_editorBody);
    layoutToolsBar();
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
    updateStrokeFillVisibility();
    applyWindowScreenLayout();
    ensureUpdateCard();
    if (QStyleHints *hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme scheme) {
            qInfo() << "AnnotateWindow: colorSchemeChanged scheme=" << static_cast<int>(scheme);
            applyAnnotateChromeTheme();
        });
        qInfo() << "AnnotateWindow: watching colorScheme=" << static_cast<int>(hints->colorScheme());
    }
    applyAnnotateChromeTheme();
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
    if (m_shareUploading) {
        qInfo() << "AnnotateWindow: close ignored while share upload is running";
        event->ignore();
        return;
    }
    qInfo() << "AnnotateWindow: close, stop photo without destroying Tool QWindow"
            << " quitOnClose=" << testAttribute(Qt::WA_QuitOnClose);
    abortPhotoCycle(true, false);
    commitTextEdit();
    ++m_redactGeneration;
    m_redactBusy = false;
    qInfo() << "AnnotateWindow: close bump redactGeneration=" << m_redactGeneration;
    // ─── Ariadne's Thread [AT-0207] ─────────────────────
    // What: Disconnect QUndoStack before the widget tree is torn down
    // Why:  ~QUndoStack emits indexChanged after QWidget children are gone; selectedAnnotation SIGSEGV
    // Date: 2026-08-27
    // Related: [AT-0203] AnnotateWindow.cpp:abortPhotoCycle, [AT-0204] Application.cpp:eventFilter
    // ─────────────────────────────────────────────────────
    if (m_undo) {
        m_undo->disconnect(this);
        m_undo->blockSignals(true);
        qInfo() << "AnnotateWindow: undo disconnected before close index=" << m_undo->index()
                << " count=" << m_undo->count();
    }
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
    layoutToolsBar();
    layoutPhotoChoice();
    if (!m_redactShowKickoff) {
        m_redactShowKickoff = true;
        QTimer::singleShot(0, this, [this]() {
            qInfo() << "AnnotateWindow: auto redact kickoff after show restored=" << m_redactSessionRestored;
            maybeStartAutoRedact();
        });
    }
    qInfo() << "AnnotateWindow: showEvent layout applied";
}

void AnnotateWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    fitShotToWindow();
    layoutPhotoOverlay();
    layoutUpdateCard();
    layoutToolsBar();
    layoutPhotoChoice();
    qInfo() << "AnnotateWindow: resizeEvent" << event->size();
}

void AnnotateWindow::moveEvent(QMoveEvent *event)
{
    QMainWindow::moveEvent(event);
    layoutPhotoOverlay();
    qInfo() << "AnnotateWindow: moveEvent" << event->pos();
}

void AnnotateWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (!event) {
        return;
    }
    if (event->type() == QEvent::ThemeChange || event->type() == QEvent::PaletteChange) {
        qInfo() << "AnnotateWindow: changeEvent type=" << static_cast<int>(event->type());
        applyAnnotateChromeTheme();
    }
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

// ─── Ariadne's Thread [AT-0407] ─────────────────────
// What: Second Blur click returns to Select and hides the sidebar
// Why:  Exclusive QActionGroup kept Blur checked so the panel could not be dismissed
// Date: 2026-09-03
// Related: [AT-0398] AnnotateWindow.cpp:setBlurSidebarVisible, [AT-0338] AnnotateWindow.cpp:m_blurAction
// ─────────────────────────────────────────────────────
void AnnotateWindow::setToolBlur()
{
    commitTextEdit();
    if (m_tool == Tool::Blur) {
        qInfo() << "AnnotateWindow: Blur clicked while active, close sidebar";
        setToolSelect();
        return;
    }
    m_tool = Tool::Blur;
    m_drawing = false;
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
    setBlurSidebarVisible(m_tool == Tool::Blur);
    qInfo() << "AnnotateWindow: syncToolActions checked=" << target->text()
            << "blurSidebar=" << (m_tool == Tool::Blur);
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

// ─── Ariadne's Thread [AT-0329] ─────────────────────
// What: Clip a drag rect to shotRect so outside press still paints only on the shot
// Why:  Users start Square/Blur/Arrow/Line from the window chrome around the image
// Date: 2026-08-28
// Related: [AT-0328] AnnotateWindow.cpp:viewPress, [AT-0050] AnnotateWindow.cpp:shotRect
// ─────────────────────────────────────────────────────
QRectF AnnotateWindow::clipDraftToShot(const QPointF &from, const QPointF &to) const
{
    const QRectF shot = shotRect();
    const QRectF box = QRectF(from, to).normalized().intersected(shot);
    return box;
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

void AnnotateWindow::layoutToolsBar()
{
    if (!m_toolsBar) {
        qWarning() << "AnnotateWindow: layoutToolsBar missing bar";
        return;
    }
    m_toolsBar->adjustSize();
    const QSize hint = m_toolsBar->sizeHint();
    const int margin = 12;
    const int maxW = qMax(1, width() - margin * 2);
    const int w = qMin(hint.width(), maxW);
    const int h = hint.height();
    const int x = qMax(margin, (width() - w) / 2);
    m_toolsBar->setGeometry(x, margin, w, h);
    m_toolsBar->raise();
    m_toolsBar->show();
    qInfo() << "AnnotateWindow: tools bar geo=" << m_toolsBar->geometry() << "hint=" << hint << "windowW=" << width()
            << "overflow=" << (hint.width() > maxW);
    layoutCopyHint();
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

// ─── Ariadne's Thread [AT-0158] ─────────────────────
// What: Apply a Color menu swatch to m_color and the last colorable item
// Why:  One circle pick is one undo; draft text still skips undo
// Date: 2026-08-26
// Related: [AT-0115] AnnotateWindow.cpp:onHueChanged, [AT-0114] AnnotateWindow.cpp:lastColorableItem
// ─────────────────────────────────────────────────────
void AnnotateWindow::setAnnotateColor(const QColor &color)
{
    if (!color.isValid()) {
        qWarning() << "AnnotateWindow: setAnnotateColor invalid color";
        return;
    }
    if (!m_undo) {
        qWarning() << "AnnotateWindow: setAnnotateColor missing undo stack color=" << color;
        return;
    }
    m_color = QColor(color.red(), color.green(), color.blue());
    qInfo() << "AnnotateWindow: annotate color=" << m_color;
    if (m_colorButton) {
        m_colorButton->setIcon(colorCircleIcon(m_color));
        m_colorButton->setToolTip(QStringLiteral("Color"));
        qInfo() << "AnnotateWindow: color button icon rgb=" << m_color.rgb();
    }
    if (m_colorGroup) {
        const auto acts = m_colorGroup->actions();
        for (QAction *act : acts) {
            const QColor swatch = act->data().value<QColor>();
            QSignalBlocker block(act);
            act->setChecked(swatch.isValid() && swatch.rgb() == m_color.rgb());
            qInfo() << "AnnotateWindow: color action check rgb=" << swatch.rgb()
                    << "on=" << act->isChecked();
        }
    }
    QGraphicsItem *last = lastColorableItem();
    if (!last) {
        qInfo() << "AnnotateWindow: no last colorable item, next stroke will use picked color";
        return;
    }
    if (itemColor(last).rgb() == m_color.rgb()) {
        qInfo() << "AnnotateWindow: last item already this color";
        return;
    }
    if (m_editingText && m_textDraft) {
        applyItemColor(m_editingText, m_color);
        qInfo() << "AnnotateWindow: color on draft text, no undo";
        return;
    }
    ++m_colorGestureId;
    m_colorGestureActive = false;
    m_undo->push(new ChangeColorCommand(last, m_color, m_colorGestureId));
    qInfo() << "AnnotateWindow: color undo gesture id=" << m_colorGestureId << "item=" << last;
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

void AnnotateWindow::onBlurRadiusPressed()
{
    ++m_blurGestureId;
    m_blurGestureActive = true;
    qInfo() << "AnnotateWindow: blur radius gesture start id=" << m_blurGestureId;
}

void AnnotateWindow::onBlurRadiusChanged(int radius)
{
    m_blurRadius = qBound(0, radius, 20);
    qInfo() << "AnnotateWindow: blur radius=" << m_blurRadius;
    QGraphicsItem *item = selectedAnnotation();
    if (!item || annotateKind(item) != AnnotateKind::Blur) {
        qInfo() << "AnnotateWindow: blur slider skip, no selected blur";
        return;
    }
    if (blurRadius(item) == m_blurRadius) {
        qInfo() << "AnnotateWindow: selected blur already this radius";
        return;
    }
    const int gesture = m_blurGestureActive ? m_blurGestureId : ++m_blurGestureId;
    m_undo->push(new ChangeBlurRadiusCommand(item, m_blurRadius, gesture));
}

void AnnotateWindow::onBlurRadiusReleased()
{
    qInfo() << "AnnotateWindow: blur radius gesture end id=" << m_blurGestureId;
    m_blurGestureActive = false;
}

void AnnotateWindow::onTextSizePressed()
{
    ++m_textSizeGestureId;
    m_textSizeGestureActive = true;
    qInfo() << "AnnotateWindow: text size gesture start id=" << m_textSizeGestureId;
}

void AnnotateWindow::onTextSizeChanged(int size)
{
    m_textSize = qBound(10, size, 48);
    qInfo() << "AnnotateWindow: text size=" << m_textSize;
    QGraphicsItem *last = lastTextSizedItem();
    if (!last) {
        qInfo() << "AnnotateWindow: no last text or steps, next item will use slider size";
        return;
    }
    if (annotateTextSize(last) == m_textSize) {
        qInfo() << "AnnotateWindow: last text-sized item already this size";
        return;
    }
    const int gesture = m_textSizeGestureActive ? m_textSizeGestureId : ++m_textSizeGestureId;
    m_undo->push(new ChangeTextSizeCommand(last, m_textSize, gesture));
}

void AnnotateWindow::onTextSizeReleased()
{
    qInfo() << "AnnotateWindow: text size gesture end id=" << m_textSizeGestureId;
    m_textSizeGestureActive = false;
}

void AnnotateWindow::onTextOutlineToggled(bool on)
{
    m_textOutline = on;
    qInfo() << "AnnotateWindow: text outline=" << m_textOutline;
    QGraphicsItem *last = lastTextSizedItem();
    if (!last) {
        qInfo() << "AnnotateWindow: no last text or steps, next item will use outline";
        return;
    }
    if (annotateTextOutline(last) == m_textOutline) {
        qInfo() << "AnnotateWindow: last text-sized item already this outline";
        return;
    }
    m_undo->push(new ChangeTextOutlineCommand(last, m_textOutline));
}

void AnnotateWindow::showError(const QString &code)
{
    qWarning() << "AnnotateWindow: showError" << code;
    QMessageBox::warning(this, QStringLiteral("SeenShot"), ErrorCatalog::message(code));
}

// ─── Ariadne's Thread [AT-0312] ─────────────────────
// What: Quota eviction dialog: Upgrade opens Polar checkout
// Why:  OK hid the upgrade path after Free 10 MB eviction
// Date: 2026-08-28
// Related: [AT-0002] ErrorCatalog.cpp:QUOTA_EVICTED, [AT-0276] CloudClient.cpp:createCheckoutUrl
// ─────────────────────────────────────────────────────
void AnnotateWindow::showQuotaEvicted()
{
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("SeenShot"));
    box.setIcon(QMessageBox::Information);
    box.setText(ErrorCatalog::message(QStringLiteral("QUOTA_EVICTED")));
    QPushButton *upgrade = box.addButton(QStringLiteral("Upgrade"), QMessageBox::AcceptRole);
    box.setDefaultButton(upgrade);
    qInfo() << "AnnotateWindow: quota evicted dialog";
    box.exec();
    if (box.clickedButton() != upgrade) {
        qInfo() << "AnnotateWindow: quota evicted dialog dismissed without Upgrade";
        return;
    }
    if (!m_cloud) {
        qWarning() << "AnnotateWindow: quota Upgrade missing CloudClient";
        showError(QStringLiteral("UNKNOWN_ERROR"));
        return;
    }
    QString url;
    QString error;
    if (!m_cloud->createCheckoutUrl(&url, &error)) {
        qWarning() << "AnnotateWindow: quota Upgrade checkout failed code=" << error << " urlEmpty=" << url.isEmpty();
        showError(error.isEmpty() ? QStringLiteral("UNKNOWN_ERROR") : error);
        return;
    }
    const QUrl checkout(url);
    qInfo() << "AnnotateWindow: quota Upgrade checkout urlChars=" << url.size() << " host=" << checkout.host();
    if (!MacPermissions::openDefaultBrowser(checkout)) {
        qWarning() << "AnnotateWindow: quota Upgrade open browser failed host=" << checkout.host();
        showError(QStringLiteral("UNKNOWN_ERROR"));
    }
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

// ─── Ariadne's Thread [AT-0368] ─────────────────────
// What: Put the full exported screenshot on the pasteboard as PNG
// Why:  Agent toolbar buttons paste the image into Cursor, VS Code, Claude chats
// Date: 2026-08-28
// Related: [AT-0076] AnnotateWindow.cpp:exportedImage, [AT-0368] MacIcons.mm:macResourceIcon
// ─────────────────────────────────────────────────────
void AnnotateWindow::copyExportedImageToClipboard(QWidget *anchor, const QString &agentName)
{
    commitTextEdit();
    const QImage image = exportedImage();
    if (image.isNull() || image.width() < 1 || image.height() < 1) {
        qWarning() << "AnnotateWindow: copy screenshot skipped empty size=" << image.size()
                   << "agent=" << agentName << "anchor=" << static_cast<void *>(anchor);
        return;
    }
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly)) {
        qWarning() << "AnnotateWindow: copy screenshot buffer open failed size=" << image.size()
                   << "agent=" << agentName;
        return;
    }
    if (!image.save(&buffer, "PNG")) {
        qWarning() << "AnnotateWindow: copy screenshot PNG encode failed size=" << image.size()
                   << "agent=" << agentName;
        return;
    }
    auto *mime = new QMimeData;
    mime->setImageData(image);
    mime->setData(QStringLiteral("image/png"), png);
    QGuiApplication::clipboard()->setMimeData(mime);
    const QString hint = QStringLiteral("Screenshot copied. Paste it in %1").arg(agentName);
    qInfo() << "AnnotateWindow: copy screenshot pngBytes=" << png.size() << "size=" << image.size()
            << "format=" << image.format() << "agent=" << agentName << "hint=" << hint
            << "anchor=" << static_cast<void *>(anchor)
            << "anchorSize=" << (anchor ? anchor->size() : QSize())
            << "anchorVisible=" << (anchor && anchor->isVisible());
    showCopyHint(anchor, hint);
}

// ─── Ariadne's Thread [AT-0379] ─────────────────────
// What: Position the CopyHint QFrame under the clicked agent button
// Why:  Same chrome as Photo choice; QToolTip was the system box under Save
// Date: 2026-08-29
// Related: [AT-0379] AnnotateWindow.cpp:m_copyHint, [AT-0165] AnnotateWindow.cpp:layoutPhotoChoice
// ─────────────────────────────────────────────────────
void AnnotateWindow::showCopyHint(QWidget *anchor, const QString &text)
{
    if (!m_copyHint || !m_copyHintLabel || !m_copyHintTimer) {
        qWarning() << "AnnotateWindow: copy hint skipped missing card text=" << text
                   << "anchor=" << static_cast<void *>(anchor);
        return;
    }
    if (!anchor) {
        qWarning() << "AnnotateWindow: copy hint skipped null anchor text=" << text;
        return;
    }
    m_copyHintAnchor = anchor;
    m_copyHintLabel->setText(text);
    m_copyHint->adjustSize();
    m_copyHint->show();
    layoutCopyHint();
    m_copyHintTimer->start(4000);
    qInfo() << "AnnotateWindow: copy hint shown text=" << text << "anchor=" << static_cast<void *>(anchor)
            << "anchorSize=" << anchor->size() << "geo=" << m_copyHint->geometry()
            << "timerMs=" << m_copyHintTimer->interval();
}

void AnnotateWindow::hideCopyHint()
{
    if (m_copyHintTimer) {
        m_copyHintTimer->stop();
    }
    if (m_copyHint) {
        m_copyHint->hide();
    }
    m_copyHintAnchor = nullptr;
    qInfo() << "AnnotateWindow: copy hint hidden";
}

void AnnotateWindow::layoutCopyHint()
{
    if (!m_copyHint || !m_copyHint->isVisible()) {
        return;
    }
    if (!m_copyHintAnchor) {
        qWarning() << "AnnotateWindow: copy hint layout skipped missing anchor";
        hideCopyHint();
        return;
    }
    m_copyHint->adjustSize();
    const QSize hint = m_copyHint->sizeHint();
    const int w = qMax(1, hint.width());
    const int h = qMax(1, hint.height());
    const QPoint below = m_copyHintAnchor->mapTo(
        this, QPoint(m_copyHintAnchor->width() / 2, m_copyHintAnchor->height() + 8));
    const int pad = 8;
    int x = below.x() - w / 2;
    int y = below.y();
    x = qBound(pad, x, qMax(pad, width() - w - pad));
    y = qBound(pad, y, qMax(pad, height() - h - pad));
    m_copyHint->setGeometry(x, y, w, h);
    m_copyHint->raise();
    qInfo() << "AnnotateWindow: copy hint geo=" << m_copyHint->geometry() << "below=" << below
            << "anchorSize=" << m_copyHintAnchor->size() << "window=" << size() << "text="
            << (m_copyHintLabel ? m_copyHintLabel->text() : QString());
}

void AnnotateWindow::saveLocal()
{
    commitTextEdit();
    // ─── Ariadne's Thread [AT-0076] ─────────────────────
    // What: Suggested Save name is SeenShot-localTimeWithSeconds-12hex.png
    // Why:  Fixed seenshot.png collided; each local PNG must be unique
    // Date: 2026-08-25
    // Related: [AT-0184] AnnotateWindow.cpp:makeShotFileId, [AT-0057] AnnotateWindow.cpp:share
    // ─────────────────────────────────────────────────────
    const QString hash = m_fileId;
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HHmmss"));
    const QString fileName = QStringLiteral("%1-%2-%3.png")
                                 .arg(QApplication::applicationName(), stamp, hash);
    const QString suggested = QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath(fileName);
    qInfo() << "AnnotateWindow: local save dialog start suggested=" << suggested << "stamp=" << stamp
            << "hash=" << hash << "fileId=" << m_fileId;
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save PNG"),
        suggested,
        QStringLiteral("PNG (*.png)"));
    if (path.isEmpty()) {
        qInfo() << "AnnotateWindow: local save cancelled";
        return;
    }
    qInfo() << "AnnotateWindow: local save chosen" << path;
    // ─── Ariadne's Thread [AT-0215] ─────────────────────
    // What: Write Save PNG from CloudPngEncoder::encode via QFile
    // Why:  Same bytes as Share PUT; QImage::save used a second encoder
    // Date: 2026-08-27
    // Related: [AT-0214] CloudPngEncoder.cpp:encode, [AT-0057] AnnotateWindow.cpp:share
    // ─────────────────────────────────────────────────────
    QString encodeCode;
    const QByteArray png = CloudPngEncoder::encode(exportedImage(), &encodeCode);
    if (png.isEmpty()) {
        qWarning() << "AnnotateWindow: local save encode failed path=" << path
                   << " encodeCode=" << encodeCode << " fileId=" << m_fileId;
        showError(encodeCode.isEmpty() ? QStringLiteral("LOCAL_SAVE_FAILED") : encodeCode);
        return;
    }
    qInfo() << "AnnotateWindow: local save encode ok path=" << path << " pngBytes=" << png.size()
            << " fileId=" << m_fileId;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "AnnotateWindow: local save open failed path=" << path
                   << " error=" << file.errorString() << " pngBytes=" << png.size();
        showError(QStringLiteral("LOCAL_SAVE_FAILED"));
        return;
    }
    const qint64 written = file.write(png);
    if (written != png.size()) {
        qWarning() << "AnnotateWindow: local save write failed path=" << path
                   << " written=" << written << " expected=" << png.size()
                   << " error=" << file.errorString();
        file.close();
        showError(QStringLiteral("LOCAL_SAVE_FAILED"));
        return;
    }
    if (!file.flush()) {
        qWarning() << "AnnotateWindow: local save flush failed path=" << path
                   << " error=" << file.errorString() << " pngBytes=" << png.size();
        file.close();
        showError(QStringLiteral("LOCAL_SAVE_FAILED"));
        return;
    }
    file.close();
    qInfo() << "AnnotateWindow: saved local path=" << path << " pngBytes=" << png.size()
            << " fileId=" << m_fileId;
    Analytics::instance().track(QStringLiteral("save"));
    // ─── Ariadne's Thread [AT-0316] ─────────────────────
    // What: Close the editor after a successful Save write
    // Why:  The annotate window stayed open after the PNG was on disk
    // Date: 2026-08-28
    // Related: [AT-0202] AnnotateWindow.cpp:closeEvent, [AT-0317] AnnotateWindow.cpp:share
    // ─────────────────────────────────────────────────────
    qInfo() << "AnnotateWindow: close after local save path=" << path;
    close();
}

// ─── Ariadne's Thread [AT-0057] ─────────────────────
// What: Drop Save to Cloud; Share is the only cloud upload + publish path
// Why:  Save to Cloud duplicated Share (private upload then publish)
// Date: 2026-08-25
// Related: [AT-0012] AnnotateWindow.cpp:saveLocal, CloudClient.cpp:uploadAndPublish
// ─────────────────────────────────────────────────────
// ─── Ariadne's Thread [AT-0169] ─────────────────────
// What: Disable Share, show in-button loader and PUT progress, then copyable link
// Why:  User needs upload feedback and a one-click copy of the public URL
// Date: 2026-08-26
// Related: [AT-0057] AnnotateWindow.cpp:share, [AT-0020] CloudClient.cpp:uploadAndPublish
// ─────────────────────────────────────────────────────
void AnnotateWindow::share()
{
    if (m_shareUploading) {
        qWarning() << "AnnotateWindow: share ignored, already uploading";
        return;
    }
    commitTextEdit();
    QString code;
    // ─── Ariadne's Thread [AT-0199] ─────────────────────
    // What: Share starts website sign-in and resumes after websiteSignInSettled
    // Why:  No SignInDialog; same default-browser OAuth as Settings Sign In
    // Date: 2026-08-27
    // Related: [AT-0198] SettingsWindow.cpp:openSignIn, [AT-0193] AuthSession.cpp:startWebsiteSignIn
    // ─────────────────────────────────────────────────────
    if (!m_auth) {
        qWarning() << "AnnotateWindow: share needs AuthSession";
        showError(QStringLiteral("STORAGE_NEED_SIGN_IN"));
        return;
    }
    if (!m_auth->hasSession()) {
        qInfo() << "AnnotateWindow: share needs sign-in";
        m_shareAfterSignIn = true;
        QString error;
        if (!m_auth->startWebsiteSignIn(&error)) {
            if (error == QLatin1String("AUTH_IN_PROGRESS")) {
                qInfo() << "AnnotateWindow: share waiting for in-progress website sign-in";
                return;
            }
            m_shareAfterSignIn = false;
            qWarning() << "AnnotateWindow: website sign-in start failed code=" << error;
            showError(error.isEmpty() ? QStringLiteral("AUTH_OAUTH_FAILED") : error);
            return;
        }
        if (m_auth->hasSession()) {
            m_shareAfterSignIn = false;
            qInfo() << "AnnotateWindow: session appeared before website browser";
        } else {
            qInfo() << "AnnotateWindow: website sign-in started for share";
            return;
        }
    }
    if (!ensureOnlineSignedIn(&code)) {
        showError(code);
        return;
    }
    setShareBusy(true);
    // ─── Ariadne's Thread [AT-0210] ─────────────────────
    // What: Open /screenshot/{fileId}?uploading=1 in the default browser before PUT
    // Why:  CloudClient QEventLoop would delay the tab until confirm; the site shows 0-100%
    // Date: 2026-08-27
    // Related: [AT-0209] MacPermissions.mm:openDefaultBrowser, [AT-0184] AnnotateWindow.cpp:makeShotFileId
    // ─────────────────────────────────────────────────────
    const QUrl sharePageUrl(Config::websiteBaseUrl() + QStringLiteral("/screenshot/") + m_fileId
                            + QStringLiteral("?uploading=1"));
    qInfo() << "AnnotateWindow: share open browser url=" << sharePageUrl.toString() << " fileId=" << m_fileId
            << " cloudShotId=" << m_cloudShotId << " cloudShotEmpty=" << m_cloudShotId.isEmpty()
            << " host=" << sharePageUrl.host() << " path=" << sharePageUrl.path();
    if (!MacPermissions::openDefaultBrowser(sharePageUrl)) {
        qWarning() << "AnnotateWindow: share open browser failed url=" << sharePageUrl.toString()
                   << " fileId=" << m_fileId;
    }
    QString url;
    CloudConfirmResult result;
    if (!m_cloudShotId.isEmpty()) {
        qInfo() << "AnnotateWindow: share publishExisting shot=" << m_cloudShotId;
        if (!m_cloud->publishExisting(m_cloudShotId, &url, &code)) {
            setShareBusy(false);
            showError(code);
            return;
        }
    } else {
        QString encodeCode;
        const QByteArray png = CloudPngEncoder::encode(exportedImage(), &encodeCode);
        if (png.isEmpty()) {
            setShareBusy(false);
            showError(encodeCode.isEmpty() ? QStringLiteral("CLOUD_IMAGE_REJECTED") : encodeCode);
            return;
        }
        qInfo() << "AnnotateWindow: share upload pngBytes=" << png.size() << " fileId=" << m_fileId;
        if (!m_cloud->uploadAndPublish(png, m_fileId, &url, &result, &code,
                                      [this](qint64 sent, qint64 total) { setShareProgress(sent, total); })) {
            setShareBusy(false);
            showError(code);
            return;
        }
        m_cloudShotId = result.shotId;
    }
    setShareBusy(false);
    if (!result.evictedIds.isEmpty()) {
        qInfo() << "AnnotateWindow: share evicted count=" << result.evictedIds.size();
        showQuotaEvicted();
    }
    qInfo() << "AnnotateWindow: published" << url << " fileId=" << m_fileId << " cloudShotId=" << m_cloudShotId;
    Analytics::instance().track(QStringLiteral("share"));
    // ─── Ariadne's Thread [AT-0317] ─────────────────────
    // What: Close the editor after Share publish succeeds
    // Why:  The annotate window stayed open after the public page was already open
    // Date: 2026-08-28
    // Related: [AT-0316] AnnotateWindow.cpp:saveLocal, [AT-0202] AnnotateWindow.cpp:closeEvent
    // ─────────────────────────────────────────────────────
    qInfo() << "AnnotateWindow: close after share fileId=" << m_fileId << " cloudShotId=" << m_cloudShotId;
    close();
}

void AnnotateWindow::onWebsiteSignInSettled(const QString &errorCode)
{
    const bool pendingShare = m_shareAfterSignIn;
    m_shareAfterSignIn = false;
    const bool signedIn = m_auth && m_auth->hasSession();
    qInfo() << "AnnotateWindow: website sign-in settled code=" << errorCode
            << " pendingShare=" << pendingShare << " hasSession=" << signedIn;
    if (!errorCode.isEmpty() && errorCode != QLatin1String("AUTH_OAUTH_DENIED")) {
        showError(errorCode);
        return;
    }
    if (errorCode == QLatin1String("AUTH_OAUTH_DENIED")) {
        qInfo() << "AnnotateWindow: website sign-in canceled, share not resumed";
        return;
    }
    if (pendingShare && signedIn) {
        qInfo() << "AnnotateWindow: resume share after website sign-in";
        share();
    }
}

void AnnotateWindow::setShareBusy(bool busy)
{
    m_shareUploading = busy;
    if (!m_shareBtn) {
        qWarning() << "AnnotateWindow: setShareBusy missing share button busy=" << busy;
        return;
    }
    m_shareBtn->setEnabled(!busy);
    m_shareBtn->setText(busy ? QString() : QStringLiteral("Share Link"));
    if (m_shareBusy) {
        if (busy) {
            const int side = qBound(12, m_shareBtn->height() - 8, 16);
            m_shareBusy->setFixedSize(side, side);
            m_shareBusy->move((m_shareBtn->width() - side) / 2, (m_shareBtn->height() - side) / 2);
            m_shareBusy->setRange(0, 0);
            m_shareBusy->show();
            m_shareBusy->raise();
        } else {
            m_shareBusy->hide();
        }
    }
    if (m_shareProgress) {
        if (busy) {
            m_shareProgress->setRange(0, 0);
            m_shareProgress->setValue(0);
            m_shareProgress->show();
        } else {
            m_shareProgress->hide();
            m_shareProgress->setRange(0, 100);
            m_shareProgress->setValue(0);
        }
    }
    qInfo() << "AnnotateWindow: share busy=" << busy << " btnW=" << m_shareBtn->width()
            << " btnH=" << m_shareBtn->height();
}

void AnnotateWindow::setShareProgress(qint64 sent, qint64 total)
{
    if (!m_shareProgress) {
        qWarning() << "AnnotateWindow: setShareProgress missing bar sent=" << sent << " total=" << total;
        return;
    }
    if (total <= 0) {
        m_shareProgress->setRange(0, 0);
        qInfo() << "AnnotateWindow: share progress indeterminate sent=" << sent;
        return;
    }
    m_shareProgress->setRange(0, 1000);
    m_shareProgress->setValue(static_cast<int>((sent * 1000) / total));
    qInfo() << "AnnotateWindow: share progress sent=" << sent << " total=" << total
            << " permille=" << m_shareProgress->value();
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
        updateStrokeFillVisibility();
        return;
    }
    item->setSelected(true);
    if (m_view) {
        m_view->setFocus(Qt::MouseFocusReason);
        m_view->viewport()->update();
    }
    qInfo() << "AnnotateWindow: selected kind=" << static_cast<int>(annotateKind(item))
            << "selected=" << item->isSelected();
    updateStrokeFillVisibility();
}

// ─── Ariadne's Thread [AT-0114] ─────────────────────
// What: Prefer the text block currently in edit for color
// Why:  Slider must recolor the caret target, not only last square
// Date: 2026-08-26
// Related: [AT-0158] AnnotateWindow.cpp:setAnnotateColor, [AT-0051] AnnotateWindow.cpp
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

QGraphicsItem *AnnotateWindow::lastAnnotationItem() const
{
    if (!m_scene) {
        qWarning() << "AnnotateWindow: lastAnnotationItem no scene";
        return nullptr;
    }
    const auto items = m_scene->items(Qt::AscendingOrder);
    for (int i = items.size() - 1; i >= 0; --i) {
        QGraphicsItem *item = items.at(i);
        if (item == m_photo || item == m_background || item->parentItem()) {
            continue;
        }
        const AnnotateKind kind = annotateKind(item);
        if (kind == AnnotateKind::None) {
            continue;
        }
        qInfo() << "AnnotateWindow: last annotation kind=" << static_cast<int>(kind)
                << "draft=" << (item == m_draft);
        return item;
    }
    qInfo() << "AnnotateWindow: last annotation none";
    return nullptr;
}

bool AnnotateWindow::isSquareOrStepsItem(const QGraphicsItem *item) const
{
    if (!item) {
        return false;
    }
    if (annotateKind(item) != AnnotateKind::Highlight) {
        qInfo() << "AnnotateWindow: isSquareOrStepsItem no kind=" << static_cast<int>(annotateKind(item));
        return false;
    }
    const HighlightStyle style = highlightStyle(item);
    const bool ok = (style == HighlightStyle::Fill || style == HighlightStyle::Steps
                     || style == HighlightStyle::Border || style == HighlightStyle::FillText);
    qInfo() << "AnnotateWindow: isSquareOrStepsItem" << ok << "style=" << static_cast<int>(style);
    return ok;
}

// ─── Ariadne's Thread [AT-0157] ─────────────────────
// What: Show Stroke and Fill only for the selected or last Square/Steps item
// Why:  Sliders stayed on the bar for Text, Arrow, and empty frames
// Date: 2026-08-26
// Related: [AT-0155] AnnotateWindow.cpp:applyCanvasChrome, [AT-0108] AnnotateWindow.cpp:setToolHighlight
// ─────────────────────────────────────────────────────
void AnnotateWindow::updateStrokeFillVisibility()
{
    QGraphicsItem *item = selectedAnnotation();
    if (!item) {
        item = lastAnnotationItem();
    }
    const bool on = isSquareOrStepsItem(item);
    if (m_strokeLabelAction) {
        m_strokeLabelAction->setVisible(on);
    }
    if (m_strokeAction) {
        m_strokeAction->setVisible(on);
    }
    if (m_fillLabelAction) {
        m_fillLabelAction->setVisible(on);
    }
    if (m_fillAction) {
        m_fillAction->setVisible(on);
    }
    if (m_strokeLabel) {
        m_strokeLabel->setVisible(on);
        m_strokeLabel->setEnabled(on);
    }
    if (m_stroke) {
        m_stroke->setVisible(on);
        m_stroke->setEnabled(on);
    }
    if (m_fillLabel) {
        m_fillLabel->setVisible(on);
        m_fillLabel->setEnabled(on);
    }
    if (m_fill) {
        m_fill->setVisible(on);
        m_fill->setEnabled(on);
    }
    qInfo() << "AnnotateWindow: Stroke/Fill visible=" << on
            << "kind=" << (item ? static_cast<int>(annotateKind(item)) : -1);
    updateTextSizeVisibility();
    updateBlurSliderVisibility();
}

// ─── Ariadne's Thread [AT-0331] ─────────────────────
// What: Show the Blur slider only while a blur box is selected
// Why:  Radius belongs to the selected blur square, not the idle toolbar
// Date: 2026-08-28
// Related: [AT-0330] AnnotateWindow.cpp:m_blurSlider, [AT-0157] AnnotateWindow.cpp:updateStrokeFillVisibility
// ─────────────────────────────────────────────────────
void AnnotateWindow::updateBlurSliderVisibility()
{
    QGraphicsItem *item = selectedAnnotation();
    const bool on = item && annotateKind(item) == AnnotateKind::Blur;
    qInfo() << "AnnotateWindow: updateBlurSliderVisibility on=" << on
            << "kind=" << (item ? static_cast<int>(annotateKind(item)) : -1);
    if (m_blurSliderAction) {
        m_blurSliderAction->setVisible(on);
    }
    if (!m_blurSlider) {
        qWarning() << "AnnotateWindow: updateBlurSliderVisibility missing slider on=" << on;
        layoutToolsBar();
        return;
    }
    m_blurSlider->setVisible(on);
    m_blurSlider->setEnabled(on);
    if (on) {
        const int radius = blurRadius(item);
        QSignalBlocker block(m_blurSlider);
        m_blurSlider->setValue(radius);
        m_blurRadius = radius;
        qInfo() << "AnnotateWindow: blur slider sync radius=" << radius;
    }
    layoutToolsBar();
    qInfo() << "AnnotateWindow: blur slider visible=" << on << "action=" << (m_blurSliderAction != nullptr);
}

// ─── Ariadne's Thread [AT-0396] ─────────────────────
// What: Blur sidebar with Automatic revealing type checkboxes
// Why:  Click Blur opens settings; Automatic persists via LocalStore
// Date: 2026-09-03
// Related: [AT-0390] app→LocalStore.cpp:blurAutomatic, [AT-0397] AnnotateWindow.cpp:commitBlurRect
// ─────────────────────────────────────────────────────
void AnnotateWindow::ensureBlurSidebar()
{
    if (m_blurSidebar) {
        qInfo() << "AnnotateWindow: blur sidebar already created";
        return;
    }
    m_blurSidebar = new QFrame(this);
    m_blurSidebar->setObjectName(QStringLiteral("BlurSidebar"));
    m_blurSidebar->setAttribute(Qt::WA_StyledBackground, true);
    m_blurSidebar->setFixedWidth(240);
    m_blurSidebar->setMinimumWidth(240);
    m_blurSidebar->setMaximumWidth(240);
    m_blurSidebar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_blurSidebar->setFrameShape(QFrame::NoFrame);
    m_blurSidebar->setAutoFillBackground(true);
    m_blurSidebar->setStyleSheet(QStringLiteral(
        "#BlurSidebar { background: #2c2c2e; border-left: 1px solid #6e6e73; }"
        "#BlurSidebar QLabel { color: #f5f5f7; font-weight: 600; }"
        "#BlurSidebar QCheckBox { color: #f5f5f7; spacing: 8px; }"));
    auto *lay = new QVBoxLayout(m_blurSidebar);
    lay->setContentsMargins(16, 18, 16, 18);
    lay->setSpacing(10);
    auto *title = new QLabel(QStringLiteral("Blur"), m_blurSidebar);
    m_blurAutomaticBox = new QCheckBox(QStringLiteral("Automatic"), m_blurSidebar);
    m_blurAutomaticBox->setFocusPolicy(Qt::NoFocus);
    m_blurTypeGroup = new QWidget(m_blurSidebar);
    auto *typeLay = new QVBoxLayout(m_blurTypeGroup);
    typeLay->setContentsMargins(16, 0, 0, 0);
    typeLay->setSpacing(8);
    m_blurFacesBox = new QCheckBox(QStringLiteral("Faces"), m_blurTypeGroup);
    m_blurPhonesBox = new QCheckBox(QStringLiteral("Phone numbers"), m_blurTypeGroup);
    m_blurEmailsBox = new QCheckBox(QStringLiteral("Emails"), m_blurTypeGroup);
    m_blurApiKeysBox = new QCheckBox(QStringLiteral("API keys"), m_blurTypeGroup);
    m_blurFacesBox->setFocusPolicy(Qt::NoFocus);
    m_blurPhonesBox->setFocusPolicy(Qt::NoFocus);
    m_blurEmailsBox->setFocusPolicy(Qt::NoFocus);
    m_blurApiKeysBox->setFocusPolicy(Qt::NoFocus);
    typeLay->addWidget(m_blurFacesBox);
    typeLay->addWidget(m_blurPhonesBox);
    typeLay->addWidget(m_blurEmailsBox);
    typeLay->addWidget(m_blurApiKeysBox);
    lay->addWidget(title);
    lay->addWidget(m_blurAutomaticBox);
    lay->addWidget(m_blurTypeGroup);
    lay->addStretch(1);

    {
        QSignalBlocker blockAuto(m_blurAutomaticBox);
        QSignalBlocker blockFaces(m_blurFacesBox);
        QSignalBlocker blockPhones(m_blurPhonesBox);
        QSignalBlocker blockEmails(m_blurEmailsBox);
        QSignalBlocker blockKeys(m_blurApiKeysBox);
        m_blurAutomaticBox->setChecked(LocalStore::blurAutomatic());
        m_blurFacesBox->setChecked(LocalStore::blurAutoFaces());
        m_blurPhonesBox->setChecked(LocalStore::blurAutoPhones());
        m_blurEmailsBox->setChecked(LocalStore::blurAutoEmails());
        m_blurApiKeysBox->setChecked(LocalStore::blurAutoApiKeys());
    }
    connect(m_blurAutomaticBox, &QCheckBox::toggled, this, &AnnotateWindow::onBlurAutomaticToggled);
    connect(m_blurFacesBox, &QCheckBox::toggled, this, &AnnotateWindow::onBlurFacesToggled);
    connect(m_blurPhonesBox, &QCheckBox::toggled, this, &AnnotateWindow::onBlurPhonesToggled);
    connect(m_blurEmailsBox, &QCheckBox::toggled, this, &AnnotateWindow::onBlurEmailsToggled);
    connect(m_blurApiKeysBox, &QCheckBox::toggled, this, &AnnotateWindow::onBlurApiKeysToggled);
    updateBlurTypeVisibility();
    qInfo() << "AnnotateWindow: blur sidebar created automatic=" << m_blurAutomaticBox->isChecked()
            << "faces=" << m_blurFacesBox->isChecked() << "phones=" << m_blurPhonesBox->isChecked()
            << "emails=" << m_blurEmailsBox->isChecked() << "apiKeys=" << m_blurApiKeysBox->isChecked();
}

void AnnotateWindow::updateBlurTypeVisibility()
{
    const bool on = m_blurAutomaticBox && m_blurAutomaticBox->isChecked();
    if (m_blurTypeGroup) {
        m_blurTypeGroup->setVisible(on);
    }
    qInfo() << "AnnotateWindow: blur type checkboxes visible=" << on;
}

void AnnotateWindow::setBlurSidebarVisible(bool on)
{
    if (!m_blurSidebar) {
        qWarning() << "AnnotateWindow: setBlurSidebarVisible missing sidebar on=" << on;
        return;
    }
    qInfo() << "AnnotateWindow: setBlurSidebarVisible on=" << on << "hidden=" << m_blurSidebar->isHidden()
            << "geo=" << m_blurSidebar->geometry()
            << "viewGeo=" << (m_view ? m_view->geometry() : QRect())
            << "body=" << (m_editorBody ? m_editorBody->size() : QSize());
    m_blurSidebar->setVisible(on);
    if (m_editorBody && m_editorBody->layout()) {
        m_editorBody->layout()->activate();
        qInfo() << "AnnotateWindow: blur sidebar layout activated on=" << on
                << "sidebarGeo=" << m_blurSidebar->geometry()
                << "viewGeo=" << (m_view ? m_view->geometry() : QRect())
                << "body=" << m_editorBody->size();
    }
    relayoutEditorChrome();
}

void AnnotateWindow::relayoutEditorChrome()
{
    fitShotToWindow();
    layoutPhotoOverlay();
    layoutUpdateCard();
    layoutToolsBar();
    layoutPhotoChoice();
    qInfo() << "AnnotateWindow: relayout editor chrome view=" << (m_view ? m_view->size() : QSize());
}

void AnnotateWindow::onBlurAutomaticToggled(bool on)
{
    LocalStore::setBlurAutomatic(on);
    qInfo() << "AnnotateWindow: Automatic toggled=" << on;
    updateBlurTypeVisibility();
    if (on) {
        maybeStartAutoRedact();
    } else {
        removeAllAutoBlur();
    }
}

void AnnotateWindow::onBlurFacesToggled(bool on)
{
    onBlurTypeToggled(SensitiveKind::Face, on);
}

void AnnotateWindow::onBlurPhonesToggled(bool on)
{
    onBlurTypeToggled(SensitiveKind::Phone, on);
}

void AnnotateWindow::onBlurEmailsToggled(bool on)
{
    onBlurTypeToggled(SensitiveKind::Email, on);
}

void AnnotateWindow::onBlurApiKeysToggled(bool on)
{
    onBlurTypeToggled(SensitiveKind::ApiKey, on);
}

void AnnotateWindow::onBlurTypeToggled(SensitiveKind kind, bool on)
{
    if (kind == SensitiveKind::Face) {
        LocalStore::setBlurAutoFaces(on);
    } else if (kind == SensitiveKind::Phone) {
        LocalStore::setBlurAutoPhones(on);
    } else if (kind == SensitiveKind::Email) {
        LocalStore::setBlurAutoEmails(on);
    } else if (kind == SensitiveKind::ApiKey) {
        LocalStore::setBlurAutoApiKeys(on);
    } else {
        qWarning() << "AnnotateWindow: onBlurTypeToggled unknown kind=" << static_cast<int>(kind);
        return;
    }
    qInfo() << "AnnotateWindow: blur type kind=" << static_cast<int>(kind) << "on=" << on
            << "automatic=" << LocalStore::blurAutomatic();
    if (!LocalStore::blurAutomatic()) {
        qInfo() << "AnnotateWindow: type toggle ignored, Automatic off";
        return;
    }
    if (on) {
        ensureSensitiveDetect();
    } else {
        removeAutoBlurOfKind(kind);
    }
}

bool AnnotateWindow::autoBlurKindEnabled(SensitiveKind kind) const
{
    if (!LocalStore::blurAutomatic()) {
        return false;
    }
    if (kind == SensitiveKind::Face) {
        return LocalStore::blurAutoFaces();
    }
    if (kind == SensitiveKind::Phone) {
        return LocalStore::blurAutoPhones();
    }
    if (kind == SensitiveKind::Email) {
        return LocalStore::blurAutoEmails();
    }
    if (kind == SensitiveKind::ApiKey) {
        return LocalStore::blurAutoApiKeys();
    }
    return false;
}

int AnnotateWindow::enabledSensitiveMask() const
{
    int mask = 0;
    if (!LocalStore::blurAutomatic()) {
        qInfo() << "AnnotateWindow: enabledSensitiveMask automatic off";
        return 0;
    }
    if (LocalStore::blurAutoFaces()) {
        mask |= kSensitiveKindFace;
    }
    if (LocalStore::blurAutoPhones()) {
        mask |= kSensitiveKindPhone;
    }
    if (LocalStore::blurAutoEmails()) {
        mask |= kSensitiveKindEmail;
    }
    if (LocalStore::blurAutoApiKeys()) {
        mask |= kSensitiveKindApiKey;
    }
    qInfo() << "AnnotateWindow: enabledSensitiveMask=" << mask;
    return mask;
}

bool AnnotateWindow::blurOverlapsExisting(const QRect &rect) const
{
    if (!m_scene || rect.isEmpty()) {
        return false;
    }
    const QRectF candidate(rect);
    const qreal candArea = candidate.width() * candidate.height();
    if (candArea <= 0) {
        return false;
    }
    const auto items = m_scene->items(Qt::AscendingOrder);
    for (QGraphicsItem *item : items) {
        if (!item || annotateKind(item) != AnnotateKind::Blur) {
            continue;
        }
        const QRectF existing = itemSceneBox(item);
        const QRectF inter = candidate.intersected(existing);
        if (inter.isEmpty()) {
            continue;
        }
        const qreal unionArea = candArea + existing.width() * existing.height() - inter.width() * inter.height();
        if (unionArea <= 0) {
            continue;
        }
        const qreal iou = (inter.width() * inter.height()) / unionArea;
        qInfo() << "AnnotateWindow: blur IoU=" << iou << "candidate=" << rect << "existing=" << existing;
        if (iou > 0.5) {
            return true;
        }
    }
    return false;
}

// ─── Ariadne's Thread [AT-0397] ─────────────────────
// What: Shared blur commit for drag and auto-redact boxes
// Why:  Auto Blur must reuse the existing Blur item path
// Date: 2026-09-03
// Related: [AT-0393] AnnotateItems.cpp:setBlurRedactKind, [AT-0011] AnnotationCommands.h:AddItemCommand
// ─────────────────────────────────────────────────────
QGraphicsItem *AnnotateWindow::commitBlurRect(const QRect &clipped, int radius, SensitiveKind redactKind,
                                              bool pushUndo)
{
    const QRect shot = shotRect().toRect();
    const QRect box = clipped.intersected(shot);
    if (box.width() < 1 || box.height() < 1) {
        qInfo() << "AnnotateWindow: commitBlurRect skip empty" << clipped << "shot=" << shot
                << "kind=" << static_cast<int>(redactKind);
        return nullptr;
    }
    const QImage source = shotImage().copy(box.translated(-shot.topLeft()));
    const QImage patch = boxBlur(source, radius);
    if (patch.isNull()) {
        qWarning() << "AnnotateWindow: commitBlurRect blur failed" << box << "radius=" << radius;
        return nullptr;
    }
    auto *pix = new QGraphicsPixmapItem(QPixmap::fromImage(patch));
    pix->setPos(box.topLeft());
    setAnnotateKind(pix, AnnotateKind::Blur);
    setBlurSource(pix, source);
    pix->setData(kAnnotateRoleBlurRadius, radius);
    setBlurRedactKind(pix, static_cast<int>(redactKind));
    if (pushUndo && m_undo) {
        m_undo->push(new AddItemCommand(m_scene, pix));
    } else if (m_scene) {
        m_scene->addItem(pix);
    }
    qInfo() << "AnnotateWindow: commitBlurRect radius=" << radius << box
            << "kind=" << static_cast<int>(redactKind) << "pushUndo=" << pushUndo;
    if (redactKind == SensitiveKind::None) {
        selectAnnotation(pix);
        qInfo() << "AnnotateWindow: committed blur selected=" << pix->isSelected();
    }
    return pix;
}

void AnnotateWindow::maybeStartAutoRedact()
{
    if (m_redactSessionRestored) {
        qInfo() << "AnnotateWindow: maybeStartAutoRedact skip restored session";
        return;
    }
    if (!LocalStore::blurAutomatic()) {
        qInfo() << "AnnotateWindow: maybeStartAutoRedact Automatic off";
        return;
    }
    if (enabledSensitiveMask() == 0) {
        qInfo() << "AnnotateWindow: maybeStartAutoRedact no types enabled";
        return;
    }
    ensureSensitiveDetect();
}

void AnnotateWindow::ensureSensitiveDetect()
{
    if (m_redactCacheReady) {
        qInfo() << "AnnotateWindow: sensitive cache ready hits=" << m_redactHits.size();
        applyEnabledAutoBlur();
        return;
    }
    if (m_redactBusy) {
        qInfo() << "AnnotateWindow: sensitive detect already running generation=" << m_redactGeneration;
        return;
    }
    startSensitiveDetect();
}

void AnnotateWindow::startSensitiveDetect()
{
    if (m_source.isNull()) {
        qWarning() << "AnnotateWindow: startSensitiveDetect empty source";
        return;
    }
    const int mask = kSensitiveKindAll;
    const int gen = ++m_redactGeneration;
    m_redactBusy = true;
    const QImage image = m_source;
    QPointer<AnnotateWindow> self = this;
    qInfo() << "AnnotateWindow: startSensitiveDetect generation=" << gen << "size=" << image.size()
            << "mask=" << mask;
    QThread *thread = QThread::create([image, gen, mask, self]() {
        qInfo() << "AnnotateWindow: sensitive worker start generation=" << gen << "size=" << image.size();
        QString error;
        const QList<SensitiveHit> hits = detectSensitive(image, mask, &error);
        qInfo() << "AnnotateWindow: sensitive worker done generation=" << gen << "hits=" << hits.size()
                << "error=" << error;
        QMetaObject::invokeMethod(qApp, [self, gen, hits, error]() {
            if (!self) {
                qInfo() << "AnnotateWindow: auto redact drop, window gone generation=" << gen;
                return;
            }
            self->onAutoRedactFinished(gen, hits, error);
        }, Qt::QueuedConnection);
    });
    if (!thread) {
        m_redactBusy = false;
        qWarning() << "AnnotateWindow: QThread::create failed generation=" << gen;
        return;
    }
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    qInfo() << "AnnotateWindow: sensitive thread started generation=" << gen;
}

void AnnotateWindow::onAutoRedactFinished(int generation, const QList<SensitiveHit> &hits, const QString &error)
{
    qInfo() << "AnnotateWindow: onAutoRedactFinished generation=" << generation
            << "current=" << m_redactGeneration << "hits=" << hits.size() << "error=" << error;
    if (generation != m_redactGeneration) {
        qInfo() << "AnnotateWindow: drop stale auto redact generation=" << generation;
        return;
    }
    m_redactBusy = false;
    if (!error.isEmpty()) {
        qWarning() << "AnnotateWindow: auto redact error=" << error;
        statusBar()->showMessage(QStringLiteral("Could not scan this screenshot."), 4000);
    }
    m_redactHits = hits;
    m_redactCacheReady = true;
    applyEnabledAutoBlur();
}

void AnnotateWindow::applyEnabledAutoBlur()
{
    if (!m_undo || !m_scene) {
        qWarning() << "AnnotateWindow: applyEnabledAutoBlur missing undo/scene";
        return;
    }
    if (!LocalStore::blurAutomatic()) {
        qInfo() << "AnnotateWindow: applyEnabledAutoBlur Automatic off";
        return;
    }
    int added = 0;
    QList<SensitiveHit> pending;
    for (const SensitiveHit &hit : m_redactHits) {
        if (!autoBlurKindEnabled(hit.kind)) {
            continue;
        }
        if (blurOverlapsExisting(hit.rect)) {
            qInfo() << "AnnotateWindow: skip overlapping auto blur" << hit.rect
                    << "kind=" << static_cast<int>(hit.kind);
            continue;
        }
        pending.append(hit);
    }
    if (pending.isEmpty()) {
        qInfo() << "AnnotateWindow: applyEnabledAutoBlur nothing to add hits=" << m_redactHits.size();
        return;
    }
    m_undo->beginMacro(QStringLiteral("Auto blur"));
    for (const SensitiveHit &hit : pending) {
        if (blurOverlapsExisting(hit.rect)) {
            qInfo() << "AnnotateWindow: skip overlapping auto blur after pending" << hit.rect
                    << "kind=" << static_cast<int>(hit.kind);
            continue;
        }
        if (commitBlurRect(hit.rect, m_blurRadius, hit.kind, true)) {
            ++added;
        }
    }
    m_undo->endMacro();
    qInfo() << "AnnotateWindow: applyEnabledAutoBlur added=" << added << "hits=" << m_redactHits.size();
}

void AnnotateWindow::removeAutoBlurOfKind(SensitiveKind kind)
{
    if (!m_scene || !m_undo) {
        qWarning() << "AnnotateWindow: removeAutoBlurOfKind missing scene/undo kind="
                   << static_cast<int>(kind);
        return;
    }
    QList<QGraphicsItem *> doomed;
    const auto items = m_scene->items(Qt::AscendingOrder);
    for (QGraphicsItem *item : items) {
        if (!item || item->parentItem() || annotateKind(item) != AnnotateKind::Blur) {
            continue;
        }
        if (blurRedactKind(item) != static_cast<int>(kind)) {
            continue;
        }
        doomed.append(item);
    }
    qInfo() << "AnnotateWindow: removeAutoBlurOfKind kind=" << static_cast<int>(kind)
            << "count=" << doomed.size();
    if (doomed.isEmpty()) {
        return;
    }
    m_undo->beginMacro(QStringLiteral("Remove auto blur"));
    for (QGraphicsItem *item : doomed) {
        m_undo->push(new RemoveItemCommand(m_scene, item));
    }
    m_undo->endMacro();
}

void AnnotateWindow::removeAllAutoBlur()
{
    qInfo() << "AnnotateWindow: removeAllAutoBlur";
    if (!m_scene || !m_undo) {
        qWarning() << "AnnotateWindow: removeAllAutoBlur missing scene/undo";
        return;
    }
    QList<QGraphicsItem *> doomed;
    const auto items = m_scene->items(Qt::AscendingOrder);
    for (QGraphicsItem *item : items) {
        if (!item || item->parentItem() || annotateKind(item) != AnnotateKind::Blur) {
            continue;
        }
        if (blurRedactKind(item) == static_cast<int>(SensitiveKind::None)) {
            continue;
        }
        doomed.append(item);
    }
    qInfo() << "AnnotateWindow: removeAllAutoBlur count=" << doomed.size();
    if (doomed.isEmpty()) {
        return;
    }
    m_undo->beginMacro(QStringLiteral("Remove auto blur"));
    for (QGraphicsItem *item : doomed) {
        m_undo->push(new RemoveItemCommand(m_scene, item));
    }
    m_undo->endMacro();
}

// ─── Ariadne's Thread [AT-0334] ─────────────────────
// What: Show Size and Outline next to Text only while a text block is selected or editing
// Why:  Idle toolbar Size/Outline were not tied to a text selection
// Date: 2026-08-28
// Related: [AT-0345] AnnotateWindow.cpp:m_textGroup, [AT-0344] AnnotateWindow.cpp:m_textOutlineBox
// ─────────────────────────────────────────────────────
void AnnotateWindow::updateTextSizeVisibility()
{
    QGraphicsItem *item = selectedAnnotation();
    AnnotateTextItem *caption = nullptr;
    if (item && annotateKind(item) == AnnotateKind::Highlight
        && highlightStyle(item) == HighlightStyle::FillText) {
        for (QGraphicsItem *child : item->childItems()) {
            caption = qgraphicsitem_cast<AnnotateTextItem *>(child);
            if (caption) {
                break;
            }
        }
    }
    const bool on = (m_editingText != nullptr)
                    || (item && annotateKind(item) == AnnotateKind::Text)
                    || (caption != nullptr);
    qInfo() << "AnnotateWindow: updateTextSizeVisibility on=" << on
            << "editing=" << (m_editingText != nullptr)
            << "kind=" << (item ? static_cast<int>(annotateKind(item)) : -1)
            << "fillTextCaption=" << (caption != nullptr)
            << "group=" << (m_textGroup != nullptr);
    if (m_textSizeLabel) {
        m_textSizeLabel->setVisible(on);
        m_textSizeLabel->setEnabled(on);
    }
    if (m_textSizeSlider) {
        m_textSizeSlider->setVisible(on);
        m_textSizeSlider->setEnabled(on);
    } else {
        qWarning() << "AnnotateWindow: updateTextSizeVisibility missing slider on=" << on;
    }
    if (m_textOutlineBox) {
        m_textOutlineBox->setVisible(on);
        m_textOutlineBox->setEnabled(on);
        qInfo() << "AnnotateWindow: Outline visible=" << on << "checked=" << m_textOutlineBox->isChecked();
    } else {
        qWarning() << "AnnotateWindow: updateTextSizeVisibility missing Outline box on=" << on;
    }
    if (m_textGroup) {
        if (auto *lay = m_textGroup->layout()) {
            const int padLeft = on ? 6 : 0;
            const int padRight = on ? 14 : 0;
            lay->setContentsMargins(padLeft, 0, padRight, 0);
            qInfo() << "AnnotateWindow: Text group margins left=" << padLeft << "right=" << padRight;
        }
        if (on) {
            // ─── Ariadne's Thread [AT-0353] ─────────────────────
            // What: Extra right padding after the Outline checkbox label
            // Why:  QCheckBox padding 0 plus 6px group margin left the label flush with the pad
            // Date: 2026-08-28
            // Related: [AT-0346] AnnotateWindow.cpp:updateTextSizeVisibility, [AT-0345] AnnotateWindow.cpp:m_textGroup
            // ─────────────────────────────────────────────────────
            m_textGroup->setStyleSheet(QStringLiteral(
                "QFrame#TextGroup { background: #3a3a3c; border: none; border-radius: 6px; }"
                "QFrame#TextGroup QToolButton, QFrame#TextGroup QToolButton:hover,"
                "QFrame#TextGroup QToolButton:checked {"
                " background: transparent; border: none; padding: 0px; margin: 0px; }"
                "QFrame#TextGroup QCheckBox { padding: 0px 8px 0px 0px; margin: 0px; }"));
        } else {
            m_textGroup->setStyleSheet(QStringLiteral(
                "QFrame#TextGroup { background: transparent; border: none; }"));
        }
        qInfo() << "AnnotateWindow: Text group chrome=" << on << "sizeHint=" << m_textGroup->sizeHint();
    } else {
        qWarning() << "AnnotateWindow: updateTextSizeVisibility missing Text group on=" << on;
    }
    if (!on) {
        qInfo() << "AnnotateWindow: Size and Outline hidden";
        return;
    }
    QGraphicsItem *sized = m_editingText;
    if (!sized && item && annotateKind(item) == AnnotateKind::Text) {
        sized = item;
    }
    if (!sized) {
        sized = caption;
    }
    if (!sized) {
        qWarning() << "AnnotateWindow: Size/Outline visible but no sized item";
        return;
    }
    const int size = annotateTextSize(sized);
    const bool outline = annotateTextOutline(sized);
    if (m_textSizeSlider) {
        QSignalBlocker blockSize(m_textSizeSlider);
        m_textSizeSlider->setValue(size);
        m_textSize = size;
    }
    if (m_textOutlineBox) {
        QSignalBlocker blockOutline(m_textOutlineBox);
        m_textOutlineBox->setChecked(outline);
        m_textOutline = outline;
    }
    qInfo() << "AnnotateWindow: Size/Outline sync size=" << size << "outline=" << outline;
}

QGraphicsItem *AnnotateWindow::lastTextSizedItem() const
{
    if (m_editingText) {
        qInfo() << "AnnotateWindow: last text-sized is editing text draft=" << m_textDraft;
        return m_editingText;
    }
    if (QGraphicsItem *selected = selectedAnnotation()) {
        const AnnotateKind kind = annotateKind(selected);
        if (kind == AnnotateKind::Text) {
            qInfo() << "AnnotateWindow: last text-sized is selected text";
            return selected;
        }
        if (kind == AnnotateKind::Highlight && highlightStyle(selected) == HighlightStyle::FillText) {
            for (QGraphicsItem *child : selected->childItems()) {
                if (auto *caption = qgraphicsitem_cast<AnnotateTextItem *>(child)) {
                    qInfo() << "AnnotateWindow: last text-sized is selected fill+text caption";
                    return caption;
                }
            }
        }
        if (kind == AnnotateKind::Highlight && highlightStyle(selected) == HighlightStyle::Steps) {
            qInfo() << "AnnotateWindow: last text-sized is selected steps";
            return selected;
        }
    }
    const auto items = m_scene->items(Qt::AscendingOrder);
    for (int i = items.size() - 1; i >= 0; --i) {
        QGraphicsItem *item = items.at(i);
        if (item == m_photo || item == m_draft || item == m_background || item->parentItem()) {
            continue;
        }
        const AnnotateKind kind = annotateKind(item);
        if (kind == AnnotateKind::Text) {
            qInfo() << "AnnotateWindow: last text-sized is text";
            return item;
        }
        if (kind == AnnotateKind::Highlight && highlightStyle(item) == HighlightStyle::Steps) {
            qInfo() << "AnnotateWindow: last text-sized is steps";
            return item;
        }
    }
    qInfo() << "AnnotateWindow: last text-sized none";
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
// ─── Ariadne's Thread [AT-0155] ─────────────────────
// What: Show Radius and Shadow only when a Background preset is on
// Why:  Disabled sliders still occupied the bar while Background was None
// Date: 2026-08-26
// Related: [AT-0056] AnnotateWindow.cpp:applyCanvasChrome, [AT-0073] AnnotateWindow.cpp:m_bgButton
// ─────────────────────────────────────────────────────
void AnnotateWindow::applyCanvasChrome()
{
    if (!m_photo || !m_background || !m_photoShadow) {
        qWarning() << "AnnotateWindow: applyCanvasChrome missing photo/backdrop";
        return;
    }
    const bool on = hasBackground();
    if (m_radiusLabelAction) {
        m_radiusLabelAction->setVisible(on);
    }
    if (m_radiusAction) {
        m_radiusAction->setVisible(on);
    }
    if (m_shadowLabelAction) {
        m_shadowLabelAction->setVisible(on);
    }
    if (m_shadowAction) {
        m_shadowAction->setVisible(on);
    }
    if (m_radius) {
        m_radius->setVisible(on);
        m_radius->setEnabled(on);
    }
    if (m_shadow) {
        m_shadow->setVisible(on);
        m_shadow->setEnabled(on);
    }
    if (m_radiusLabel) {
        m_radiusLabel->setVisible(on);
        m_radiusLabel->setEnabled(on);
    }
    if (m_shadowLabel) {
        m_shadowLabel->setVisible(on);
        m_shadowLabel->setEnabled(on);
    }
    layoutToolsBar();
    qInfo() << "AnnotateWindow: chrome sliders visible=" << on << "preset=" << m_bgPreset;
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

// ─── Ariadne's Thread [AT-0332] ─────────────────────
// What: Gray toolbar buttons are white with black text on the dark annotate bar
// Why:  Native QPushButton followed Light/Dark system text on a always-dark HUD
// Date: 2026-08-28
// Related: [AT-0071] AnnotateWindow.cpp:makeNativeToolbarButton, [AT-0149] AnnotateWindow.cpp:m_toolsBar
// ─────────────────────────────────────────────────────
void AnnotateWindow::applyAnnotateChromeTheme()
{
    const Qt::ColorScheme scheme =
        QGuiApplication::styleHints() ? QGuiApplication::styleHints()->colorScheme() : Qt::ColorScheme::Unknown;
    qInfo() << "AnnotateWindow: applyAnnotateChromeTheme scheme=" << static_cast<int>(scheme)
            << "grayButtons=whiteBlack";
    styleGrayToolbarButton(m_saveBtn, true);
    styleGrayToolbarButton(m_photoPictureButton, true);
    styleGrayToolbarButton(m_photoTimer5Button, true);
    layoutToolsBar();
    qInfo() << "AnnotateWindow: gray buttons styled save=" << (m_saveBtn != nullptr)
            << "picture=" << (m_photoPictureButton != nullptr) << "timer=" << (m_photoTimer5Button != nullptr);
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
        showPhotoChoice();
        qInfo() << "AnnotateWindow: preview only, Photo choice card shown";
        return;
    }
    if (follow == PhotoFollow::Picture) {
        if (m_photoTimer) {
            m_photoTimer->stop();
        }
        if (m_photoCountdown) {
            m_photoCountdown->hide();
        }
        hidePhotoChoice();
        beginPhotoFlashCapture();
        return;
    }
    if (follow == PhotoFollow::Timer5) {
        m_photoCount = 5;
        if (m_photoCountdown) {
            m_photoCountdown->hide();
        }
        showPhotoChoiceCountdown(5);
        if (m_photoTimer) {
            m_photoTimer->start();
        }
        qInfo() << "AnnotateWindow: 5s timer started in Photo choice card";
    }
}

void AnnotateWindow::showPhotoChoice()
{
    if (!m_photoChoice || !m_photoPictureButton || !m_photoTimer5Button || !m_photoChoiceCount) {
        qWarning() << "AnnotateWindow: showPhotoChoice missing card";
        return;
    }
    if (photoCaptureBusy()) {
        qInfo() << "AnnotateWindow: showPhotoChoice skipped, flash busy";
        return;
    }
    m_photoPictureButton->show();
    m_photoTimer5Button->show();
    m_photoChoiceCount->hide();
    m_photoChoice->show();
    layoutPhotoChoice();
    qInfo() << "AnnotateWindow: Photo choice card shown geo=" << m_photoChoice->geometry();
}

void AnnotateWindow::hidePhotoChoice()
{
    if (!m_photoChoice) {
        qWarning() << "AnnotateWindow: hidePhotoChoice missing card";
        return;
    }
    m_photoChoice->hide();
    qInfo() << "AnnotateWindow: Photo choice card hidden";
}

void AnnotateWindow::layoutPhotoChoice()
{
    if (!m_photoChoice || !m_photoChoice->isVisible()) {
        return;
    }
    m_photoChoice->adjustSize();
    const QSize hint = m_photoChoice->sizeHint();
    const int w = qMax(hint.width(), 160);
    const int h = qMax(hint.height(), 52);
    const int x = qMax(0, (width() - w) / 2);
    const int y = qMax(0, (height() - h) / 2);
    m_photoChoice->setGeometry(x, y, w, h);
    m_photoChoice->raise();
    qInfo() << "AnnotateWindow: Photo choice card geo=" << m_photoChoice->geometry() << "window=" << size();
}

void AnnotateWindow::showPhotoChoiceCountdown(int seconds)
{
    if (!m_photoChoice || !m_photoPictureButton || !m_photoTimer5Button || !m_photoChoiceCount) {
        qWarning() << "AnnotateWindow: showPhotoChoiceCountdown missing card seconds=" << seconds;
        return;
    }
    m_photoPictureButton->hide();
    m_photoTimer5Button->hide();
    m_photoChoiceCount->setText(QString::number(seconds));
    m_photoChoiceCount->show();
    m_photoChoice->show();
    layoutPhotoChoice();
    qInfo() << "AnnotateWindow: Photo choice countdown=" << seconds;
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
    hidePhotoChoice();
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
            m_photoCountdown->hide();
        }
        showPhotoChoiceCountdown(m_photoCount);
        return;
    }
    if (m_photoTimer) {
        m_photoTimer->stop();
    }
    if (m_photoCountdown) {
        m_photoCountdown->hide();
    }
    hidePhotoChoice();
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

// ─── Ariadne's Thread [AT-0203] ─────────────────────
// What: Stop camera/pip without QWindow::destroy while the editor is closing
// Why:  Destroying the Tool NSWindow in closeEvent SIGSEGV in setNeedsDisplayInRect
// Date: 2026-08-27
// Related: [AT-0080] AnnotateWindow.cpp:m_photoOverlay, [AT-0202] AnnotateWindow.cpp:closeEvent
// ─────────────────────────────────────────────────────
void AnnotateWindow::abortPhotoCycle(bool notifyEnded, bool destroyOverlayWindow)
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
        QWindow *win = m_photoOverlay->windowHandle();
        qInfo() << "AnnotateWindow: photo Tool hide destroyNative=" << destroyOverlayWindow
                << " hasQWindow=" << (win != nullptr) << " visible=" << m_photoOverlay->isVisible();
        if (destroyOverlayWindow && win) {
            win->destroy();
            qInfo() << "AnnotateWindow: photo Tool QWindow destroyed";
        }
    }
    if (m_photoFlash) {
        m_photoFlash->hide();
    }
    if (m_photoCountdown) {
        m_photoCountdown->hide();
    }
    hidePhotoChoice();
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
                << " notifyEnded=" << notifyEnded << " destroyOverlayWindow=" << destroyOverlayWindow;
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
    item->setTextSize(m_textSize);
    item->setTextOutline(m_textOutline);
    item->setTextWidth(qMin(item->textWidth(), maxTextWidthOnShot(item)));
    m_scene->addItem(item);
    qInfo() << "AnnotateWindow: new text draft at" << scenePos << "color=" << m_color
            << "width=" << item->textWidth() << "size=" << m_textSize << "outline=" << m_textOutline;
    beginEditText(item, true, scenePos);
}

// ─── Ariadne's Thread [AT-0116] ─────────────────────
// What: Select the text being edited
// Why:  lastColorableItem and chrome follow the caret block
// Date: 2026-08-26
// Related: [AT-0114] AnnotateWindow.cpp:lastColorableItem, [AT-0111] AnnotateWindow.cpp:textItemAt
// ─────────────────────────────────────────────────────
void AnnotateWindow::beginEditText(AnnotateTextItem *item, bool draft, const QPointF &scenePos)
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
    // ─── Ariadne's Thread [AT-0340] ─────────────────────
    // What: Activate the view, then place the QTextCursor before any leftover mousePress
    // Why:  Size slider show and QGraphicsView::mousePress on the shot stole scene focus
    // Date: 2026-08-28
    // Related: [AT-0333] AnnotateWindow.cpp:updateTextSizeVisibility, [AT-0116] AnnotateWindow.cpp:beginEditText
    // ─────────────────────────────────────────────────────
    selectAnnotation(item);
    if (m_view) {
        m_view->setFocus(Qt::MouseFocusReason);
        qInfo() << "AnnotateWindow: view focus after select hasFocus=" << m_view->hasFocus()
                << "sceneActive=" << (m_scene && m_scene->isActive())
                << "focusWidget="
                << (QApplication::focusWidget() ? QApplication::focusWidget()->metaObject()->className()
                                                : "null");
    }
    const QPointF itemPos = item->mapFromScene(scenePos);
    item->beginEdit(itemPos);
    if (m_scene) {
        m_scene->setFocus(Qt::MouseFocusReason);
        m_scene->setFocusItem(item, Qt::MouseFocusReason);
        qInfo() << "AnnotateWindow: scene focusItem=" << (m_scene->focusItem() == item)
                << "itemHasFocus=" << item->hasFocus() << "sceneActive=" << m_scene->isActive();
    }
    if (m_undoAction) {
        m_undoAction->setShortcut(QKeySequence());
    }
    if (m_redoAction) {
        m_redoAction->setShortcut(QKeySequence());
    }
    qInfo() << "AnnotateWindow: text edit begin draft=" << draft << "oldLen=" << m_editOldText.size()
            << "itemPos=" << itemPos << "cursor=" << item->textCursor().position();
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
    caption->setTextSize(m_textSize);
    caption->setTextOutline(m_textOutline);
    m_fillTextRect = rect;
    qInfo() << "AnnotateWindow: Fill+text caption start rect=" << box;
    beginEditText(caption, false, caption->mapToScene(QPointF(4, 4)));
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
    m_textViewMouse = false;
    m_fillTextRect = nullptr;
    if (m_view && m_view->viewport()) {
        m_view->viewport()->unsetCursor();
        qInfo() << "AnnotateWindow: text commit unset viewport cursor";
    }
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
            selectAnnotation(fillRect);
            return;
        }
        if (text != m_editOldText) {
            m_undo->push(new ChangeTextCommand(item, m_editOldText, text));
            qInfo() << "AnnotateWindow: Fill+text caption committed";
        }
        selectAnnotation(fillRect);
        return;
    }
    if (draft) {
        if (empty) {
            m_scene->removeItem(item);
            delete item;
            qInfo() << "AnnotateWindow: empty draft text discarded";
            updateStrokeFillVisibility();
            return;
        }
        m_scene->removeItem(item);
        m_undo->push(new AddItemCommand(m_scene, item));
        qInfo() << "AnnotateWindow: committed text add";
        updateStrokeFillVisibility();
        return;
    }
    if (empty) {
        m_undo->push(new RemoveItemCommand(m_scene, item));
        qInfo() << "AnnotateWindow: emptied text removed";
        updateStrokeFillVisibility();
        return;
    }
    if (text != m_editOldText) {
        m_undo->push(new ChangeTextCommand(item, m_editOldText, text));
        qInfo() << "AnnotateWindow: committed text edit";
    }
    updateStrokeFillVisibility();
}

// ─── Ariadne's Thread [AT-0072] ─────────────────────
// What: Select move/resize/delete on the current annotation
// Why:  Select only set ItemIsSelectable; Del/Backspace and grips were missing
// Date: 2026-08-25
// Related: [AT-0066] AnnotateWindow.cpp:setToolSelect, [AT-0011] RemoveItemCommand
// ─────────────────────────────────────────────────────
// ─── Ariadne's Thread [AT-0310] ─────────────────────
// What: Hit-test the painted Select handle square, including the outer half
// Why:  A 10px circle around the corner missed the square; annotationItemAt ignored clicks outside the item
// Date: 2026-08-28
// Related: [AT-0156] AnnotateWindow.cpp:paintSelectHandles, [AT-0153] AnnotateWindow.cpp:hitSelectHandle
// ─────────────────────────────────────────────────────
qreal AnnotateWindow::selectHandleHalfScene() const
{
    if (!m_view) {
        qWarning() << "AnnotateWindow: selectHandleHalfScene no view";
        return 4.0;
    }
    const qreal scale = m_view->transform().m11();
    const qreal r = (scale > 0) ? (4.0 / scale) : 4.0;
    return r;
}

bool AnnotateWindow::hitsScenePoint(const QPointF &target, const QPointF &scenePos) const
{
    const qreal r = selectHandleHalfScene();
    qreal pad = 1.0;
    if (m_view) {
        const qreal scale = m_view->transform().m11();
        if (scale > 0) {
            pad = 1.0 / scale;
        }
    }
    const QRectF box(target.x() - r - pad, target.y() - r - pad, (r + pad) * 2.0, (r + pad) * 2.0);
    const bool hit = box.contains(scenePos);
    qInfo() << "AnnotateWindow: hitsScenePoint" << hit << "target=" << target << "pos=" << scenePos
            << "box=" << box << "r=" << r << "pad=" << pad;
    return hit;
}

// ─── Ariadne's Thread [AT-0153] ─────────────────────
// What: Hit-test a box edge in view pixels, same 10px slop as corners
// Why:  Edge press fell through to Move and dragged the annotation
// Date: 2026-08-26
// Related: [AT-0066] AnnotateWindow.cpp:hitsScenePoint, [AT-0153] AnnotateWindow.cpp:hitSelectHandle
// ─────────────────────────────────────────────────────
bool AnnotateWindow::hitsSceneEdge(const QPointF &a, const QPointF &b, const QPointF &scenePos) const
{
    if (!m_view) {
        qWarning() << "AnnotateWindow: hitsSceneEdge no view";
        return false;
    }
    const QPointF va = m_view->mapFromScene(a);
    const QPointF vb = m_view->mapFromScene(b);
    const QPointF vp = m_view->mapFromScene(scenePos);
    const QLineF seg(va, vb);
    const qreal len = seg.length();
    if (len < 1.0) {
        const bool hit = hitsScenePoint(a, scenePos);
        qInfo() << "AnnotateWindow: hitsSceneEdge degenerate hit=" << hit << "a=" << a;
        return hit;
    }
    const qreal t = qBound(0.0, QPointF::dotProduct(vp - va, vb - va) / (len * len), 1.0);
    const QPointF proj = va + (vb - va) * t;
    const bool hit = QLineF(proj, vp).length() <= 10.0;
    qInfo() << "AnnotateWindow: hitsSceneEdge" << hit << "a=" << a << "b=" << b << "pos=" << scenePos
            << "t=" << t << "dist=" << QLineF(proj, vp).length();
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
    if (kind == AnnotateKind::Highlight || kind == AnnotateKind::Blur || kind == AnnotateKind::Text) {
        const QRectF box = itemSceneBox(item);
        qInfo() << "AnnotateWindow: hitSelectHandle visual box=" << box
                << "bounds=" << item->mapRectToScene(item->boundingRect()) << "pos=" << scenePos;
        if (hitsScenePoint(box.topLeft(), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle TopLeft box=" << box;
            return SelectHandle::TopLeft;
        }
        if (hitsScenePoint(box.topRight(), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle TopRight box=" << box;
            return SelectHandle::TopRight;
        }
        if (hitsScenePoint(box.bottomLeft(), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle BottomLeft box=" << box;
            return SelectHandle::BottomLeft;
        }
        if (hitsScenePoint(box.bottomRight(), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle BottomRight box=" << box;
            return SelectHandle::BottomRight;
        }
        if (hitsScenePoint(QPointF(box.center().x(), box.top()), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle Top box=" << box;
            return SelectHandle::Top;
        }
        if (hitsScenePoint(QPointF(box.center().x(), box.bottom()), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle Bottom box=" << box;
            return SelectHandle::Bottom;
        }
        if (hitsScenePoint(QPointF(box.left(), box.center().y()), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle Left box=" << box;
            return SelectHandle::Left;
        }
        if (hitsScenePoint(QPointF(box.right(), box.center().y()), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle Right box=" << box;
            return SelectHandle::Right;
        }
        if (hitsSceneEdge(box.topLeft(), box.topRight(), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle Top edge box=" << box;
            return SelectHandle::Top;
        }
        if (hitsSceneEdge(box.bottomLeft(), box.bottomRight(), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle Bottom edge box=" << box;
            return SelectHandle::Bottom;
        }
        if (hitsSceneEdge(box.topLeft(), box.bottomLeft(), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle Left edge box=" << box;
            return SelectHandle::Left;
        }
        if (hitsSceneEdge(box.topRight(), box.bottomRight(), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle Right edge box=" << box;
            return SelectHandle::Right;
        }
        qInfo() << "AnnotateWindow: hitSelectHandle none on box=" << box << "pos=" << scenePos;
        return SelectHandle::None;
    }
    if (kind == AnnotateKind::Photo) {
        const QRectF box = itemSceneBox(item);
        if (hitsScenePoint(box.topLeft(), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle photo TopLeft box=" << box;
            return SelectHandle::TopLeft;
        }
        if (hitsScenePoint(box.topRight(), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle photo TopRight box=" << box;
            return SelectHandle::TopRight;
        }
        if (hitsScenePoint(box.bottomLeft(), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle photo BottomLeft box=" << box;
            return SelectHandle::BottomLeft;
        }
        if (hitsScenePoint(box.bottomRight(), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle photo BottomRight box=" << box;
            return SelectHandle::BottomRight;
        }
        if (hitsScenePoint(QPointF(box.center().x(), box.top()), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle photo Top box=" << box;
            return SelectHandle::Top;
        }
        if (hitsScenePoint(QPointF(box.center().x(), box.bottom()), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle photo Bottom box=" << box;
            return SelectHandle::Bottom;
        }
        if (hitsScenePoint(QPointF(box.left(), box.center().y()), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle photo Left box=" << box;
            return SelectHandle::Left;
        }
        if (hitsScenePoint(QPointF(box.right(), box.center().y()), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle photo Right box=" << box;
            return SelectHandle::Right;
        }
        if (hitsSceneEdge(box.topLeft(), box.topRight(), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle photo Top edge box=" << box;
            return SelectHandle::Top;
        }
        if (hitsSceneEdge(box.bottomLeft(), box.bottomRight(), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle photo Bottom edge box=" << box;
            return SelectHandle::Bottom;
        }
        if (hitsSceneEdge(box.topLeft(), box.bottomLeft(), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle photo Left edge box=" << box;
            return SelectHandle::Left;
        }
        if (hitsSceneEdge(box.topRight(), box.bottomRight(), scenePos)) {
            qInfo() << "AnnotateWindow: hitSelectHandle photo Right edge box=" << box;
            return SelectHandle::Right;
        }
        qInfo() << "AnnotateWindow: hitSelectHandle photo none box=" << box << "pos=" << scenePos;
        return SelectHandle::None;
    }
    return SelectHandle::None;
}

bool AnnotateWindow::tryStartHandleGesture(QGraphicsItem *item, const QPointF &scenePos)
{
    if (!item) {
        qInfo() << "AnnotateWindow: tryStartHandleGesture null item pos=" << scenePos;
        return false;
    }
    const SelectHandle handle = hitSelectHandle(item, scenePos);
    qInfo() << "AnnotateWindow: tryStartHandleGesture kind=" << static_cast<int>(annotateKind(item))
            << "handle=" << static_cast<int>(handle) << "pos=" << scenePos;
    if (handle == SelectHandle::None || handle == SelectHandle::Move) {
        return false;
    }
    if (auto *photo = qgraphicsitem_cast<AnnotatePhotoItem *>(item)) {
        ++m_photoScaleGestureId;
        m_scalingPhoto = photo;
        m_photoScaleHandle = handle;
        m_photoOldScale = photo->photoScale();
        m_photoOldPos = photo->pos();
        m_resizeStart = scenePos;
        qInfo() << "AnnotateWindow: handle gesture photo scale=" << m_photoOldScale
                << "handle=" << static_cast<int>(handle) << "gesture=" << m_photoScaleGestureId;
        return true;
    }
    m_selectWasSelected = item->isSelected();
    selectAnnotation(item);
    m_selectItem = item;
    m_selectPressScene = scenePos;
    m_selectOldPos = item->pos();
    m_selectDidMove = false;
    m_selectHandle = handle;
    if (auto *rect = qgraphicsitem_cast<QGraphicsRectItem *>(item)) {
        m_selectOldRect = rect->rect();
    }
    m_selectOldP1 = annotateP1(item);
    m_selectOldP2 = annotateP2(item);
    if (auto *pix = qgraphicsitem_cast<QGraphicsPixmapItem *>(item)) {
        m_selectOldBlurPos = pix->pos();
        m_selectOldBlurSource = pix->data(kAnnotateRoleBlurSource).value<QImage>();
    }
    if (auto *text = qgraphicsitem_cast<AnnotateTextItem *>(item)) {
        m_selectOldWidth = text->textWidth();
        if (m_selectOldWidth < 0) {
            m_selectOldWidth = text->boundingRect().width();
        }
        m_selectOldHeight = text->boxHeight();
        m_selectOldRect = itemSceneBox(text);
        qInfo() << "AnnotateWindow: handle gesture text oldWidth=" << m_selectOldWidth
                << "oldHeight=" << m_selectOldHeight << "oldRect=" << m_selectOldRect
                << "handle=" << static_cast<int>(handle);
    }
    qInfo() << "AnnotateWindow: handle gesture start kind=" << static_cast<int>(annotateKind(item))
            << "handle=" << static_cast<int>(handle) << "wasSelected=" << m_selectWasSelected;
    return true;
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
    // ─── Ariadne's Thread [AT-0156] ─────────────────────
    // What: Select handles on a text block set wrap width and box height
    // Why:  Corners and bottom must stretch the box, not only Left/Right wrap
    // Date: 2026-08-28
    // Related: [AT-0348] AnnotateItems.cpp:setBoxHeight, [AT-0153] AnnotateWindow.cpp:hitSelectHandle
    // ─────────────────────────────────────────────────────
    if (kind == AnnotateKind::Text) {
        auto *text = qgraphicsitem_cast<AnnotateTextItem *>(m_selectItem);
        if (!text) {
            qWarning() << "AnnotateWindow: applySelectResize text not AnnotateTextItem";
            return;
        }
        QRectF box = m_selectOldRect;
        if (!box.isValid() || box.isEmpty()) {
            box = itemSceneBox(text);
            qWarning() << "AnnotateWindow: applySelectResize text empty oldRect, live=" << box;
        }
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
        case SelectHandle::Top:
            box.setTop(clamped.y());
            break;
        case SelectHandle::Bottom:
            box.setBottom(clamped.y());
            break;
        case SelectHandle::Left:
            box.setLeft(clamped.x());
            break;
        case SelectHandle::Right:
            box.setRight(clamped.x());
            break;
        default:
            qWarning() << "AnnotateWindow: applySelectResize bad text handle="
                       << static_cast<int>(m_selectHandle);
            return;
        }
        box = box.normalized();
        const QRectF shot = shotRect();
        const qreal minW = text->minTextWidth();
        if (box.width() < minW) {
            if (m_selectHandle == SelectHandle::Left || m_selectHandle == SelectHandle::TopLeft
                || m_selectHandle == SelectHandle::BottomLeft) {
                box.setLeft(box.right() - minW);
            } else {
                box.setWidth(minW);
            }
        }
        text->setTextWidth(qBound(minW, box.width(), qMax(minW, shot.right() - box.left())));
        const qreal minH = text->minTextHeight();
        if (box.height() < minH) {
            if (m_selectHandle == SelectHandle::Top || m_selectHandle == SelectHandle::TopLeft
                || m_selectHandle == SelectHandle::TopRight) {
                box.setTop(box.bottom() - minH);
            } else {
                box.setHeight(minH);
            }
        }
        box = box.intersected(shot);
        if (box.width() < minW || box.height() < minH) {
            qWarning() << "AnnotateWindow: applySelectResize text box too small after shot clip=" << box
                       << "minW=" << minW << "minH=" << minH;
            return;
        }
        text->setBoxHeight(box.height());
        const QRectF local = text->boundingRect();
        text->setPos(box.topLeft() - local.topLeft());
        qInfo() << "AnnotateWindow: select resize text width=" << text->textWidth()
                << "height=" << text->boxHeight() << "sceneBox=" << box << "pos=" << text->pos()
                << "handle=" << static_cast<int>(m_selectHandle) << "minH=" << minH;
        return;
    }
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
        case SelectHandle::Top:
            box.setTop(clamped.y());
            break;
        case SelectHandle::Bottom:
            box.setBottom(clamped.y());
            break;
        case SelectHandle::Left:
            box.setLeft(clamped.x());
            break;
        case SelectHandle::Right:
            box.setRight(clamped.x());
            break;
        default:
            qWarning() << "AnnotateWindow: applySelectResize bad highlight handle="
                       << static_cast<int>(m_selectHandle);
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
        qInfo() << "AnnotateWindow: select resize highlight" << box << "handle="
                << static_cast<int>(m_selectHandle);
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
        case SelectHandle::Top:
            box.setTop(clamped.y());
            break;
        case SelectHandle::Bottom:
            box.setBottom(clamped.y());
            break;
        case SelectHandle::Left:
            box.setLeft(clamped.x());
            break;
        case SelectHandle::Right:
            box.setRight(clamped.x());
            break;
        default:
            qWarning() << "AnnotateWindow: applySelectResize bad blur handle="
                       << static_cast<int>(m_selectHandle);
            return;
        }
        applyBlurSceneRect(pix, box);
        qInfo() << "AnnotateWindow: select resize blur" << box << "handle="
                << static_cast<int>(m_selectHandle);
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

// ─── Ariadne's Thread [AT-0166] ─────────────────────
// What: Scale a camera Photo cutout from any edge or corner, keep aspect
// Why:  Only the bottom-right grip scaled; an edge press moved the whole cutout
// Date: 2026-08-26
// Related: [AT-0060] AnnotateItems.h:AnnotatePhotoItem, [AT-0153] AnnotateWindow.cpp:hitSelectHandle
// ─────────────────────────────────────────────────────
void AnnotateWindow::applyPhotoScaleFromHandle(AnnotatePhotoItem *photo, const QPointF &scenePos)
{
    if (!photo) {
        qWarning() << "AnnotateWindow: applyPhotoScaleFromHandle null";
        return;
    }
    const QPointF clamped = clampToShot(scenePos);
    const QSizeF pix(photo->pixmap().size());
    if (pix.width() < 1 || pix.height() < 1) {
        qWarning() << "AnnotateWindow: applyPhotoScaleFromHandle empty pixmap";
        return;
    }
    const qreal pw = pix.width();
    const qreal ph = pix.height();
    const QPointF oldPos = m_photoOldPos;
    const QPointF oldBR(oldPos.x() + pw * m_photoOldScale, oldPos.y() + ph * m_photoOldScale);
    const QRectF shot = shotRect();
    const SelectHandle handle = m_photoScaleHandle;
    qreal maxScale = photo->minScale();
    switch (handle) {
    case SelectHandle::TopRight:
        maxScale = qMin((shot.right() - oldPos.x()) / pw, (oldBR.y() - shot.top()) / ph);
        break;
    case SelectHandle::Left:
    case SelectHandle::BottomLeft:
        maxScale = qMin((oldBR.x() - shot.left()) / pw, (shot.bottom() - oldPos.y()) / ph);
        break;
    case SelectHandle::TopLeft:
        maxScale = qMin((oldBR.x() - shot.left()) / pw, (oldBR.y() - shot.top()) / ph);
        break;
    case SelectHandle::Top:
        maxScale = qMin((shot.right() - oldPos.x()) / pw, (oldBR.y() - shot.top()) / ph);
        break;
    case SelectHandle::Right:
    case SelectHandle::Bottom:
    case SelectHandle::BottomRight:
    default:
        maxScale = qMin((shot.right() - oldPos.x()) / pw, (shot.bottom() - oldPos.y()) / ph);
        break;
    }
    maxScale = qMax(photo->minScale(), maxScale);
    qreal raw = m_photoOldScale;
    switch (handle) {
    case SelectHandle::Right:
    case SelectHandle::TopRight:
    case SelectHandle::BottomRight:
        raw = (clamped.x() - oldPos.x()) / pw;
        break;
    case SelectHandle::Left:
    case SelectHandle::TopLeft:
    case SelectHandle::BottomLeft:
        raw = (oldBR.x() - clamped.x()) / pw;
        break;
    case SelectHandle::Bottom:
        raw = (clamped.y() - oldPos.y()) / ph;
        break;
    case SelectHandle::Top:
        raw = (oldBR.y() - clamped.y()) / ph;
        break;
    default:
        raw = m_photoOldScale
              * (QLineF(oldPos, clamped).length() / qMax(1.0, QLineF(oldPos, m_resizeStart).length()));
        break;
    }
    const qreal scale = qBound(photo->minScale(), raw, maxScale);
    QPointF nextPos = oldPos;
    if (handle == SelectHandle::Left || handle == SelectHandle::TopLeft || handle == SelectHandle::BottomLeft) {
        nextPos.setX(oldBR.x() - pw * scale);
    }
    if (handle == SelectHandle::Top || handle == SelectHandle::TopLeft || handle == SelectHandle::TopRight) {
        nextPos.setY(oldBR.y() - ph * scale);
    }
    photo->setPhotoScale(scale);
    photo->setPos(nextPos);
    clampPhotoToShot(photo);
    qInfo() << "AnnotateWindow: photo scale live handle=" << static_cast<int>(handle) << "scale=" << scale
            << "pos=" << photo->pos() << "raw=" << raw << "max=" << maxScale << "oldPos=" << oldPos
            << "oldBR=" << oldBR << "clamped=" << clamped;
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
        } else if (kind == AnnotateKind::Text) {
            auto *text = qgraphicsitem_cast<AnnotateTextItem *>(item);
            if (text) {
                const qreal nowW = text->textWidth();
                const qreal nowH = text->boxHeight();
                const QPointF nowP = text->pos();
                const bool widthChanged = !qFuzzyCompare(nowW + 1.0, m_selectOldWidth + 1.0);
                const bool heightChanged = !qFuzzyCompare(nowH + 1.0, m_selectOldHeight + 1.0);
                if (widthChanged || heightChanged || nowP != m_selectOldPos) {
                    auto *macro = new QUndoCommand(QStringLiteral("Resize text"));
                    if (widthChanged) {
                        new ChangeTextWidthCommand(text, m_selectOldWidth, nowW, macro);
                    }
                    if (heightChanged) {
                        new ChangeTextHeightCommand(text, m_selectOldHeight, nowH, macro);
                    }
                    if (nowP != m_selectOldPos) {
                        ++m_photoMoveGestureId;
                        new ChangePhotoPosCommand(text, m_selectOldPos, nowP, m_photoMoveGestureId, macro);
                    }
                    m_undo->push(macro);
                    qInfo() << "AnnotateWindow: select text box committed width=" << m_selectOldWidth
                            << "->" << nowW << "height=" << m_selectOldHeight << "->" << nowH
                            << "pos=" << m_selectOldPos << "->" << nowP;
                }
            }
        }
    } else if (wasSelected) {
        if (auto *text = qgraphicsitem_cast<AnnotateTextItem *>(item)) {
            qInfo() << "AnnotateWindow: select second click edits text";
            beginEditText(text, false, m_selectPressScene);
        } else if (auto *rect = qgraphicsitem_cast<QGraphicsRectItem *>(item)) {
            for (QGraphicsItem *child : rect->childItems()) {
                if (auto *caption = qgraphicsitem_cast<AnnotateTextItem *>(child)) {
                    qInfo() << "AnnotateWindow: select second click edits fill+text caption";
                    m_fillTextRect = rect;
                    beginEditText(caption, false, m_selectPressScene);
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

// ─── Ariadne's Thread [AT-0327] ─────────────────────
// What: 1px cosmetic shot border in EditorView::drawForeground
// Why:  White screenshot on the window fill hid the image edge
// Date: 2026-08-28
// Related: [AT-0162] AnnotateWindow.cpp:EditorView, [AT-0056] AnnotateWindow.cpp:applyCanvasChrome
// ─────────────────────────────────────────────────────
void AnnotateWindow::paintShotBorder(QPainter *painter) const
{
    if (!painter || !m_photo) {
        return;
    }
    painter->save();
    QPen pen(QColor(142, 142, 147), 0);
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    const QRectF shot = shotRect();
    const qreal radius = hasBackground() ? m_photo->cornerRadius() : 0.0;
    const QRectF box = shot.adjusted(0.5, 0.5, -0.5, -0.5);
    if (radius > 0) {
        painter->drawRoundedRect(box, radius, radius);
    } else {
        painter->drawRect(box);
    }
    painter->restore();
}

void AnnotateWindow::paintSelectHandles(QPainter *painter) const
{
    if (!painter || !m_view) {
        return;
    }
    // ─── Ariadne's Thread [AT-0347] ─────────────────────
    // What: Keep text width handles visible and hittable while the caret is in the block
    // Why:  paintSelectHandles returned early on m_editingText so stretch grips vanished
    // Date: 2026-08-28
    // Related: [AT-0156] AnnotateWindow.cpp:paintSelectHandles, [AT-0153] AnnotateWindow.cpp:hitSelectHandle
    // ─────────────────────────────────────────────────────
    QGraphicsItem *item = m_selectItem;
    if (!item && m_editingText) {
        item = m_editingText;
        qInfo() << "AnnotateWindow: paintSelectHandles using editing text";
    }
    if (!item) {
        item = selectedAnnotation();
    }
    if (!item) {
        return;
    }
    const AnnotateKind kind = annotateKind(item);
    const qreal r = selectHandleHalfScene();
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
        paintAt(QPointF(box.center().x(), box.top()));
        paintAt(QPointF(box.center().x(), box.bottom()));
        paintAt(QPointF(box.left(), box.center().y()));
        paintAt(QPointF(box.right(), box.center().y()));
        return;
    }
    if (kind == AnnotateKind::Photo) {
        // ─── Ariadne's Thread [AT-0166] ─────────────────────
        // What: Draw Photo scale handles on all four edges and corners
        // Why:  The cutout had a dashed select box but no visible edge grips
        // Date: 2026-08-26
        // Related: [AT-0166] AnnotateWindow.cpp:hitSelectHandle, [AT-0156] AnnotateWindow.cpp:paintSelectHandles
        // ─────────────────────────────────────────────────────
        const QRectF box = itemSceneBox(item);
        paintAt(box.topLeft());
        paintAt(box.topRight());
        paintAt(box.bottomLeft());
        paintAt(box.bottomRight());
        paintAt(QPointF(box.center().x(), box.top()));
        paintAt(QPointF(box.center().x(), box.bottom()));
        paintAt(QPointF(box.left(), box.center().y()));
        paintAt(QPointF(box.right(), box.center().y()));
        qInfo() << "AnnotateWindow: paint photo scale handles box=" << box;
        return;
    }
    // ─── Ariadne's Thread [AT-0349] ─────────────────────
    // What: Paint Top/Bottom mid handles on the text box
    // Why:  Width-only grips could not stretch down or on the diagonal
    // Date: 2026-08-28
    // Related: [AT-0348] AnnotateItems.cpp:minTextHeight, [AT-0156] AnnotateWindow.cpp:applySelectResize
    // ─────────────────────────────────────────────────────
    if (kind == AnnotateKind::Text) {
        const QRectF box = itemSceneBox(item);
        paintAt(box.topLeft());
        paintAt(box.topRight());
        paintAt(box.bottomLeft());
        paintAt(box.bottomRight());
        paintAt(QPointF(box.center().x(), box.top()));
        paintAt(QPointF(box.center().x(), box.bottom()));
        paintAt(QPointF(box.left(), box.center().y()));
        paintAt(QPointF(box.right(), box.center().y()));
        qInfo() << "AnnotateWindow: paint text box handles box=" << box;
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
        // ─── Ariadne's Thread [AT-0343] ─────────────────────
        // What: Outside-shot click keeps Blur/Highlight/Steps; other tools go to cursor
        // Why:  Those three tools start and resize from chrome; Text/Arrow/Line must not
        // Date: 2026-08-28
        // Related: [AT-0342] AnnotateWindow.cpp:viewPress, [AT-0328] AnnotateWindow.cpp:viewPress
        // ─────────────────────────────────────────────────────
        qInfo() << "AnnotateWindow: press outside shot tool=" << static_cast<int>(m_tool)
                << "style=" << static_cast<int>(m_highlightStyle) << scenePos << "shot=" << shotRect();
        if (m_editingText) {
            qInfo() << "AnnotateWindow: outside shot commit text before tool decision";
            commitTextEdit();
        }
        QGraphicsItem *selected = selectedAnnotation();
        const AnnotateKind kind = selected ? annotateKind(selected) : AnnotateKind::None;
        const bool pullable = (kind == AnnotateKind::Blur || kind == AnnotateKind::Highlight);
        qInfo() << "AnnotateWindow: outside shot selectedKind=" << static_cast<int>(kind)
                << "pullable=" << pullable;
        if (pullable && tryStartHandleGesture(selected, scenePos)) {
            m_drawing = false;
            qInfo() << "AnnotateWindow: outside shot pull handle kind=" << static_cast<int>(kind)
                    << "keep tool=" << static_cast<int>(m_tool);
            return false;
        }
        const bool keepDrawTool = (m_tool == Tool::Blur || m_tool == Tool::Highlight);
        if (keepDrawTool) {
            qInfo() << "AnnotateWindow: outside shot keep draw tool=" << static_cast<int>(m_tool)
                    << "style=" << static_cast<int>(m_highlightStyle);
        } else if (m_tool != Tool::Select) {
            qInfo() << "AnnotateWindow: outside shot reset to Select from tool="
                    << static_cast<int>(m_tool) << "selectedKind=" << static_cast<int>(kind);
            setToolSelect();
        } else {
            qInfo() << "AnnotateWindow: outside shot already Select selectedKind="
                    << static_cast<int>(kind);
        }
    }
    // ─── Ariadne's Thread [AT-0352] ─────────────────────
    // What: Route every text grip through the 8-handle Select gesture
    // Why:  The old right-strip width drag stole BottomRight so diagonal stretch never ran
    // Date: 2026-08-28
    // Related: [AT-0348] AnnotateItems.cpp:setBoxHeight, [AT-0156] AnnotateWindow.cpp:applySelectResize
    // ─────────────────────────────────────────────────────
    AnnotateTextItem *handleText = m_editingText;
    if (!handleText) {
        handleText = qgraphicsitem_cast<AnnotateTextItem *>(selectedAnnotation());
    }
    if (!handleText) {
        handleText = textItemAt(scenePos);
    }
    if (handleText && tryStartHandleGesture(handleText, scenePos)) {
        m_drawing = false;
        m_textViewMouse = false;
        qInfo() << "AnnotateWindow: press text box handle tool=" << static_cast<int>(m_tool)
                << "editing=" << (m_editingText != nullptr) << "pos=" << scenePos;
        return false;
    }
    if (m_tool == Tool::Select) {
        commitTextEdit();
        if (tryStartHandleGesture(selectedAnnotation(), scenePos)) {
            m_drawing = false;
            qInfo() << "AnnotateWindow: select press handle on selected";
            return false;
        }
        QGraphicsItem *hit = annotationItemAt(scenePos);
        m_selectWasSelected = hit && hit->isSelected();
        selectAnnotation(hit);
        if (tryStartHandleGesture(hit, scenePos)) {
            m_drawing = false;
            qInfo() << "AnnotateWindow: select press handle on hit";
            return false;
        }
        if (auto *photo = qgraphicsitem_cast<AnnotatePhotoItem *>(hit)) {
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
            m_selectHandle = SelectHandle::Move;
            if (auto *rect = qgraphicsitem_cast<QGraphicsRectItem *>(hit)) {
                m_selectOldRect = rect->rect();
            }
            m_selectOldP1 = annotateP1(hit);
            m_selectOldP2 = annotateP2(hit);
            if (auto *pix = qgraphicsitem_cast<QGraphicsPixmapItem *>(hit)) {
                m_selectOldBlurPos = pix->pos();
                m_selectOldBlurSource = pix->data(kAnnotateRoleBlurSource).value<QImage>();
            }
            if (auto *text = qgraphicsitem_cast<AnnotateTextItem *>(hit)) {
                m_selectOldWidth = text->textWidth();
                if (m_selectOldWidth < 0) {
                    m_selectOldWidth = text->boundingRect().width();
                }
                m_selectOldHeight = text->boxHeight();
                m_selectOldRect = itemSceneBox(text);
                qInfo() << "AnnotateWindow: select text oldWidth=" << m_selectOldWidth
                        << "oldHeight=" << m_selectOldHeight << "oldRect=" << m_selectOldRect
                        << "handle=" << static_cast<int>(m_selectHandle);
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
            m_textViewMouse = true;
            qInfo() << "AnnotateWindow: click inside editing text, view owns mouse";
            return true;
        }
        commitTextEdit();
    }
    if (auto *text = textItemAt(scenePos)) {
        qInfo() << "AnnotateWindow: re-enter existing text";
        m_fillTextRect = qgraphicsitem_cast<QGraphicsRectItem *>(text->parentItem());
        beginEditText(text, false, scenePos);
        m_textViewMouse = false;
        return false;
    }
    if (auto *selectedPhoto = qgraphicsitem_cast<AnnotatePhotoItem *>(selectedAnnotation())) {
        if (tryStartHandleGesture(selectedPhoto, scenePos)) {
            qInfo() << "AnnotateWindow: photo scale handle on selected";
            return false;
        }
    }
    if (auto *photo = photoItemAt(scenePos)) {
        commitTextEdit();
        if (tryStartHandleGesture(photo, scenePos)) {
            qInfo() << "AnnotateWindow: photo scale handle on hit";
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
        m_textViewMouse = false;
        qInfo() << "AnnotateWindow: new text, skip view mousePress so shot cannot steal focus";
        return false;
    }
    if (m_tool == Tool::Highlight || m_tool == Tool::Blur) {
        const AnnotateKind want =
            (m_tool == Tool::Highlight) ? AnnotateKind::Highlight : AnnotateKind::Blur;
        QGraphicsItem *selected = selectedAnnotation();
        if (selected && annotateKind(selected) == want && tryStartHandleGesture(selected, scenePos)) {
            m_drawing = false;
            qInfo() << "AnnotateWindow: square handle on selected kind=" << static_cast<int>(want);
            return false;
        }
        QGraphicsItem *hit = annotationItemAt(scenePos);
        if (hit && annotateKind(hit) == want) {
            if (tryStartHandleGesture(hit, scenePos)) {
                m_drawing = false;
                qInfo() << "AnnotateWindow: square handle on hit kind=" << static_cast<int>(want);
                return false;
            }
            qInfo() << "AnnotateWindow: square press interior, new draft kind=" << static_cast<int>(want);
        }
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
        if (m_view) {
            m_view->viewport()->update();
        }
        qInfo() << "AnnotateWindow: text width live" << width << "viewport refresh";
        return false;
    }
    if (m_scalingPhoto) {
        applyPhotoScaleFromHandle(m_scalingPhoto, scenePos);
        if (m_view) {
            m_view->viewport()->update();
        }
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
        const SelectHandle handle = hitSelectHandle(m_editingText, scenePos);
        const bool onHandle = (handle != SelectHandle::None && handle != SelectHandle::Move);
        if (m_view && m_view->viewport()) {
            Qt::CursorShape cursor = Qt::IBeamCursor;
            if (onHandle) {
                switch (handle) {
                case SelectHandle::Left:
                case SelectHandle::Right:
                    cursor = Qt::SizeHorCursor;
                    break;
                case SelectHandle::Top:
                case SelectHandle::Bottom:
                    cursor = Qt::SizeVerCursor;
                    break;
                case SelectHandle::TopLeft:
                case SelectHandle::BottomRight:
                    cursor = Qt::SizeFDiagCursor;
                    break;
                case SelectHandle::TopRight:
                case SelectHandle::BottomLeft:
                    cursor = Qt::SizeBDiagCursor;
                    break;
                default:
                    cursor = Qt::SizeAllCursor;
                    break;
                }
            }
            m_view->viewport()->setCursor(cursor);
        }
        qInfo() << "AnnotateWindow: viewMove text viewOwnsMouse=" << m_textViewMouse
                << "handle=" << static_cast<int>(handle) << "onHandle=" << onHandle;
        if (onHandle) {
            return false;
        }
        return m_textViewMouse;
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
        const QPointF newPos = m_scalingPhoto->pos();
        qInfo() << "AnnotateWindow: photo scale commit" << m_photoOldScale << "->" << newScale
                << "pos" << m_photoOldPos << "->" << newPos
                << "handle=" << static_cast<int>(m_photoScaleHandle);
        if (!qFuzzyCompare(newScale + 1.0, m_photoOldScale + 1.0) || newPos != m_photoOldPos) {
            m_undo->push(new ChangePhotoScaleCommand(m_scalingPhoto, m_photoOldScale, newScale, m_photoOldPos, newPos,
                                                    m_photoScaleGestureId));
        }
        m_scalingPhoto = nullptr;
        m_photoScaleHandle = SelectHandle::None;
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
        const bool forward = m_textViewMouse;
        qInfo() << "AnnotateWindow: viewRelease text viewOwnsMouse=" << m_textViewMouse;
        m_textViewMouse = false;
        return forward;
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
    if (m_tool == Tool::Highlight || m_tool == Tool::Blur) {
        const QRectF rect = clipDraftToShot(m_dragStart, pos);
        if (rect.width() < 1 || rect.height() < 1) {
            if (auto *item = qgraphicsitem_cast<QGraphicsRectItem *>(m_draft)) {
                item->setVisible(false);
            }
            return;
        }
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
            qInfo() << "AnnotateWindow: draft created tool=" << static_cast<int>(m_tool) << "clip=" << rect;
            updateStrokeFillVisibility();
        }
        if (auto *item = qgraphicsitem_cast<QGraphicsRectItem *>(m_draft)) {
            item->setVisible(true);
            item->setRect(rect);
        }
    } else if (m_tool == Tool::Arrow || m_tool == Tool::Line) {
        const QPointF from = clampToShot(m_dragStart);
        if (QLineF(from, pos).length() < 1) {
            qInfo() << "AnnotateWindow: line clip too short tool=" << static_cast<int>(m_tool);
            return;
        }
        if (m_draft) {
            m_scene->removeItem(m_draft);
            delete m_draft;
            m_draft = nullptr;
        }
        if (m_tool == Tool::Arrow) {
            m_draft = makeArrow(from, pos, m_color);
        } else {
            m_draft = makeLine(from, pos, m_color);
        }
        m_scene->addItem(m_draft);
        qInfo() << "AnnotateWindow: draft line tool=" << static_cast<int>(m_tool) << "from=" << from << "to=" << pos;
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
        commitBlurRect(clipped, m_blurRadius, SensitiveKind::None, true);
        m_drawing = false;
        return;
    }
    QGraphicsItem *item = m_draft;
    m_draft = nullptr;
    if (auto *rect = qgraphicsitem_cast<QGraphicsRectItem *>(item)) {
        const QRectF clipped = rect->rect().intersected(shotRect());
        if (clipped.width() < 1 || clipped.height() < 1 || !rect->isVisible()) {
            m_scene->removeItem(item);
            delete item;
            m_drawing = false;
            qInfo() << "AnnotateWindow: discard empty highlight clip=" << clipped;
            return;
        }
        rect->setRect(clipped);
        rect->setVisible(true);
        qInfo() << "AnnotateWindow: highlight clipped to" << clipped;
    }
    const HighlightStyle style = (m_tool == Tool::Highlight) ? highlightStyle(item) : HighlightStyle::Fill;
    // ─── Ariadne's Thread [AT-0152] ─────────────────────
    // What: Keep the Steps rect on the scene, then place the badge against shotRect()
    // Why:  removeItem before applyAnnotateTextSize made shotRectOfItem empty and pinned digits off-shot
    // Date: 2026-08-26
    // Related: [AT-0151] AnnotateItems.cpp:shotRectOfItem, [AT-0134] AnnotateItems.cpp:placeHighlightStepBadge
    // ─────────────────────────────────────────────────────
    if (m_tool == Tool::Highlight && style == HighlightStyle::Steps) {
        if (auto *rect = qgraphicsitem_cast<QGraphicsRectItem *>(item)) {
            qInfo() << "AnnotateWindow: Steps commit onScene=" << (item->scene() != nullptr)
                    << "shot=" << shotRect() << "size=" << m_textSize << "cursor=" << pos
                    << "box=" << rect->rect();
            attachHighlightStepBadge(rect, m_color, m_nextStepSeq, shotRect(), pos);
            applyAnnotateTextSize(rect, m_textSize);
            applyAnnotateTextOutline(rect, m_textOutline);
            placeHighlightStepBadge(rect, shotRect(), pos);
            ++m_nextStepSeq;
            qInfo() << "AnnotateWindow: highlight Steps seq assigned next=" << m_nextStepSeq
                    << "cursor=" << pos << "shot=" << shotRect();
        }
    } else {
        m_scene->removeItem(item);
        qInfo() << "AnnotateWindow: commit removed from scene before undo add kind="
                << static_cast<int>(annotateKind(item));
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
    root.insert(QStringLiteral("fileId"), m_fileId);
    qInfo() << "AnnotateWindow: serialize fileId=" << m_fileId;
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
            obj.insert(QStringLiteral("textSize"), annotateTextSize(item));
            obj.insert(QStringLiteral("textOutline"), annotateTextOutline(item));
            obj.insert(QStringLiteral("rectX"), rect->rect().x());
            obj.insert(QStringLiteral("rectY"), rect->rect().y());
            obj.insert(QStringLiteral("rectW"), rect->rect().width());
            obj.insert(QStringLiteral("rectH"), rect->rect().height());
            for (QGraphicsItem *child : rect->childItems()) {
                if (auto *caption = qgraphicsitem_cast<AnnotateTextItem *>(child)) {
                    obj.insert(QStringLiteral("caption"), caption->toPlainText());
                    obj.insert(QStringLiteral("captionWidth"), caption->textWidth());
                    obj.insert(QStringLiteral("captionSize"), caption->textSize());
                    obj.insert(QStringLiteral("captionOutline"), caption->textOutline());
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
            obj.insert(QStringLiteral("textHeight"), text->boxHeight());
            obj.insert(QStringLiteral("textSize"), text->textSize());
            obj.insert(QStringLiteral("textOutline"), text->textOutline());
            qInfo() << "AnnotateWindow: serialize text width=" << text->textWidth()
                    << "height=" << text->boxHeight();
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
            obj.insert(QStringLiteral("redactKind"), blurRedactKind(item));
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
                applyAnnotateTextSize(item, obj.value(QStringLiteral("textSize")).toInt(14));
                applyAnnotateTextOutline(item, obj.value(QStringLiteral("textOutline")).toBool(false));
                placeHighlightStepBadge(item, shotRect(), item->mapToScene(item->rect().center()));
                qInfo() << "AnnotateWindow: restore Steps seq=" << obj.value(QStringLiteral("stepSeq")).toInt(1)
                        << "shot=" << shotRect() << "box=" << item->rect();
            }
            const QString caption = obj.value(QStringLiteral("caption")).toString();
            if (!caption.isEmpty()) {
                auto *text = new AnnotateTextItem(caption);
                text->setParentItem(item);
                const QRectF box = item->rect();
                text->setPos(box.topLeft() + QPointF(4, 4));
                text->setTextWidth(obj.value(QStringLiteral("captionWidth")).toDouble(box.width() - 8));
                text->setDefaultTextColor(color.isValid() ? color : m_color);
                text->setTextSize(obj.value(QStringLiteral("captionSize")).toInt(18));
                text->setTextOutline(obj.value(QStringLiteral("captionOutline")).toBool(false));
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
            text->setTextSize(obj.value(QStringLiteral("textSize")).toInt(18));
            text->setTextOutline(obj.value(QStringLiteral("textOutline")).toBool(false));
            text->setTextWidth(obj.value(QStringLiteral("textWidth")).toDouble(text->textWidth()));
            text->setBoxHeight(obj.value(QStringLiteral("textHeight")).toDouble(text->boxHeight()));
            qInfo() << "AnnotateWindow: restore text width=" << text->textWidth()
                    << "height=" << text->boxHeight();
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
            setBlurRedactKind(pix, obj.value(QStringLiteral("redactKind")).toInt(0));
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
    const QString restoredId = json.value(QStringLiteral("fileId")).toString();
    if (looksLikeShotFileId(restoredId)) {
        m_fileId = restoredId;
        qInfo() << "AnnotateWindow: restoreSession fileId=" << m_fileId;
    } else {
        qInfo() << "AnnotateWindow: restoreSession keep fileId=" << m_fileId << " jsonFileId=" << restoredId;
    }
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
    m_redactSessionRestored = true;
    qInfo() << "AnnotateWindow: restoreSession skip auto redact";
    if (m_undo) {
        m_undo->clear();
    }
    applyCanvasChrome();
    updateStrokeFillVisibility();
    fitShotToWindow();
    qInfo() << "AnnotateWindow: restoreSession items=" << json.value(QStringLiteral("items")).toArray().size()
            << " assets=" << assets.size();
    return true;
}
