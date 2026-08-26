#include "annotate/AnnotateItems.h"

#include <QBrush>
#include <QDebug>
#include <QVariant>
#include <QFont>
#include <QLineF>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QStyleOptionGraphicsItem>
#include <algorithm>

void setAnnotateKind(QGraphicsItem *item, AnnotateKind kind)
{
    if (!item) {
        qWarning() << "setAnnotateKind: null item";
        return;
    }
    item->setData(kAnnotateRoleKind, static_cast<int>(kind));
    item->setFlag(QGraphicsItem::ItemIsSelectable, true);
    qInfo() << "setAnnotateKind:" << static_cast<int>(kind) << "type=" << item->type()
            << "selectable=" << item->flags().testFlag(QGraphicsItem::ItemIsSelectable);
}

AnnotateKind annotateKind(const QGraphicsItem *item)
{
    if (!item) {
        return AnnotateKind::None;
    }
    return static_cast<AnnotateKind>(item->data(kAnnotateRoleKind).toInt());
}

void setHighlightStyle(QGraphicsItem *item, HighlightStyle style)
{
    if (!item) {
        qWarning() << "setHighlightStyle: null item";
        return;
    }
    item->setData(kAnnotateRoleStyle, static_cast<int>(style));
    qInfo() << "setHighlightStyle:" << static_cast<int>(style);
}

HighlightStyle highlightStyle(const QGraphicsItem *item)
{
    if (!item) {
        return HighlightStyle::Fill;
    }
    return static_cast<HighlightStyle>(item->data(kAnnotateRoleStyle).toInt());
}

void setHighlightStrokeWidth(QGraphicsItem *item, int width)
{
    if (!item) {
        qWarning() << "setHighlightStrokeWidth: null item";
        return;
    }
    const int clamped = qBound(1, width, 16);
    item->setData(kAnnotateRoleStrokeWidth, clamped);
    qInfo() << "setHighlightStrokeWidth:" << clamped;
}

int highlightStrokeWidth(const QGraphicsItem *item)
{
    if (!item) {
        return 2;
    }
    const QVariant value = item->data(kAnnotateRoleStrokeWidth);
    if (!value.isValid()) {
        return 2;
    }
    return qBound(1, value.toInt(), 16);
}

void setHighlightFillAlpha(QGraphicsItem *item, int alpha)
{
    if (!item) {
        qWarning() << "setHighlightFillAlpha: null item";
        return;
    }
    const int clamped = qBound(0, alpha, 255);
    item->setData(kAnnotateRoleFillAlpha, clamped);
    qInfo() << "setHighlightFillAlpha:" << clamped;
}

int highlightFillAlpha(const QGraphicsItem *item)
{
    if (!item) {
        return 80;
    }
    const QVariant value = item->data(kAnnotateRoleFillAlpha);
    if (!value.isValid()) {
        return 80;
    }
    return qBound(0, value.toInt(), 255);
}

bool isColorableKind(AnnotateKind kind)
{
    return kind == AnnotateKind::Highlight || kind == AnnotateKind::Arrow || kind == AnnotateKind::Text
        || kind == AnnotateKind::Step || kind == AnnotateKind::Line;
}

QColor contrastInk(const QColor &fill)
{
    const int y = qGray(fill.rgb());
    const QColor ink = (y > 140) ? QColor(20, 20, 20) : QColor(250, 250, 250);
    qInfo() << "contrastInk: gray=" << y << "ink=" << ink;
    return ink;
}

