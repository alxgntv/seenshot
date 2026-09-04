#include "capture/RegionPicker.h"

#include "app/MacPermissions.h"

#include <QDebug>
#include <QGuiApplication>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QShowEvent>

RegionPicker::RegionPicker(const QList<CaptureFreezeFrame> &frames, QWidget *parent)
    : QWidget(parent)
    , m_frames(frames)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_QuitOnClose, false);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setGeometry(screenUnion());
    qInfo() << "RegionPicker: overlay geometry" << geometry() << " screens=" << QGuiApplication::screens().size()
            << " frames=" << m_frames.size()
            << " alwaysShowTool=" << testAttribute(Qt::WA_MacAlwaysShowToolWindow)
            << " showWithoutActivating=" << testAttribute(Qt::WA_ShowWithoutActivating)
            << " noFocus=" << (windowFlags() & Qt::WindowDoesNotAcceptFocus)
            << " quitOnClose=" << testAttribute(Qt::WA_QuitOnClose);
    for (int i = 0; i < m_frames.size(); ++i) {
        const CaptureFreezeFrame &frame = m_frames.at(i);
        qInfo() << "RegionPicker: freeze frame" << i << "geo=" << frame.geometry << "image=" << frame.image.size()
                << "null=" << frame.image.isNull();
    }
}

QRect RegionPicker::screenUnion() const
{
    QRect unionRect;
    const auto screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        unionRect = unionRect.united(screen->geometry());
    }
    return unionRect;
}

void RegionPicker::syncRubberFromGlobal(const QPoint &globalNow)
{
    const QPoint a = mapFromGlobal(m_originGlobal);
    const QPoint b = mapFromGlobal(globalNow);
    m_rubber = QRect(a, b).normalized();
    qInfo() << "RegionPicker: rubber local=" << m_rubber << " originGlobal=" << m_originGlobal
            << " nowGlobal=" << globalNow << " geo=" << geometry();
}

void RegionPicker::drawFreeze(QPainter &painter, const QRect &clip) const
{
    painter.save();
    if (!clip.isNull()) {
        painter.setClipRect(clip, Qt::IntersectClip);
    }
    for (const CaptureFreezeFrame &frame : m_frames) {
        if (frame.image.isNull() || frame.geometry.isEmpty()) {
            qWarning() << "RegionPicker: drawFreeze skip empty geo=" << frame.geometry
                       << "null=" << frame.image.isNull();
            continue;
        }
        const QRect local(mapFromGlobal(frame.geometry.topLeft()), frame.geometry.size());
        painter.drawImage(local, frame.image);
    }
    painter.restore();
}

