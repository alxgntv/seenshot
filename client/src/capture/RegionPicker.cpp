#include "capture/RegionPicker.h"

#include <QDebug>
#include <QGuiApplication>
#include <QHideEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QShowEvent>

RegionPicker::RegionPicker(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    QRect unionRect;
    const auto screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        unionRect = unionRect.united(screen->geometry());
    }
    setGeometry(unionRect);
    qInfo() << "RegionPicker: overlay geometry" << unionRect << " screens=" << screens.size()
            << " alwaysShowTool=" << testAttribute(Qt::WA_MacAlwaysShowToolWindow);
}

// ─── Ariadne's Thread [AT-0117] ─────────────────────
// What: Take keyboard so Escape can cancel a stuck overlay
// Why:  Tool overlay was not key; path hotkey left capturing=true
// Date: 2026-08-26
// Related: [AT-0118] MacPermissions.mm:pinCaptureOverlay, [AT-0010] RegionPicker.cpp
// ─────────────────────────────────────────────────────
void RegionPicker::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    setFocus(Qt::ActiveWindowFocusReason);
    grabKeyboard();
    qInfo() << "RegionPicker: showEvent visible=" << isVisible() << " geo=" << geometry()
            << " active=" << isActiveWindow();
}

void RegionPicker::hideEvent(QHideEvent *event)
{
    releaseKeyboard();
    QWidget::hideEvent(event);
    qInfo() << "RegionPicker: hideEvent";
}

void RegionPicker::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 90));
    if (!m_rubber.isNull()) {
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillRect(m_rubber, Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setPen(QPen(Qt::white, 1));
        painter.drawRect(m_rubber.adjusted(0, 0, -1, -1));
    }
}

void RegionPicker::mousePressEvent(QMouseEvent *event)
{
    m_dragging = true;
    m_origin = event->pos();
    m_rubber = QRect(m_origin, QSize());
    qInfo() << "RegionPicker: press" << m_origin;
    update();
}

void RegionPicker::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        return;
    }
    m_rubber = QRect(m_origin, event->pos()).normalized();
    update();
}

void RegionPicker::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        return;
    }
    m_dragging = false;
    m_rubber = QRect(m_origin, event->pos()).normalized();
    const QRect global(mapToGlobal(m_rubber.topLeft()), m_rubber.size());
    qInfo() << "RegionPicker: release local=" << m_rubber << " global=" << global;
    hide();
    if (global.width() < 4 || global.height() < 4) {
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