// ─── Ariadne's Thread [AT-0108] ─────────────────────
// What: Stroke width and fill alpha drive Square and Steps, including the badge
// Why:  Border style ignored Fill; badge used a fixed pen and alpha
// Date: 2026-08-26
// Related: [AT-0046] AnnotateWindow.cpp:onStrokeChanged, [AT-0041] AnnotateItems.h:StepBadgeItem
// ─────────────────────────────────────────────────────
void applyHighlightAppearance(QGraphicsRectItem *rect, const QColor &color, HighlightStyle style)
{
    if (!rect) {
        qWarning() << "applyHighlightAppearance: null rect";
        return;
    }
    const int width = highlightStrokeWidth(rect);
    const int alpha = highlightFillAlpha(rect);
    rect->setPen(QPen(color, width));
    if (alpha <= 0) {
        rect->setBrush(Qt::NoBrush);
        qInfo() << "applyHighlightAppearance: stroke only style=" << static_cast<int>(style) << color
                << "width=" << width;
    } else {
        QColor fill = color;
        fill.setAlpha(alpha);
        rect->setBrush(fill);
        qInfo() << "applyHighlightAppearance: fill style=" << static_cast<int>(style) << color
                << "width=" << width << "alpha=" << alpha;
    }
    const auto children = rect->childItems();
    for (QGraphicsItem *child : children) {
        if (auto *text = dynamic_cast<QGraphicsTextItem *>(child)) {
            text->setDefaultTextColor(color);
            qInfo() << "applyHighlightAppearance: caption color" << color;
        }
        if (auto *badge = qgraphicsitem_cast<StepBadgeItem *>(child)) {
            badge->applyColor(color);
            qInfo() << "applyHighlightAppearance: step badge color" << color << "width=" << width
                    << "alpha=" << alpha;
        }
    }
}

namespace {

constexpr qreal kStepBadgeRadius = 14.0;

bool stepBadgeFitsShot(const QGraphicsRectItem *rect, const QPointF &localCenter, const QRectF &shot)
{
    if (!rect) {
        return false;
    }
    const QRectF local(localCenter.x() - kStepBadgeRadius, localCenter.y() - kStepBadgeRadius,
                       kStepBadgeRadius * 2.0, kStepBadgeRadius * 2.0);
    const QRectF sceneBadge = rect->mapRectToScene(local);
    const bool ok = shot.contains(sceneBadge);
    qInfo() << "stepBadgeFitsShot: local=" << localCenter << "scene=" << sceneBadge << "fit=" << ok;
    return ok;
}

QPointF clampStepBadgeLocal(const QGraphicsRectItem *rect, QPointF localCenter, const QRectF &shot)
{
    if (!rect) {
        qWarning() << "clampStepBadgeLocal: null rect";
        return localCenter;
    }
    const QRectF local(localCenter.x() - kStepBadgeRadius, localCenter.y() - kStepBadgeRadius,
                       kStepBadgeRadius * 2.0, kStepBadgeRadius * 2.0);
    const QRectF sceneBadge = rect->mapRectToScene(local);
    qreal dx = 0;
    qreal dy = 0;
    if (sceneBadge.left() < shot.left()) {
        dx = shot.left() - sceneBadge.left();
    }
    if (sceneBadge.right() > shot.right()) {
        dx = shot.right() - sceneBadge.right();
    }
    if (sceneBadge.top() < shot.top()) {
        dy = shot.top() - sceneBadge.top();
    }
    if (sceneBadge.bottom() > shot.bottom()) {
        dy = shot.bottom() - sceneBadge.bottom();
    }
    const QPointF clamped = localCenter + QPointF(dx, dy);
    qInfo() << "clampStepBadgeLocal: from=" << localCenter << "to=" << clamped << "dx=" << dx << "dy=" << dy;
    return clamped;
}

QPointF outsideStepBadgeCenter(const QRectF &box, int corner)
{
    switch (corner) {
    case 0:
        return box.topLeft() + QPointF(-kStepBadgeRadius, -kStepBadgeRadius);
    case 1:
        return QPointF(box.right(), box.top()) + QPointF(kStepBadgeRadius, -kStepBadgeRadius);
    case 2:
        return QPointF(box.left(), box.bottom()) + QPointF(-kStepBadgeRadius, kStepBadgeRadius);
    default:
        return box.bottomRight() + QPointF(kStepBadgeRadius, kStepBadgeRadius);
    }
}

QPointF stepBoxCorner(const QRectF &box, int corner)
{
    switch (corner) {
    case 0:
        return box.topLeft();
    case 1:
        return QPointF(box.right(), box.top());
    case 2:
        return QPointF(box.left(), box.bottom());
    default:
        return box.bottomRight();
    }
}

StepBadgeItem *ensureStepBadge(QGraphicsRectItem *rect, const QColor &color, int seq)
{
    if (!rect) {
        qWarning() << "ensureStepBadge: null rect";
        return nullptr;
    }
    rect->setData(kAnnotateRoleStepSeq, seq);
    for (QGraphicsItem *child : rect->childItems()) {
        if (auto *badge = qgraphicsitem_cast<StepBadgeItem *>(child)) {
            badge->setData(kAnnotateRoleStepSeq, seq);
            qInfo() << "ensureStepBadge: reuse seq=" << seq;
            return badge;
        }
    }
    auto *badge = new StepBadgeItem(seq, color);
    badge->setParentItem(rect);
    applyHighlightAppearance(rect, color, highlightStyle(rect));
    qInfo() << "ensureStepBadge: created seq=" << seq;
    return badge;
}

} // namespace