// ─── Ariadne's Thread [AT-0409] ─────────────────────
// What: Crop the pre-captured freeze to the dragged global rect
// Why:  A second ScreenCaptureKit pass ran after the overlay click closed menus
// Date: 2026-09-04
// Related: [AT-0406] ScreenCaptureBackend.mm:captureRegion, [AT-0010] RegionPicker.cpp
// ─────────────────────────────────────────────────────
QImage RegionPicker::croppedFreeze(const QRect &globalRect) const
{
    qInfo() << "RegionPicker: croppedFreeze global=" << globalRect << " frames=" << m_frames.size();
    if (globalRect.width() < 1 || globalRect.height() < 1) {
        qWarning() << "RegionPicker: croppedFreeze empty rect" << globalRect;
        return {};
    }
    struct Piece {
        QRect inter;
        QImage image;
        qreal scaleX = 1;
        qreal scaleY = 1;
    };
    QList<Piece> pieces;
    qreal scale = 1;
    for (int i = 0; i < m_frames.size(); ++i) {
        const CaptureFreezeFrame &frame = m_frames.at(i);
        if (frame.image.isNull() || frame.geometry.width() < 1 || frame.geometry.height() < 1) {
            qWarning() << "RegionPicker: croppedFreeze skip frame" << i << frame.geometry
                       << "null=" << frame.image.isNull();
            continue;
        }
        const QRect inter = globalRect.intersected(frame.geometry);
        if (inter.isEmpty()) {
            qInfo() << "RegionPicker: croppedFreeze no intersect frame" << i << "geo=" << frame.geometry;
            continue;
        }
        const qreal sx = static_cast<qreal>(frame.image.width()) / static_cast<qreal>(frame.geometry.width());
        const qreal sy = static_cast<qreal>(frame.image.height()) / static_cast<qreal>(frame.geometry.height());
        QRect src(qRound(static_cast<qreal>(inter.x() - frame.geometry.x()) * sx),
                  qRound(static_cast<qreal>(inter.y() - frame.geometry.y()) * sy),
                  qRound(static_cast<qreal>(inter.width()) * sx),
                  qRound(static_cast<qreal>(inter.height()) * sy));
        src = src.intersected(frame.image.rect());
        if (src.width() < 1 || src.height() < 1) {
            qWarning() << "RegionPicker: croppedFreeze empty src frame=" << i << "src=" << src
                       << "inter=" << inter << "image=" << frame.image.rect();
            continue;
        }
        const QImage piece = frame.image.copy(src);
        qInfo() << "RegionPicker: croppedFreeze piece" << i << "inter=" << inter << "src=" << src
                << "piece=" << piece.size() << "sx=" << sx << "sy=" << sy;
        if (piece.isNull()) {
            qWarning() << "RegionPicker: croppedFreeze copy failed frame=" << i << src;
            continue;
        }
        scale = qMax(scale, qMax(sx, sy));
        pieces.append({inter, piece, sx, sy});
    }
    if (pieces.isEmpty()) {
        qWarning() << "RegionPicker: croppedFreeze no pieces global=" << globalRect;
        return {};
    }
    if (pieces.size() == 1) {
        qInfo() << "RegionPicker: croppedFreeze single" << pieces.first().image.size() << pieces.first().inter;
        return pieces.first().image;
    }
    const int outW = qMax(1, qRound(static_cast<qreal>(globalRect.width()) * scale));
    const int outH = qMax(1, qRound(static_cast<qreal>(globalRect.height()) * scale));
    QImage out(outW, outH, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (int i = 0; i < pieces.size(); ++i) {
        const Piece &piece = pieces.at(i);
        const QRect dest(qRound(static_cast<qreal>(piece.inter.x() - globalRect.x()) * scale),
                         qRound(static_cast<qreal>(piece.inter.y() - globalRect.y()) * scale),
                         qRound(static_cast<qreal>(piece.inter.width()) * scale),
                         qRound(static_cast<qreal>(piece.inter.height()) * scale));
        qInfo() << "RegionPicker: croppedFreeze stitch" << i << "dest=" << dest << "src=" << piece.image.size();
        painter.drawImage(dest, piece.image);
    }
    painter.end();
    qInfo() << "RegionPicker: croppedFreeze stitched" << out.size() << "pieces=" << pieces.size()
            << "scale=" << scale;
    return out;
}

// ─── Ariadne's Thread [AT-0117] ─────────────────────
// What: Take keyboard so Escape can cancel a stuck overlay
// Why:  Tool overlay was not key; path hotkey left capturing=true
// Date: 2026-08-26
// Related: [AT-0118] MacPermissions.mm:pinCaptureOverlay, [AT-0010] RegionPicker.cpp
// ─────────────────────────────────────────────────────
// ─── Ariadne's Thread [AT-0405] ─────────────────────
// What: Do not focus or grabKeyboard when the path overlay appears
// Why:  Key window / app activation dismissed dropdowns in the captured app
// Date: 2026-09-03
// Related: [AT-0404] MacPermissions.mm:setCaptureEscapeHandler, [AT-0117] RegionPicker.cpp:showEvent
// ─────────────────────────────────────────────────────
void RegionPicker::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    installEscapeMonitor();
    MacPermissions::pinCaptureOverlay(this);
    qInfo() << "RegionPicker: showEvent visible=" << isVisible() << " geo=" << geometry()
            << " active=" << isActiveWindow();
}

void RegionPicker::hideEvent(QHideEvent *event)
{
    MacPermissions::clearCaptureEscapeHandler();
    if (mouseGrabber() == this) {
        releaseMouse();
        qInfo() << "RegionPicker: hideEvent releaseMouse";
    }
    QWidget::hideEvent(event);
    qInfo() << "RegionPicker: hideEvent";
}

