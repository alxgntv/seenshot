#include "annotate/AnnotateItems.h"

#include <QAbstractTextDocumentLayout>
#include <QBrush>
#include <QDebug>
#include <QVariant>
#include <QFont>
#include <QFontMetrics>
#include <QLineF>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QPixmap>
#include <QElapsedTimer>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QStyleOptionGraphicsItem>
#include <algorithm>

#include <Accelerate/Accelerate.h>

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

qreal stepBadgeRadiusFor(const QGraphicsRectItem *rect)
{
    if (!rect) {
        return 14.0;
    }
    for (QGraphicsItem *child : rect->childItems()) {
        if (auto *badge = qgraphicsitem_cast<StepBadgeItem *>(child)) {
            return badge->badgeRadius();
        }
    }
    return 14.0;
}

bool stepBadgeFitsShot(const QGraphicsRectItem *rect, const QPointF &localCenter, const QRectF &shot)
{
    if (!rect) {
        qWarning() << "stepBadgeFitsShot: null rect";
        return false;
    }
    if (shot.isEmpty()) {
        qWarning() << "stepBadgeFitsShot: empty shot, fit=false local=" << localCenter;
        return false;
    }
    const qreal radius = stepBadgeRadiusFor(rect);
    const QRectF local(localCenter.x() - radius, localCenter.y() - radius, radius * 2.0, radius * 2.0);
    const QRectF sceneBadge = rect->mapRectToScene(local);
    const bool ok = shot.contains(sceneBadge);
    qInfo() << "stepBadgeFitsShot: local=" << localCenter << "scene=" << sceneBadge << "shot=" << shot
            << "fit=" << ok;
    return ok;
}

QPointF clampStepBadgeLocal(const QGraphicsRectItem *rect, QPointF localCenter, const QRectF &shot)
{
    if (!rect) {
        qWarning() << "clampStepBadgeLocal: null rect";
        return localCenter;
    }
    if (shot.isEmpty()) {
        qWarning() << "clampStepBadgeLocal: empty shot, keep local=" << localCenter;
        return localCenter;
    }
    const qreal radius = stepBadgeRadiusFor(rect);
    const QRectF local(localCenter.x() - radius, localCenter.y() - radius, radius * 2.0, radius * 2.0);
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
    qInfo() << "clampStepBadgeLocal: from=" << localCenter << "to=" << clamped << "dx=" << dx << "dy=" << dy
            << "shot=" << shot << "sceneBadge=" << sceneBadge;
    return clamped;
}