// ─── Ariadne's Thread [AT-0134] ─────────────────────
// What: Place the Steps badge after release, outside, corner touching corner
// Why:  Live-under-cursor sat in the corner center and appeared while dragging
// Date: 2026-08-26
// Related: [AT-0041] AnnotateItems.h:StepBadgeItem, docs/PRD-02-annotate-tools.md
// ─────────────────────────────────────────────────────
void placeHighlightStepBadge(QGraphicsRectItem *rect, const QRectF &shot, const QPointF &cursorScene)
{
    if (!rect) {
        qWarning() << "placeHighlightStepBadge: null rect";
        return;
    }
    StepBadgeItem *badge = nullptr;
    for (QGraphicsItem *child : rect->childItems()) {
        badge = qgraphicsitem_cast<StepBadgeItem *>(child);
        if (badge) {
            break;
        }
    }
    if (!badge) {
        qWarning() << "placeHighlightStepBadge: no badge";
        return;
    }
    const QRectF box = rect->rect();
    int order[4] = {0, 1, 2, 3};
    qreal dist[4];
    for (int i = 0; i < 4; ++i) {
        dist[i] = QLineF(rect->mapToScene(stepBoxCorner(box, i)), cursorScene).length();
        qInfo() << "placeHighlightStepBadge: corner=" << i << "dist=" << dist[i];
    }
    std::sort(order, order + 4, [&](int a, int b) {
        return dist[a] < dist[b];
    });
    QPointF chosen = outsideStepBadgeCenter(box, order[0]);
    bool fitted = false;
    for (int i = 0; i < 4; ++i) {
        const QPointF candidate = outsideStepBadgeCenter(box, order[i]);
        if (stepBadgeFitsShot(rect, candidate, shot)) {
            chosen = candidate;
            fitted = true;
            qInfo() << "placeHighlightStepBadge: fit corner=" << order[i] << "center=" << candidate
                    << "cursor=" << cursorScene;
            break;
        }
    }
    if (!fitted) {
        chosen = clampStepBadgeLocal(rect, chosen, shot);
        qInfo() << "placeHighlightStepBadge: no outside corner fits, clamped=" << chosen
                << "cursor=" << cursorScene;
    }
    badge->setPos(chosen);
    qInfo() << "placeHighlightStepBadge: pos=" << chosen << "box=" << box;
}

void attachHighlightStepBadge(QGraphicsRectItem *rect, const QColor &color, int seq, const QRectF &shot)
{
    if (!ensureStepBadge(rect, color, seq)) {
        return;
    }
    const QRectF box = rect->rect();
    const QPointF candidates[] = {
        box.topLeft() + QPointF(-kStepBadgeRadius, -kStepBadgeRadius),
        QPointF(box.right(), box.top()) + QPointF(kStepBadgeRadius, -kStepBadgeRadius),
        QPointF(box.left(), box.bottom()) + QPointF(-kStepBadgeRadius, kStepBadgeRadius),
        box.bottomRight() + QPointF(kStepBadgeRadius, kStepBadgeRadius),
    };
    QPointF chosen = candidates[0];
    bool fitted = false;
    for (const QPointF &candidate : candidates) {
        if (stepBadgeFitsShot(rect, candidate, shot)) {
            chosen = candidate;
            fitted = true;
            qInfo() << "attachHighlightStepBadge: restore corner fits" << candidate;
            break;
        }
    }
    if (!fitted) {
        chosen = clampStepBadgeLocal(rect, chosen, shot);
    }
    for (QGraphicsItem *child : rect->childItems()) {
        if (auto *badge = qgraphicsitem_cast<StepBadgeItem *>(child)) {
            badge->setPos(chosen);
            break;
        }
    }
    qInfo() << "attachHighlightStepBadge: restore seq=" << seq << "pos=" << chosen;
}