void RegionPicker::installEscapeMonitor()
{
    QPointer<RegionPicker> self = this;
    MacPermissions::setCaptureEscapeHandler([self]() {
        if (!self || !self->isVisible()) {
            qInfo() << "RegionPicker: Escape monitor skip gone/hidden";
            return;
        }
        qInfo() << "RegionPicker: cancelled by Escape monitor";
        self->hide();
        emit self->cancelled();
    });
    qInfo() << "RegionPicker: Escape monitor installed visible=" << isVisible();
}

// ─── Ariadne's Thread [AT-0403] ─────────────────────
// What: Drop mouse/keyboard grab so a warning above the overlay can be dismissed
// Why:  grabKeyboard on the path overlay ate Return/Escape meant for QMessageBox
// Date: 2026-09-03
// Related: [AT-0401] MacPermissions.mm:pinAlertAboveCapture, [AT-0117] RegionPicker.cpp:showEvent
// ─────────────────────────────────────────────────────
void RegionPicker::yieldInput()
{
    MacPermissions::clearCaptureEscapeHandler();
    if (mouseGrabber() == this) {
        releaseMouse();
        qInfo() << "RegionPicker: yieldInput releaseMouse";
    }
    qInfo() << "RegionPicker: yieldInput visible=" << isVisible() << " dragging=" << m_dragging
            << " active=" << isActiveWindow();
}

void RegionPicker::resumeInput()
{
    if (!isVisible()) {
        qInfo() << "RegionPicker: resumeInput skip hidden";
        return;
    }
    installEscapeMonitor();
    qInfo() << "RegionPicker: resumeInput visible=" << isVisible() << " dragging=" << m_dragging
            << " active=" << isActiveWindow();
}

// ─── Ariadne's Thread [AT-0164] ─────────────────────
// What: Snap RegionPicker back if macOS drags the overlay from the first pixels
// Why:  press (28,5) then release QRect(28,-66) meant the window moved and captured 25x71
// Date: 2026-08-26
// Related: [AT-0163] MacPermissions.mm:pinCaptureOverlay, [AT-0010] RegionPicker.cpp
// ─────────────────────────────────────────────────────
void RegionPicker::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    if (m_snapping) {
        qInfo() << "RegionPicker: moveEvent skip snap old=" << event->oldPos() << " pos=" << event->pos();
        return;
    }
    const QRect lock = screenUnion();
    if (geometry() == lock) {
        qInfo() << "RegionPicker: moveEvent already locked geo=" << geometry();
        return;
    }
    qWarning() << "RegionPicker: window moved old=" << event->oldPos() << " pos=" << event->pos()
               << " geo=" << geometry() << " snap=" << lock;
    m_snapping = true;
    setGeometry(lock);
    m_snapping = false;
}

void RegionPicker::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    drawFreeze(painter, QRect());
    painter.fillRect(rect(), QColor(0, 0, 0, 90));
    if (!m_rubber.isNull()) {
        drawFreeze(painter, m_rubber);
        painter.setPen(QPen(Qt::white, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(m_rubber.adjusted(0, 0, -1, -1));
    }
}

void RegionPicker::mousePressEvent(QMouseEvent *event)
{
    MacPermissions::pinCaptureOverlay(this);
    m_dragging = true;
    m_originGlobal = event->globalPosition().toPoint();
    grabMouse();
    syncRubberFromGlobal(m_originGlobal);
    qInfo() << "RegionPicker: press local=" << event->pos() << " global=" << m_originGlobal
            << " geo=" << geometry() << " active=" << isActiveWindow() << " grabMouse=1";
    update();
}

void RegionPicker::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        return;
    }
    syncRubberFromGlobal(event->globalPosition().toPoint());
    update();
}

void RegionPicker::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        return;
    }
    m_dragging = false;
    if (mouseGrabber() == this) {
        releaseMouse();
        qInfo() << "RegionPicker: releaseMouse";
    }
    const QPoint now = event->globalPosition().toPoint();
    const QRect global = QRect(m_originGlobal, now).normalized();
    qInfo() << "RegionPicker: release originGlobal=" << m_originGlobal << " now=" << now
            << " global=" << global << " geo=" << geometry();
    hide();
    if (global.width() < 4 || global.height() < 4) {
        qInfo() << "RegionPicker: cancelled tiny global=" << global;
        emit cancelled();
        return;
    }
    emit regionPicked(global);
}

void RegionPicker::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        qInfo() << "RegionPicker: cancelled by Escape";
        hide();
        emit cancelled();
    }
}