QPointF outsideStepBadgeCenter(const QRectF &box, int corner, qreal radius)
{
    switch (corner) {
    case 0:
        return box.topLeft() + QPointF(-radius, -radius);
    case 1:
        return QPointF(box.right(), box.top()) + QPointF(radius, -radius);
    case 2:
        return QPointF(box.left(), box.bottom()) + QPointF(-radius, radius);
    default:
        return box.bottomRight() + QPointF(radius, radius);
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
    if (shot.isEmpty()) {
        qWarning() << "placeHighlightStepBadge: empty shot, skip clamp-to-origin box=" << rect->rect();
    }
    qInfo() << "placeHighlightStepBadge: shot=" << shot << "radius=" << stepBadgeRadiusFor(rect)
            << "cursor=" << cursorScene << "onScene=" << (rect->scene() != nullptr);
    const QRectF box = rect->rect();
    const qreal radius = stepBadgeRadiusFor(rect);
    int order[4] = {0, 1, 2, 3};
    qreal dist[4];
    for (int i = 0; i < 4; ++i) {
        dist[i] = QLineF(rect->mapToScene(stepBoxCorner(box, i)), cursorScene).length();
        qInfo() << "placeHighlightStepBadge: corner=" << i << "dist=" << dist[i];
    }
    std::sort(order, order + 4, [&](int a, int b) {
        return dist[a] < dist[b];
    });
    QPointF chosen = outsideStepBadgeCenter(box, order[0], radius);
    bool fitted = false;
    for (int i = 0; i < 4; ++i) {
        const QPointF candidate = outsideStepBadgeCenter(box, order[i], radius);
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
    const qreal radius = stepBadgeRadiusFor(rect);
    const QPointF candidates[] = {
        box.topLeft() + QPointF(-radius, -radius),
        QPointF(box.right(), box.top()) + QPointF(radius, -radius),
        QPointF(box.left(), box.bottom()) + QPointF(-radius, radius),
        box.bottomRight() + QPointF(radius, radius),
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

// ─── Ariadne's Thread [AT-0339] ─────────────────────
// What: Blur uses vImageBoxConvolve_ARGB8888 instead of QImage::pixel
// Why:  Slider valueChanged re-blurs the patch; pixel/setPixel froze the editor
// Date: 2026-08-28
// Related: [AT-0330] AnnotateWindow.cpp:m_blurSlider, [AT-0078] AnnotateItems.cpp:boxBlur
// ─────────────────────────────────────────────────────
QImage boxBlur(const QImage &patch, int radius)
{
    if (patch.isNull()) {
        qWarning() << "boxBlur: empty patch";
        return {};
    }
    const int r = qBound(0, radius, 20);
    if (r == 0) {
        qInfo() << "boxBlur: radius 0, copy" << patch.size() << "format=" << patch.format();
        return patch;
    }
    QImage src = patch.convertToFormat(QImage::Format_ARGB32);
    if (src.isNull() || src.width() < 1 || src.height() < 1) {
        qWarning() << "boxBlur: ARGB32 convert failed size=" << patch.size() << "format=" << patch.format();
        return patch;
    }
    QImage out(src.size(), QImage::Format_ARGB32);
    if (out.isNull()) {
        qWarning() << "boxBlur: dest alloc failed size=" << src.size();
        return patch;
    }
    vImage_Buffer srcBuf{};
    srcBuf.data = src.bits();
    srcBuf.height = static_cast<vImagePixelCount>(src.height());
    srcBuf.width = static_cast<vImagePixelCount>(src.width());
    srcBuf.rowBytes = static_cast<size_t>(src.bytesPerLine());
    vImage_Buffer destBuf{};
    destBuf.data = out.bits();
    destBuf.height = static_cast<vImagePixelCount>(out.height());
    destBuf.width = static_cast<vImagePixelCount>(out.width());
    destBuf.rowBytes = static_cast<size_t>(out.bytesPerLine());
    const uint32_t kernel = static_cast<uint32_t>(2 * r + 1);
    Pixel_8888 background = {0, 0, 0, 0};
    QElapsedTimer timer;
    timer.start();
    const vImage_Error err = vImageBoxConvolve_ARGB8888(&srcBuf, &destBuf, nullptr, 0, 0, kernel, kernel,
                                                          background, kvImageEdgeExtend);
    const qint64 ms = timer.elapsed();
    if (err != kvImageNoError) {
        qWarning() << "boxBlur: vImageBoxConvolve_ARGB8888 err=" << static_cast<long>(err)
                   << "radius=" << r << "kernel=" << kernel << "size=" << src.size()
                   << "srcRow=" << srcBuf.rowBytes << "destRow=" << destBuf.rowBytes << "ms=" << ms;
        return patch;
    }
    qInfo() << "boxBlur: vImage radius=" << r << "kernel=" << kernel << "size=" << src.size()
            << "srcRow=" << srcBuf.rowBytes << "destRow=" << destBuf.rowBytes << "ms=" << ms;
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

// ─── Ariadne's Thread [AT-0161] ─────────────────────
// What: QTextDocument wrap is WrapAtWordBoundaryOrAnywhere on every text block
// Why:  Default WordWrap will not shrink width below the longest unbreakable token
// Date: 2026-08-26
// Related: [AT-0156] AnnotateWindow.cpp:applySelectResize, [AT-0040] AnnotateItems.h:AnnotateTextItem
// ─────────────────────────────────────────────────────
AnnotateTextItem::AnnotateTextItem(const QString &text)
    : QGraphicsTextItem(text)
{
    setAnnotateKind(this, AnnotateKind::Text);
    setTextInteractionFlags(Qt::NoTextInteraction);
    QTextDocument *doc = document();
    if (!doc) {
        qWarning() << "AnnotateTextItem: created without document";
    } else {
        QTextOption opt = doc->defaultTextOption();
        opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        doc->setDefaultTextOption(opt);
        qInfo() << "AnnotateTextItem: wrap mode=" << opt.wrapMode();
    }
    setTextWidth(160);
    qInfo() << "AnnotateTextItem: created width=" << textWidth()
            << "docW=" << (doc ? doc->size().width() : -1);
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

// ─── Ariadne's Thread [AT-0348] ─────────────────────
// What: Extra box height so Bottom and corner handles can stretch the text frame
// Why:  QGraphicsTextItem height followed the document only; down/diagonal did nothing
// Date: 2026-08-28
// Related: [AT-0156] AnnotateWindow.cpp:applySelectResize, [AT-0040] AnnotateItems.h:AnnotateTextItem
// ─────────────────────────────────────────────────────
qreal AnnotateTextItem::minTextHeight() const
{
    QRectF box = QGraphicsTextItem::boundingRect();
    if (m_textOutline) {
        const qreal pad = stickerStrokeWidth() * 0.5 + 1.0;
        box.adjust(-pad, -pad, pad, pad);
    }
    const qreal height = qMax(16.0, box.height());
    qInfo() << "AnnotateTextItem: minTextHeight=" << height << "outline=" << m_textOutline;
    return height;
}

void AnnotateTextItem::setBoxHeight(qreal height)
{
    const qreal minH = minTextHeight();
    const qreal next = qMax(height, minH);
    if (qFuzzyCompare(m_boxHeight + 1.0, next + 1.0)) {
        qInfo() << "AnnotateTextItem: setBoxHeight unchanged=" << next << "min=" << minH;
        return;
    }
    prepareGeometryChange();
    m_boxHeight = next;
    setData(kAnnotateRoleTextHeight, m_boxHeight);
    qInfo() << "AnnotateTextItem: setBoxHeight=" << m_boxHeight << "min=" << minH;
}

qreal AnnotateTextItem::boxHeight() const
{
    return qMax(m_boxHeight, minTextHeight());
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

void AnnotateTextItem::beginEdit(const QPointF &itemPos)
{
    setTextInteractionFlags(Qt::TextEditorInteraction);
    setFocus(Qt::MouseFocusReason);
    QTextCursor cursor = textCursor();
    cursor.clearSelection();
    int hit = -1;
    QTextDocument *doc = document();
    QAbstractTextDocumentLayout *layout = doc ? doc->documentLayout() : nullptr;
    if (layout) {
        hit = layout->hitTest(itemPos, Qt::FuzzyHit);
        qInfo() << "AnnotateTextItem: beginEdit hitTest=" << hit << "itemPos=" << itemPos
                << "docSize=" << doc->size();
    } else {
        qWarning() << "AnnotateTextItem: beginEdit no document layout";
    }
    if (hit >= 0) {
        cursor.setPosition(hit);
    } else {
        cursor.movePosition(QTextCursor::End);
    }
    setTextCursor(cursor);
    qInfo() << "AnnotateTextItem: beginEdit cursor=" << textCursor().position()
            << "hasFocus=" << hasFocus() << "flags=" << int(textInteractionFlags());
}

void AnnotateTextItem::endEdit()
{
    // ─── Ariadne's Thread [AT-0314] ─────────────────────
    // What: Clear QTextCursor selection when leaving in-place edit
    // Why:  NoTextInteraction still painted the gray selection on the old block
    // Date: 2026-08-28
    // Related: [AT-0040] AnnotateItems.h:AnnotateTextItem, [AT-0111] AnnotateWindow.cpp:commitTextEdit
    // ─────────────────────────────────────────────────────
    QTextCursor cursor = textCursor();
    const bool hadSelection = cursor.hasSelection();
    const int selStart = cursor.selectionStart();
    const int selEnd = cursor.selectionEnd();
    cursor.clearSelection();
    cursor.movePosition(QTextCursor::End);
    setTextCursor(cursor);
    setTextInteractionFlags(Qt::NoTextInteraction);
    clearFocus();
    update();
    qInfo() << "AnnotateTextItem: endEdit text=" << toPlainText()
            << "hadSelection=" << hadSelection << "selStart=" << selStart << "selEnd=" << selEnd
            << "width=" << textWidth();
}

void AnnotateTextItem::setTextSize(int size)
{
    const int next = qBound(10, size, 48);
    if (m_textSize == next) {
        qInfo() << "AnnotateTextItem: setTextSize unchanged=" << next;
        return;
    }
    m_textSize = next;
    setData(kAnnotateRoleTextSize, m_textSize);
    applyTextStyle();
    qInfo() << "AnnotateTextItem: setTextSize=" << m_textSize;
}

int AnnotateTextItem::textSize() const
{
    return m_textSize;
}

void AnnotateTextItem::setTextOutline(bool on)
{
    if (m_textOutline == on) {
        qInfo() << "AnnotateTextItem: setTextOutline unchanged=" << on;
        return;
    }
    prepareGeometryChange();
    m_textOutline = on;
    setData(kAnnotateRoleTextOutline, m_textOutline);
    applyTextStyle();
    qInfo() << "AnnotateTextItem: setTextOutline=" << m_textOutline
            << "stroke=" << stickerStrokeWidth();
}

bool AnnotateTextItem::textOutline() const
{
    return m_textOutline;
}

qreal AnnotateTextItem::stickerStrokeWidth() const
{
    return qMax(4.0, static_cast<qreal>(m_textSize) * 0.32);
}

QPainterPath AnnotateTextItem::stickerGlyphPath() const
{
    QPainterPath path;
    QTextDocument *doc = document();
    if (!doc) {
        qWarning() << "AnnotateTextItem: stickerGlyphPath no document";
        return path;
    }
    QAbstractTextDocumentLayout *layout = doc->documentLayout();
    if (!layout) {
        qWarning() << "AnnotateTextItem: stickerGlyphPath no layout";
        return path;
    }
    const QFont f = font();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        QTextLayout *textLayout = block.layout();
        if (!textLayout) {
            continue;
        }
        const QRectF blockRect = layout->blockBoundingRect(block);
        const int lineCount = textLayout->lineCount();
        for (int i = 0; i < lineCount; ++i) {
            const QTextLine line = textLayout->lineAt(i);
            QString slice = block.text().mid(line.textStart(), line.textLength());
            slice.remove(QLatin1Char('\n'));
            slice.remove(QLatin1Char('\r'));
            if (slice.isEmpty()) {
                continue;
            }
            const QPointF baseline(blockRect.left() + line.x(),
                                   blockRect.top() + line.y() + line.ascent());
            path.addText(baseline, f, slice);
        }
    }
    return path;
}

// ─── Ariadne's Thread [AT-0350] ─────────────────────
// What: Grow boundingRect to the extra box height from Bottom/corner stretch
// Why:  Handles and hit-tests sat on the document box so down-drag had no frame
// Date: 2026-08-28
// Related: [AT-0348] AnnotateItems.cpp:setBoxHeight, [AT-0156] AnnotateWindow.cpp:applySelectResize
// ─────────────────────────────────────────────────────
QRectF AnnotateTextItem::boundingRect() const
{
    QRectF box = QGraphicsTextItem::boundingRect();
    if (m_textOutline) {
        const qreal pad = stickerStrokeWidth() * 0.5 + 1.0;
        box.adjust(-pad, -pad, pad, pad);
    }
    if (m_boxHeight > box.height()) {
        box.setHeight(m_boxHeight);
    }
    return box;
}

void AnnotateTextItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    if (m_textOutline && painter) {
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QPainterPath glyphs = stickerGlyphPath();
        if (!glyphs.isEmpty()) {
            const qreal width = stickerStrokeWidth();
            QPen pen(contrastInk(defaultTextColor()), width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter->strokePath(glyphs, pen);
        }
    }
    QGraphicsTextItem::paint(painter, option, widget);
}

// ─── Ariadne's Thread [AT-0148] ─────────────────────
// What: Apply pixel size and QTextCharFormat outline to the whole document
// Why:  Size slider and Outline checkbox must update typed runs, not only defaultFont
// Date: 2026-08-26
// Related: [AT-0113] AnnotationCommands.cpp:applyTextItemColor, [AT-0040] AnnotateItems.h
// ─────────────────────────────────────────────────────
// ─── Ariadne's Thread [AT-0313] ─────────────────────
// What: Drop QTextCharFormat outline; sticker halo is stroke-under-fill in paint
// Why:  Qt text outline stroked each glyph inward and did not merge into one blob
// Date: 2026-08-28
// Related: [AT-0148] AnnotateItems.cpp:applyTextStyle, [AT-0040] AnnotateItems.h:AnnotateTextItem
// ─────────────────────────────────────────────────────
void AnnotateTextItem::applyTextStyle()
{
    if (m_textOutline) {
        prepareGeometryChange();
    }
    QFont next = font();
    next.setPixelSize(m_textSize);
    setFont(next);
    QTextDocument *doc = document();
    const bool undoOn = doc && doc->isUndoRedoEnabled();
    if (doc) {
        doc->setUndoRedoEnabled(false);
    }
    const QTextCursor keep = textCursor();
    QTextCharFormat fmt;
    fmt.setFont(next);
    fmt.setForeground(QBrush(defaultTextColor()));
    fmt.setTextOutline(QPen(Qt::NoPen));
    if (doc) {
        QTextCursor all(doc);
        all.select(QTextCursor::Document);
        all.mergeCharFormat(fmt);
    }
    setTextCursor(keep);
    if (doc) {
        doc->setUndoRedoEnabled(undoOn);
    }
    update();
    if (m_boxHeight > 0) {
        const qreal minH = minTextHeight();
        if (m_boxHeight < minH) {
            prepareGeometryChange();
            m_boxHeight = minH;
            setData(kAnnotateRoleTextHeight, m_boxHeight);
            qInfo() << "AnnotateTextItem: applyTextStyle grew boxHeight=" << m_boxHeight;
        }
    }
    qInfo() << "AnnotateTextItem: applyTextStyle size=" << m_textSize << "outline=" << m_textOutline
            << "color=" << defaultTextColor() << "stroke=" << stickerStrokeWidth()
            << "boxHeight=" << m_boxHeight;
}

// ─── Ariadne's Thread [AT-0160] ─────────────────────
// What: Stop painting the always-on 6x16 gray wrap grip on committed text
// Why:  After endEdit the grip sat to the right of the glyphs; Select handles wrap width
// Date: 2026-08-26
// Related: [AT-0156] AnnotateWindow.cpp:paintSelectHandles, [AT-0040] AnnotateItems.h:AnnotateTextItem
// ─────────────────────────────────────────────────────

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

void StepBadgeItem::setDigitSize(int size)
{
    const int next = qBound(10, size, 48);
    if (m_digitSize == next) {
        qInfo() << "StepBadgeItem: setDigitSize unchanged=" << next;
        return;
    }
    m_digitSize = next;
    setData(kAnnotateRoleTextSize, m_digitSize);
    const qreal radius = static_cast<qreal>(m_digitSize);
    setRect(QRectF(-radius, -radius, radius * 2.0, radius * 2.0));
    update();
    qInfo() << "StepBadgeItem: setDigitSize=" << m_digitSize << "radius=" << radius;
}

int StepBadgeItem::digitSize() const
{
    return m_digitSize;
}

void StepBadgeItem::setTextOutline(bool on)
{
    if (m_textOutline == on) {
        qInfo() << "StepBadgeItem: setTextOutline unchanged=" << on;
        return;
    }
    m_textOutline = on;
    setData(kAnnotateRoleTextOutline, m_textOutline);
    update();
    qInfo() << "StepBadgeItem: setTextOutline=" << m_textOutline;
}

bool StepBadgeItem::textOutline() const
{
    return m_textOutline;
}

qreal StepBadgeItem::badgeRadius() const
{
    return rect().width() / 2.0;
}

void StepBadgeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    QGraphicsRectItem::paint(painter, option, widget);
    const QString label = QString::number(m_number);
    QFont font = painter->font();
    font.setBold(true);
    font.setPixelSize(m_digitSize);
    painter->setFont(font);
    const QFontMetrics metrics(font);
    const QRectF box = rect();
    const QRectF textBox = metrics.boundingRect(label);
    const qreal x = box.center().x() - textBox.width() / 2.0 - textBox.left();
    const qreal y = box.center().y() + (metrics.ascent() - metrics.descent()) / 2.0;
    QPainterPath path;
    path.addText(QPointF(x, y), font, label);
    if (m_textOutline) {
        const qreal width = qMax(4.0, static_cast<qreal>(m_digitSize) * 0.32);
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->strokePath(path, QPen(contrastInk(m_ink), width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
    painter->fillPath(path, m_ink);
}

namespace {

QGraphicsRectItem *stepsRectOf(QGraphicsItem *item)
{
    if (!item) {
        return nullptr;
    }
    if (annotateKind(item) == AnnotateKind::Highlight && highlightStyle(item) == HighlightStyle::Steps) {
        return qgraphicsitem_cast<QGraphicsRectItem *>(item);
    }
    if (auto *badge = qgraphicsitem_cast<StepBadgeItem *>(item)) {
        return qgraphicsitem_cast<QGraphicsRectItem *>(badge->parentItem());
    }
    return nullptr;
}

// ─── Ariadne's Thread [AT-0151] ─────────────────────
// What: Resolve Steps fit/clamp against the screenshot pixmap, never padded sceneRect
// Why:  Empty shot (item off scene) clamped badges to origin; canvas pad sat outside the shot
// Date: 2026-08-26
// Related: [AT-0134] AnnotateItems.cpp:placeHighlightStepBadge, [AT-0056] AnnotateWindow.cpp:applyCanvasChrome
// ─────────────────────────────────────────────────────
QRectF shotRectOfItem(const QGraphicsItem *item)
{
    if (!item) {
        qWarning() << "shotRectOfItem: null item";
        return QRectF();
    }
    QGraphicsScene *scene = item->scene();
    if (!scene) {
        qWarning() << "shotRectOfItem: item not on scene";
        return QRectF();
    }
    const auto items = scene->items();
    for (QGraphicsItem *it : items) {
        auto *photo = qgraphicsitem_cast<ShotPhotoItem *>(it);
        if (!photo) {
            continue;
        }
        const QRectF shot = photo->mapRectToScene(photo->boundingRect());
        qInfo() << "shotRectOfItem: shot=" << shot << "bounds=" << photo->boundingRect()
                << "pixmap=" << photo->pixmap().size() << "sceneRect=" << scene->sceneRect();
        return shot;
    }
    qWarning() << "shotRectOfItem: no ShotPhotoItem sceneRect=" << scene->sceneRect();
    return QRectF();
}

void relayoutStepsBadge(QGraphicsRectItem *rect)
{
    if (!rect) {
        qWarning() << "relayoutStepsBadge: null rect";
        return;
    }
    const QRectF shot = shotRectOfItem(rect);
    if (shot.isEmpty()) {
        qWarning() << "relayoutStepsBadge: skip empty shot hasScene=" << (rect->scene() != nullptr)
                   << "box=" << rect->rect();
        return;
    }
    QPointF cursor = rect->mapToScene(rect->rect().center());
    for (QGraphicsItem *child : rect->childItems()) {
        if (auto *badge = qgraphicsitem_cast<StepBadgeItem *>(child)) {
            cursor = badge->mapToScene(badge->rect().center());
            break;
        }
    }
    placeHighlightStepBadge(rect, shot, cursor);
    qInfo() << "relayoutStepsBadge: cursor=" << cursor << "shot=" << shot << "box=" << rect->rect();
}

} // namespace

int annotateTextSize(const QGraphicsItem *item)
{
    if (!item) {
        return 18;
    }
    QGraphicsItem *live = const_cast<QGraphicsItem *>(item);
    if (auto *text = qgraphicsitem_cast<AnnotateTextItem *>(live)) {
        return text->textSize();
    }
    if (auto *badge = qgraphicsitem_cast<StepBadgeItem *>(live)) {
        return badge->digitSize();
    }
    if (annotateKind(item) == AnnotateKind::Highlight && highlightStyle(item) == HighlightStyle::Steps) {
        for (QGraphicsItem *child : live->childItems()) {
            if (auto *badge = qgraphicsitem_cast<StepBadgeItem *>(child)) {
                return badge->digitSize();
            }
        }
    }
    return 18;
}

bool annotateTextOutline(const QGraphicsItem *item)
{
    if (!item) {
        return false;
    }
    QGraphicsItem *live = const_cast<QGraphicsItem *>(item);
    if (auto *text = qgraphicsitem_cast<AnnotateTextItem *>(live)) {
        return text->textOutline();
    }
    if (auto *badge = qgraphicsitem_cast<StepBadgeItem *>(live)) {
        return badge->textOutline();
    }
    if (annotateKind(item) == AnnotateKind::Highlight && highlightStyle(item) == HighlightStyle::Steps) {
        for (QGraphicsItem *child : live->childItems()) {
            if (auto *badge = qgraphicsitem_cast<StepBadgeItem *>(child)) {
                return badge->textOutline();
            }
        }
    }
    return false;
}

void applyAnnotateTextSize(QGraphicsItem *item, int size)
{
    const int next = qBound(10, size, 48);
    if (auto *text = qgraphicsitem_cast<AnnotateTextItem *>(item)) {
        text->setTextSize(next);
        qInfo() << "applyAnnotateTextSize: text size=" << next;
        return;
    }
    if (QGraphicsRectItem *rect = stepsRectOf(item)) {
        for (QGraphicsItem *child : rect->childItems()) {
            if (auto *badge = qgraphicsitem_cast<StepBadgeItem *>(child)) {
                badge->setDigitSize(next);
            }
        }
        relayoutStepsBadge(rect);
        qInfo() << "applyAnnotateTextSize: steps size=" << next << "onScene=" << (rect->scene() != nullptr)
                << "box=" << rect->rect();
        return;
    }
    qInfo() << "applyAnnotateTextSize: skip kind=" << (item ? static_cast<int>(annotateKind(item)) : -1);
}

void applyAnnotateTextOutline(QGraphicsItem *item, bool on)
{
    if (auto *text = qgraphicsitem_cast<AnnotateTextItem *>(item)) {
        text->setTextOutline(on);
        qInfo() << "applyAnnotateTextOutline: text on=" << on;
        return;
    }
    if (QGraphicsRectItem *rect = stepsRectOf(item)) {
        for (QGraphicsItem *child : rect->childItems()) {
            if (auto *badge = qgraphicsitem_cast<StepBadgeItem *>(child)) {
                badge->setTextOutline(on);
            }
        }
        qInfo() << "applyAnnotateTextOutline: steps on=" << on;
        return;
    }
    qInfo() << "applyAnnotateTextOutline: skip kind=" << (item ? static_cast<int>(annotateKind(item)) : -1);
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
    const qreal m = 10;
    const bool inside = bounds.adjusted(-m, -m, m, m).contains(itemPos);
    const bool interior = bounds.adjusted(m, m, -m, -m).contains(itemPos);
    const bool hit = inside && !interior;
    qInfo() << "AnnotatePhotoItem: scale handle hit=" << hit << itemPos << bounds;
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