void attachHighlightStepBadge(QGraphicsRectItem *rect, const QColor &color, int seq, const QRectF &shot,
                              const QPointF &cursorScene)
{
    if (!ensureStepBadge(rect, color, seq)) {
        return;
    }
    placeHighlightStepBadge(rect, shot, cursorScene);
    qInfo() << "attachHighlightStepBadge: cursor seq=" << seq << "scene=" << cursorScene;
}

void syncStepNumbers(QGraphicsScene *scene)
{
    if (!scene) {
        qWarning() << "syncStepNumbers: null scene";
        return;
    }
    QVector<QGraphicsRectItem *> rects;
    const auto items = scene->items();
    for (QGraphicsItem *item : items) {
        if (item->parentItem()) {
            continue;
        }
        if (annotateKind(item) == AnnotateKind::Highlight && highlightStyle(item) == HighlightStyle::Steps) {
            rects.append(static_cast<QGraphicsRectItem *>(item));
        }
    }
    std::sort(rects.begin(), rects.end(), [](const QGraphicsRectItem *a, const QGraphicsRectItem *b) {
        return a->data(kAnnotateRoleStepSeq).toInt() < b->data(kAnnotateRoleStepSeq).toInt();
    });
    for (int i = 0; i < rects.size(); ++i) {
        const int number = i + 1;
        for (QGraphicsItem *child : rects[i]->childItems()) {
            if (auto *badge = qgraphicsitem_cast<StepBadgeItem *>(child)) {
                badge->setNumber(number);
            }
        }
    }
    qInfo() << "syncStepNumbers: count=" << rects.size();
}

QImage boxBlur(const QImage &patch, int radius)
{
    if (patch.isNull()) {
        qWarning() << "boxBlur: empty patch";
        return {};
    }
    const int r = qBound(0, radius, 20);
    if (r == 0) {
        qInfo() << "boxBlur: radius 0, copy" << patch.size();
        return patch;
    }
    QImage out = patch;
    for (int y = 0; y < patch.height(); ++y) {
        for (int x = 0; x < patch.width(); ++x) {
            int red = 0, green = 0, blue = 0, alpha = 0, n = 0;
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    const int xx = qBound(0, x + dx, patch.width() - 1);
                    const int yy = qBound(0, y + dy, patch.height() - 1);
                    const QRgb px = patch.pixel(xx, yy);
                    red += qRed(px);
                    green += qGreen(px);
                    blue += qBlue(px);
                    alpha += qAlpha(px);
                    ++n;
                }
            }
            out.setPixel(x, y, qRgba(red / n, green / n, blue / n, alpha / n));
        }
    }
    qInfo() << "boxBlur: radius=" << r << "size=" << patch.size();
    return out;
}

void setBlurSource(QGraphicsItem *item, const QImage &source)
{
    if (!item) {
        qWarning() << "setBlurSource: null item";
        return;
    }
    item->setData(kAnnotateRoleBlurSource, source);
    qInfo() << "setBlurSource: size=" << source.size();
}

int blurRadius(const QGraphicsItem *item)
{
    if (!item) {
        return 8;
    }
    const QVariant value = item->data(kAnnotateRoleBlurRadius);
    if (!value.isValid()) {
        return 8;
    }
    return qBound(0, value.toInt(), 20);
}

