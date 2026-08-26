#include "annotate/HueColorSlider.h"

#include <QDebug>
#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>
#include <QStyle>
#include <QStyleOptionSlider>

HueColorSlider::HueColorSlider(QWidget *parent)
    : QSlider(Qt::Horizontal, parent)
{
    setRange(0, 359);
    setValue(0);
    setFixedWidth(90);
    setFixedHeight(24);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    qInfo() << "HueColorSlider: created range 0-359";
}

QColor HueColorSlider::currentColor() const
{
    return QColor::fromHsv(value(), 255, 230);
}

void HueColorSlider::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QStyleOptionSlider option;
    initStyleOption(&option);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
    const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);

    QRect track = groove;
    if (track.height() < 8) {
        track.setHeight(8);
        track.moveCenter(groove.center());
    }
    track.adjust(4, 2, -4, -2);

    QLinearGradient gradient(track.topLeft(), track.topRight());
    for (int i = 0; i <= 6; ++i) {
        gradient.setColorAt(static_cast<qreal>(i) / 6.0, QColor::fromHsv((i * 60) % 360, 255, 255));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(track, 4, 4);

    const QColor thumb = currentColor();
    QRect knob = handle;
    knob.setSize(QSize(14, 14));
    knob.moveCenter(QPoint(handle.center().x(), track.center().y()));
    painter.setBrush(thumb);
    painter.setPen(QPen(Qt::white, 2));
    painter.drawEllipse(knob);
}