void applyBlurRadius(QGraphicsItem *item, int radius)
{
    if (!item) {
        qWarning() << "applyBlurRadius: null item";
        return;
    }
    auto *pix = qgraphicsitem_cast<QGraphicsPixmapItem *>(item);
    if (!pix) {
        qWarning() << "applyBlurRadius: not a pixmap item type=" << item->type();
        return;
    }
    const int clamped = qBound(0, radius, 20);
    item->setData(kAnnotateRoleBlurRadius, clamped);
    const QImage source = item->data(kAnnotateRoleBlurSource).value<QImage>();
    if (source.isNull()) {
        qWarning() << "applyBlurRadius: missing source patch";
        return;
    }
    pix->setPixmap(QPixmap::fromImage(boxBlur(source, clamped)));
    qInfo() << "applyBlurRadius: radius=" << clamped;
}

// ─── Ariadne's Thread [AT-0072] ─────────────────────
// What: Highlight child layout, arrow path, stored line/arrow endpoints
// Why:  Select tool must resize existing items without a second editor
// Date: 2026-08-25
// Related: [AT-0066] AnnotateWindow.cpp, [AT-0054] AnnotateWindow.cpp:makeArrow
// ─────────────────────────────────────────────────────
void layoutHighlightChrome(QGraphicsRectItem *rect)
{
    if (!rect) {
        qWarning() << "layoutHighlightChrome: null rect";
        return;
    }
    const QRectF box = rect->rect();
    qInfo() << "layoutHighlightChrome: box=" << box << "children=" << rect->childItems().size();
    for (QGraphicsItem *child : rect->childItems()) {
        if (auto *text = dynamic_cast<AnnotateTextItem *>(child)) {
            text->setPos(box.topLeft() + QPointF(4, 4));
            text->setTextWidth(qMax(text->minTextWidth(), box.width() - 8));
            qInfo() << "layoutHighlightChrome: caption width=" << text->textWidth();
        }
        if (auto *badge = qgraphicsitem_cast<StepBadgeItem *>(child)) {
            qInfo() << "layoutHighlightChrome: keep step badge pos=" << badge->pos();
        }
    }
}

QPointF annotateP1(const QGraphicsItem *item)
{
    if (!item) {
        return {};
    }
    return item->data(kAnnotateRoleP1).toPointF();
}

QPointF annotateP2(const QGraphicsItem *item)
{
    if (!item) {
        return {};
    }
    return item->data(kAnnotateRoleP2).toPointF();
}

void setAnnotateEndpoints(QGraphicsItem *item, const QPointF &p1, const QPointF &p2)
{
    if (!item) {
        qWarning() << "setAnnotateEndpoints: null item";
        return;
    }
    item->setData(kAnnotateRoleP1, p1);
    item->setData(kAnnotateRoleP2, p2);
    qInfo() << "setAnnotateEndpoints: p1=" << p1 << "p2=" << p2;
}

QPainterPath arrowPath(const QPointF &from, const QPointF &to)
{
    QPainterPath path;
    const QLineF line(from, to);
    const qreal len = line.length();
    if (len < 1) {
        path.moveTo(from);
        path.lineTo(to);
        qInfo() << "arrowPath: too short, shaft only from" << from << "to" << to;
        return path;
    }
    const qreal head = qBound(10.0, len * 0.28, 22.0);
    QLineF left(to, from);
    left.setLength(head);
    left.setAngle(line.angle() + 155);
    QLineF right(to, from);
    right.setLength(head);
    right.setAngle(line.angle() + 205);
    const QPointF neck((left.p2().x() + right.p2().x()) / 2.0, (left.p2().y() + right.p2().y()) / 2.0);
    path.moveTo(from);
    path.lineTo(neck);
    path.moveTo(to);
    path.lineTo(left.p2());
    path.lineTo(right.p2());
    path.closeSubpath();
    qInfo() << "arrowPath: from" << from << "to" << to << "len=" << len << "head=" << head;
    return path;
}

void setArrowEndpoints(QGraphicsPathItem *item, const QPointF &from, const QPointF &to)
{
    if (!item) {
        qWarning() << "setArrowEndpoints: null item";
        return;
    }
    item->setPath(arrowPath(from, to));
    setAnnotateEndpoints(item, from, to);
    qInfo() << "setArrowEndpoints: from=" << from << "to=" << to;
}

AnnotateTextItem::AnnotateTextItem(const QString &text)
    : QGraphicsTextItem(text)
{
    setAnnotateKind(this, AnnotateKind::Text);
    setTextInteractionFlags(Qt::NoTextInteraction);
    setTextWidth(160);
    qInfo() << "AnnotateTextItem: created width=" << textWidth();
}

int AnnotateTextItem::type() const
{
    return Type;
}

// ─── Ariadne's Thread [AT-0112] ─────────────────────
// What: Hit-test the wrap box, not glyph outlines
// Why:  Clicks in padding missed the item and spawned a second text
// Date: 2026-08-26
// Related: [AT-0111] AnnotateWindow.cpp:textItemAt, [AT-0040] AnnotateItems.h:AnnotateTextItem
// ─────────────────────────────────────────────────────
QPainterPath AnnotateTextItem::shape() const
{
    QPainterPath path;
    path.addRect(boundingRect());
    return path;
}

qreal AnnotateTextItem::minTextWidth() const
{
    return 40;
}

bool AnnotateTextItem::isEditing() const
{
    return textInteractionFlags() & Qt::TextEditorInteraction;
}

bool AnnotateTextItem::isResizeHandle(const QPointF &itemPos) const
{
    const QRectF bounds = boundingRect();
    const bool hit = itemPos.x() >= bounds.right() - 10 && bounds.contains(itemPos);
    qInfo() << "AnnotateTextItem: resize handle hit=" << hit << itemPos << bounds;
    return hit;
}

void AnnotateTextItem::beginEdit()
{
    setTextInteractionFlags(Qt::TextEditorInteraction);
    setFocus(Qt::MouseFocusReason);
    qInfo() << "AnnotateTextItem: beginEdit";
}

void AnnotateTextItem::endEdit()
{
    setTextInteractionFlags(Qt::NoTextInteraction);
    clearFocus();
    qInfo() << "AnnotateTextItem: endEdit text=" << toPlainText();
}

void AnnotateTextItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    QGraphicsTextItem::paint(painter, option, widget);
    if (isEditing()) {
        return;
    }
    const QRectF bounds = boundingRect();
    const QRectF grip(bounds.right() - 6, bounds.center().y() - 8, 6, 16);
    painter->fillRect(grip, QColor(40, 40, 40, 180));
}

StepBadgeItem::StepBadgeItem(int seq, const QColor &color)
    : QGraphicsRectItem(QRectF(-14, -14, 28, 28))
{
    setAnnotateKind(this, AnnotateKind::Step);
    setData(kAnnotateRoleStepSeq, seq);
    applyColor(color);
    qInfo() << "StepBadgeItem: created seq=" << seq << "color=" << color;
}

int StepBadgeItem::type() const
{
    return Type;
}

void StepBadgeItem::setNumber(int number)
{
    if (m_number == number) {
        return;
    }
    m_number = number;
    update();
    qInfo() << "StepBadgeItem: number=" << m_number << "seq=" << seq();
}

int StepBadgeItem::number() const
{
    return m_number;
}

int StepBadgeItem::seq() const
{
    return data(kAnnotateRoleStepSeq).toInt();
}

// ─── Ariadne's Thread [AT-0135] ─────────────────────
// What: Steps badge fill is always opaque; Fill slider only tints the rectangle
// Why:  Digit sat on a translucent square and became unreadable
// Date: 2026-08-26
// Related: [AT-0108] AnnotateItems.cpp:applyHighlightAppearance, docs/PRD-02-annotate-tools.md
// ─────────────────────────────────────────────────────
void StepBadgeItem::applyColor(const QColor &color)
{
    int width = 2;
    if (parentItem()) {
        width = highlightStrokeWidth(parentItem());
    }
    setPen(QPen(color, width));
    QColor fill = color;
    fill.setAlpha(255);
    setBrush(fill);
    m_ink = contrastInk(color);
    update();
    qInfo() << "StepBadgeItem: applyColor" << color << "width=" << width << "fillAlpha=255"
            << "ink=" << m_ink;
}

void StepBadgeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    QGraphicsRectItem::paint(painter, option, widget);
    painter->setPen(m_ink);
    QFont font = painter->font();
    font.setBold(true);
    font.setPixelSize(14);
    painter->setFont(font);
    painter->drawText(rect(), Qt::AlignCenter, QString::number(m_number));
}

ShotPhotoItem::ShotPhotoItem(const QPixmap &pixmap)
    : QGraphicsPixmapItem(pixmap)
{
    setFlag(QGraphicsItem::ItemClipsToShape, true);
    setShapeMode(QGraphicsPixmapItem::BoundingRectShape);
    setTransformationMode(Qt::SmoothTransformation);
    qInfo() << "ShotPhotoItem: created size=" << pixmap.size();
}

int ShotPhotoItem::type() const
{
    return Type;
}

void ShotPhotoItem::setCornerRadius(qreal radius)
{
    const qreal next = qMax(0.0, radius);
    if (qFuzzyCompare(m_radius + 1.0, next + 1.0)) {
        return;
    }
    m_radius = next;
    qInfo() << "ShotPhotoItem: corner radius=" << m_radius;
    update();
}

qreal ShotPhotoItem::cornerRadius() const
{
    return m_radius;
}

QPainterPath ShotPhotoItem::shape() const
{
    QPainterPath path;
    const QRectF bounds = boundingRect();
    const qreal maxR = qMin(bounds.width(), bounds.height()) / 2.0;
    const qreal r = qBound(0.0, m_radius, maxR);
    if (r <= 0) {
        path.addRect(bounds);
    } else {
        path.addRoundedRect(bounds, r, r);
    }
    return path;
}

AnnotatePhotoItem::AnnotatePhotoItem(const QPixmap &pixmap)
    : QGraphicsPixmapItem(pixmap)
{
    setAnnotateKind(this, AnnotateKind::Photo);
    setTransformationMode(Qt::SmoothTransformation);
    qInfo() << "AnnotatePhotoItem: created size=" << pixmap.size();
}

int AnnotatePhotoItem::type() const
{
    return Type;
}

bool AnnotatePhotoItem::isScaleHandle(const QPointF &itemPos) const
{
    const QRectF bounds = boundingRect();
    const QRectF grip(bounds.right() - 14, bounds.bottom() - 14, 14, 14);
    const bool hit = grip.contains(itemPos);
    qInfo() << "AnnotatePhotoItem: scale handle hit=" << hit << itemPos << grip;
    return hit;
}

qreal AnnotatePhotoItem::minScale() const
{
    const qreal shorter = qMin(pixmap().width(), pixmap().height());
    if (shorter <= 0) {
        return 0.1;
    }
    return qMax(0.05, 40.0 / shorter);
}

qreal AnnotatePhotoItem::maxScaleOnShot(const QRectF &shot) const
{
    const qreal pw = pixmap().width();
    const qreal ph = pixmap().height();
    if (pw <= 0 || ph <= 0) {
        return 1;
    }
    const qreal maxW = qMax(1.0, shot.right() - pos().x());
    const qreal maxH = qMax(1.0, shot.bottom() - pos().y());
    const qreal scale = qMin(maxW / pw, maxH / ph);
    qInfo() << "AnnotatePhotoItem: maxScale=" << scale << "shot=" << shot << "pos=" << pos();
    return qMax(minScale(), scale);
}

QRectF AnnotatePhotoItem::sceneBox() const
{
    const qreal s = photoScale();
    return QRectF(pos(), QSizeF(pixmap().width() * s, pixmap().height() * s));
}

void AnnotatePhotoItem::setPhotoScale(qreal scale)
{
    const qreal next = qMax(0.01, scale);
    setScale(next);
    qInfo() << "AnnotatePhotoItem: scale=" << next;
}

qreal AnnotatePhotoItem::photoScale() const
{
    return scale();
}

void AnnotatePhotoItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    QGraphicsPixmapItem::paint(painter, option, widget);
    const QRectF bounds = boundingRect();
    const QRectF grip(bounds.right() - 10, bounds.bottom() - 10, 10, 10);
    painter->fillRect(grip, QColor(40, 40, 40, 180));
}
