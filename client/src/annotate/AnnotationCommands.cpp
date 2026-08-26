#include "annotate/AnnotationCommands.h"

#include "annotate/AnnotateItems.h"

#include <QBrush>
#include <QDebug>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QPen>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextDocument>

namespace {

// ─── Ariadne's Thread [AT-0113] ─────────────────────
// What: Recolor default, document runs, and caret format
// Why:  setDefaultTextColor leaves already typed QTextCharFormat gray
// Date: 2026-08-26
// Related: [AT-0043] AnnotationCommands.cpp:applyItemColor, [AT-0115] AnnotateWindow.cpp:onHueChanged
// ─────────────────────────────────────────────────────
void applyTextItemColor(QGraphicsTextItem *text, const QColor &color)
{
    if (!text) {
        qWarning() << "applyTextItemColor: null text";
        return;
    }
    QTextDocument *doc = text->document();
    const bool undoOn = doc && doc->isUndoRedoEnabled();
    if (doc) {
        doc->setUndoRedoEnabled(false);
    }
    const QTextCursor keep = text->textCursor();
    text->setDefaultTextColor(color);
    QTextCharFormat fmt;
    fmt.setForeground(QBrush(color));
    if (doc) {
        QTextCursor all(doc);
        all.select(QTextCursor::Document);
        all.mergeCharFormat(fmt);
    }
    text->setTextCursor(keep);
    if (doc) {
        doc->setUndoRedoEnabled(undoOn);
    }
    qInfo() << "applyTextItemColor:" << color << "len=" << text->toPlainText().size()
            << "default=" << text->defaultTextColor();
}

} // namespace

// ─── Ariadne's Thread [AT-0043] ─────────────────────
// What: Color by annotate kind; skip blur; keep highlight style
// Why:  PRD-02 slider recolors last committed item, never blur
// Date: 2026-08-25
// Related: [AT-0011] AnnotationCommands.h, [AT-0040] AnnotateItems.h
// ─────────────────────────────────────────────────────
void applyItemColor(QGraphicsItem *item, const QColor &color)
{
    if (!item) {
        qWarning() << "applyItemColor: null item";
        return;
    }
    const AnnotateKind kind = annotateKind(item);
    qInfo() << "applyItemColor: kind=" << static_cast<int>(kind) << "color=" << color << "type=" << item->type();
    if (kind == AnnotateKind::Blur) {
        qInfo() << "applyItemColor: skip blur";
        return;
    }
    if (kind == AnnotateKind::Photo) {
        qInfo() << "applyItemColor: skip photo";
        return;
    }
    if (kind == AnnotateKind::Highlight) {
        auto *rect = static_cast<QGraphicsRectItem *>(item);
        applyHighlightAppearance(rect, color, highlightStyle(item));
        return;
    }
    if (kind == AnnotateKind::Step) {
        static_cast<StepBadgeItem *>(item)->applyColor(color);
        return;
    }
    if (kind == AnnotateKind::Text) {
        applyTextItemColor(static_cast<QGraphicsTextItem *>(item), color);
        return;
    }
    if (kind == AnnotateKind::Arrow || kind == AnnotateKind::Line) {
        if (auto *line = qgraphicsitem_cast<QGraphicsLineItem *>(item)) {
            line->setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            return;
        }
        if (auto *path = qgraphicsitem_cast<QGraphicsPathItem *>(item)) {
            path->setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            path->setBrush(color);
            return;
        }
    }
    if (auto *rect = qgraphicsitem_cast<QGraphicsRectItem *>(item)) {
        applyHighlightAppearance(rect, color, highlightStyle(item));
        return;
    }
    if (auto *line = qgraphicsitem_cast<QGraphicsLineItem *>(item)) {
        line->setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        return;
    }
    if (auto *path = qgraphicsitem_cast<QGraphicsPathItem *>(item)) {
        path->setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        path->setBrush(color);
        return;
    }
    if (auto *text = dynamic_cast<QGraphicsTextItem *>(item)) {
        applyTextItemColor(text, color);
        return;
    }
    qInfo() << "applyItemColor: unsupported item type" << item->type();
}

QColor itemColor(QGraphicsItem *item)
{
    if (!item) {
        return Qt::red;
    }
    const AnnotateKind kind = annotateKind(item);
    if (kind == AnnotateKind::Highlight || kind == AnnotateKind::Step) {
        if (auto *rect = dynamic_cast<QGraphicsRectItem *>(item)) {
            return rect->pen().color();
        }
    }
    if (kind == AnnotateKind::Text) {
        if (auto *text = dynamic_cast<QGraphicsTextItem *>(item)) {
            return text->defaultTextColor();
        }
    }
    if (auto *rect = qgraphicsitem_cast<QGraphicsRectItem *>(item)) {
        return rect->pen().color();
    }
    if (auto *line = qgraphicsitem_cast<QGraphicsLineItem *>(item)) {
        return line->pen().color();
    }
    if (auto *path = qgraphicsitem_cast<QGraphicsPathItem *>(item)) {
        return path->pen().color();
    }
    if (auto *text = dynamic_cast<QGraphicsTextItem *>(item)) {
        return text->defaultTextColor();
    }
    return Qt::red;
}

AddItemCommand::AddItemCommand(QGraphicsScene *scene, QGraphicsItem *item, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_scene(scene)
    , m_item(item)
{
    setText(QStringLiteral("Add annotation"));
    qInfo() << "AddItemCommand: construct kind=" << static_cast<int>(annotateKind(item));
}

AddItemCommand::~AddItemCommand()
{
    if (m_owns) {
        qInfo() << "AddItemCommand: delete owned item";
        delete m_item;
    }
}

void AddItemCommand::undo()
{
    qInfo() << "AddItemCommand: undo";
    if (m_scene && m_item) {
        m_scene->removeItem(m_item);
        m_owns = true;
        syncStepNumbers(m_scene);
    }
}

void AddItemCommand::redo()
{
    qInfo() << "AddItemCommand: redo";
    if (m_scene && m_item && m_item->scene() != m_scene) {
        m_scene->addItem(m_item);
        m_owns = false;
        syncStepNumbers(m_scene);
    }
}

RemoveItemCommand::RemoveItemCommand(QGraphicsScene *scene, QGraphicsItem *item, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_scene(scene)
    , m_item(item)
{
    setText(QStringLiteral("Remove annotation"));
    qInfo() << "RemoveItemCommand: construct kind=" << static_cast<int>(annotateKind(item));
}

RemoveItemCommand::~RemoveItemCommand()
{
    if (m_owns) {
        qInfo() << "RemoveItemCommand: delete owned item";
        delete m_item;
    }
}

void RemoveItemCommand::undo()
{
    qInfo() << "RemoveItemCommand: undo";
    if (m_scene && m_item && m_item->scene() != m_scene) {
        m_scene->addItem(m_item);
        m_owns = false;
        syncStepNumbers(m_scene);
    }
}

void RemoveItemCommand::redo()
{
    qInfo() << "RemoveItemCommand: redo";
    if (m_scene && m_item && m_item->scene() == m_scene) {
        m_scene->removeItem(m_item);
        m_owns = true;
        syncStepNumbers(m_scene);
    }
}

ChangeColorCommand::ChangeColorCommand(QGraphicsItem *item, const QColor &color, int gestureId, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_item(item)
    , m_old(itemColor(item))
    , m_new(color)
    , m_gestureId(gestureId)
{
    setText(QStringLiteral("Change color"));
    qInfo() << "ChangeColorCommand: construct old=" << m_old << "new=" << m_new << "gesture=" << m_gestureId;
}

void ChangeColorCommand::undo()
{
    qInfo() << "ChangeColorCommand: undo" << m_old;
    applyItemColor(m_item, m_old);
}

void ChangeColorCommand::redo()
{
    qInfo() << "ChangeColorCommand: redo" << m_new;
    applyItemColor(m_item, m_new);
}

int ChangeColorCommand::id() const
{
    return 4101;
}

bool ChangeColorCommand::mergeWith(const QUndoCommand *other)
{
    const auto *next = dynamic_cast<const ChangeColorCommand *>(other);
    if (!next || next->m_item != m_item || next->m_gestureId != m_gestureId) {
        qInfo() << "ChangeColorCommand: merge rejected";
        return false;
    }
    m_new = next->m_new;
    qInfo() << "ChangeColorCommand: merge new=" << m_new << "gesture=" << m_gestureId;
    return true;
}

static void refreshHighlightLook(QGraphicsItem *item)
{
    if (!item || annotateKind(item) != AnnotateKind::Highlight) {
        qWarning() << "refreshHighlightLook: not a highlight";
        return;
    }
    applyHighlightAppearance(static_cast<QGraphicsRectItem *>(item), itemColor(item), highlightStyle(item));
}

ChangeStrokeWidthCommand::ChangeStrokeWidthCommand(QGraphicsItem *item, int width, int gestureId, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_item(item)
    , m_old(highlightStrokeWidth(item))
    , m_new(width)
    , m_gestureId(gestureId)
{
    setText(QStringLiteral("Change stroke"));
    qInfo() << "ChangeStrokeWidthCommand: construct old=" << m_old << "new=" << m_new << "gesture=" << m_gestureId;
}

void ChangeStrokeWidthCommand::undo()
{
    qInfo() << "ChangeStrokeWidthCommand: undo" << m_old;
    setHighlightStrokeWidth(m_item, m_old);
    refreshHighlightLook(m_item);
}

void ChangeStrokeWidthCommand::redo()
{
    qInfo() << "ChangeStrokeWidthCommand: redo" << m_new;
    setHighlightStrokeWidth(m_item, m_new);
    refreshHighlightLook(m_item);
}

int ChangeStrokeWidthCommand::id() const
{
    return 4102;
}

bool ChangeStrokeWidthCommand::mergeWith(const QUndoCommand *other)
{
    const auto *next = dynamic_cast<const ChangeStrokeWidthCommand *>(other);
    if (!next || next->m_item != m_item || next->m_gestureId != m_gestureId) {
        qInfo() << "ChangeStrokeWidthCommand: merge rejected";
        return false;
    }
    m_new = next->m_new;
    qInfo() << "ChangeStrokeWidthCommand: merge new=" << m_new << "gesture=" << m_gestureId;
    return true;
}

ChangeFillAlphaCommand::ChangeFillAlphaCommand(QGraphicsItem *item, int alpha, int gestureId, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_item(item)
    , m_old(highlightFillAlpha(item))
    , m_new(alpha)
    , m_gestureId(gestureId)
{
    setText(QStringLiteral("Change fill"));
    qInfo() << "ChangeFillAlphaCommand: construct old=" << m_old << "new=" << m_new << "gesture=" << m_gestureId;
}

void ChangeFillAlphaCommand::undo()
{
    qInfo() << "ChangeFillAlphaCommand: undo" << m_old;
    setHighlightFillAlpha(m_item, m_old);
    refreshHighlightLook(m_item);
}

void ChangeFillAlphaCommand::redo()
{
    qInfo() << "ChangeFillAlphaCommand: redo" << m_new;
    setHighlightFillAlpha(m_item, m_new);
    refreshHighlightLook(m_item);
}

int ChangeFillAlphaCommand::id() const
{
    return 4103;
}

bool ChangeFillAlphaCommand::mergeWith(const QUndoCommand *other)
{
    const auto *next = dynamic_cast<const ChangeFillAlphaCommand *>(other);
    if (!next || next->m_item != m_item || next->m_gestureId != m_gestureId) {
        qInfo() << "ChangeFillAlphaCommand: merge rejected";
        return false;
    }
    m_new = next->m_new;
    qInfo() << "ChangeFillAlphaCommand: merge new=" << m_new << "gesture=" << m_gestureId;
    return true;
}

ChangeBlurRadiusCommand::ChangeBlurRadiusCommand(QGraphicsItem *item, int radius, int gestureId, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_item(item)
    , m_old(blurRadius(item))
    , m_new(radius)
    , m_gestureId(gestureId)
{
    setText(QStringLiteral("Change blur"));
    qInfo() << "ChangeBlurRadiusCommand: construct old=" << m_old << "new=" << m_new << "gesture=" << m_gestureId;
}

void ChangeBlurRadiusCommand::undo()
{
    qInfo() << "ChangeBlurRadiusCommand: undo" << m_old;
    applyBlurRadius(m_item, m_old);
}

void ChangeBlurRadiusCommand::redo()
{
    qInfo() << "ChangeBlurRadiusCommand: redo" << m_new;
    applyBlurRadius(m_item, m_new);
}

int ChangeBlurRadiusCommand::id() const
{
    return 4104;
}

bool ChangeBlurRadiusCommand::mergeWith(const QUndoCommand *other)
{
    const auto *next = dynamic_cast<const ChangeBlurRadiusCommand *>(other);
    if (!next || next->m_item != m_item || next->m_gestureId != m_gestureId) {
        qInfo() << "ChangeBlurRadiusCommand: merge rejected";
        return false;
    }
    m_new = next->m_new;
    qInfo() << "ChangeBlurRadiusCommand: merge new=" << m_new << "gesture=" << m_gestureId;
    return true;
}

ChangeTextCommand::ChangeTextCommand(QGraphicsTextItem *item, const QString &oldText, const QString &newText,
                                     QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_item(item)
    , m_old(oldText)
    , m_new(newText)
{
    setText(QStringLiteral("Edit text"));
    qInfo() << "ChangeTextCommand: construct oldLen=" << m_old.size() << "newLen=" << m_new.size();
}

void ChangeTextCommand::undo()
{
    qInfo() << "ChangeTextCommand: undo";
    if (m_item) {
        m_item->setPlainText(m_old);
    }
}

void ChangeTextCommand::redo()
{
    qInfo() << "ChangeTextCommand: redo";
    if (m_item) {
        m_item->setPlainText(m_new);
    }
}

ChangeTextWidthCommand::ChangeTextWidthCommand(QGraphicsTextItem *item, qreal oldWidth, qreal newWidth,
                                               QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_item(item)
    , m_old(oldWidth)
    , m_new(newWidth)
{
    setText(QStringLiteral("Resize text"));
    qInfo() << "ChangeTextWidthCommand: construct" << m_old << "->" << m_new;
}

void ChangeTextWidthCommand::undo()
{
    qInfo() << "ChangeTextWidthCommand: undo" << m_old;
    if (m_item) {
        m_item->setTextWidth(m_old);
    }
}

void ChangeTextWidthCommand::redo()
{
    qInfo() << "ChangeTextWidthCommand: redo" << m_new;
    if (m_item) {
        m_item->setTextWidth(m_new);
    }
}

ChangePhotoPosCommand::ChangePhotoPosCommand(QGraphicsItem *item, const QPointF &oldPos, const QPointF &newPos,
                                             int gestureId, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_item(item)
    , m_old(oldPos)
    , m_new(newPos)
    , m_gestureId(gestureId)
{
    setText(QStringLiteral("Move annotation"));
    qInfo() << "ChangePhotoPosCommand: construct old=" << m_old << "new=" << m_new << "gesture=" << m_gestureId;
}

void ChangePhotoPosCommand::undo()
{
    qInfo() << "ChangePhotoPosCommand: undo" << m_old;
    if (m_item) {
        m_item->setPos(m_old);
    }
}

void ChangePhotoPosCommand::redo()
{
    qInfo() << "ChangePhotoPosCommand: redo" << m_new;
    if (m_item) {
        m_item->setPos(m_new);
    }
}

int ChangePhotoPosCommand::id() const
{
    return 4105;
}

bool ChangePhotoPosCommand::mergeWith(const QUndoCommand *other)
{
    const auto *next = dynamic_cast<const ChangePhotoPosCommand *>(other);
    if (!next || next->m_item != m_item || next->m_gestureId != m_gestureId) {
        qInfo() << "ChangePhotoPosCommand: merge rejected";
        return false;
    }
    m_new = next->m_new;
    qInfo() << "ChangePhotoPosCommand: merge new=" << m_new << "gesture=" << m_gestureId;
    return true;
}

ChangePhotoScaleCommand::ChangePhotoScaleCommand(QGraphicsItem *item, qreal oldScale, qreal newScale, int gestureId,
                                                 QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_item(item)
    , m_old(oldScale)
    , m_new(newScale)
    , m_gestureId(gestureId)
{
    setText(QStringLiteral("Scale photo"));
    qInfo() << "ChangePhotoScaleCommand: construct old=" << m_old << "new=" << m_new << "gesture=" << m_gestureId;
}

void ChangePhotoScaleCommand::undo()
{
    qInfo() << "ChangePhotoScaleCommand: undo" << m_old;
    if (m_item) {
        m_item->setScale(m_old);
    }
}

void ChangePhotoScaleCommand::redo()
{
    qInfo() << "ChangePhotoScaleCommand: redo" << m_new;
    if (m_item) {
        m_item->setScale(m_new);
    }
}

int ChangePhotoScaleCommand::id() const
{
    return 4106;
}

bool ChangePhotoScaleCommand::mergeWith(const QUndoCommand *other)
{
    const auto *next = dynamic_cast<const ChangePhotoScaleCommand *>(other);
    if (!next || next->m_item != m_item || next->m_gestureId != m_gestureId) {
        qInfo() << "ChangePhotoScaleCommand: merge rejected";
        return false;
    }
    m_new = next->m_new;
    qInfo() << "ChangePhotoScaleCommand: merge new=" << m_new << "gesture=" << m_gestureId;
    return true;
}

// ─── Ariadne's Thread [AT-0072] ─────────────────────
// What: Undo for Select resize of highlight, line/arrow, blur box
// Why:  Select only selected items; geometry had no QUndoCommand
// Date: 2026-08-25
// Related: [AT-0072] AnnotateItems.cpp:layoutHighlightChrome, [AT-0011] AnnotationCommands.h
// ─────────────────────────────────────────────────────
static void applyEndpoints(QGraphicsItem *item, const QPointF &p1, const QPointF &p2)
{
    if (!item) {
        qWarning() << "applyEndpoints: null item";
        return;
    }
    if (auto *line = qgraphicsitem_cast<QGraphicsLineItem *>(item)) {
        line->setLine(QLineF(p1, p2));
        setAnnotateEndpoints(item, p1, p2);
        qInfo() << "applyEndpoints: line p1=" << p1 << "p2=" << p2;
        return;
    }
    if (auto *path = qgraphicsitem_cast<QGraphicsPathItem *>(item)) {
        setArrowEndpoints(path, p1, p2);
        qInfo() << "applyEndpoints: arrow p1=" << p1 << "p2=" << p2;
        return;
    }
    qWarning() << "applyEndpoints: unsupported type=" << item->type();
}

ChangeRectCommand::ChangeRectCommand(QGraphicsRectItem *item, const QRectF &oldRect, const QRectF &newRect,
                                     QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_item(item)
    , m_old(oldRect)
    , m_new(newRect)
{
    setText(QStringLiteral("Resize annotation"));
    qInfo() << "ChangeRectCommand: construct old=" << m_old << "new=" << m_new;
}

void ChangeRectCommand::undo()
{
    qInfo() << "ChangeRectCommand: undo" << m_old;
    if (m_item) {
        m_item->setRect(m_old);
        layoutHighlightChrome(m_item);
    }
}

void ChangeRectCommand::redo()
{
    qInfo() << "ChangeRectCommand: redo" << m_new;
    if (m_item) {
        m_item->setRect(m_new);
        layoutHighlightChrome(m_item);
    }
}

ChangeEndpointsCommand::ChangeEndpointsCommand(QGraphicsItem *item, const QPointF &oldP1, const QPointF &oldP2,
                                               const QPointF &newP1, const QPointF &newP2, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_item(item)
    , m_oldP1(oldP1)
    , m_oldP2(oldP2)
    , m_newP1(newP1)
    , m_newP2(newP2)
{
    setText(QStringLiteral("Resize annotation"));
    qInfo() << "ChangeEndpointsCommand: construct old=" << m_oldP1 << m_oldP2 << "new=" << m_newP1 << m_newP2;
}

void ChangeEndpointsCommand::undo()
{
    qInfo() << "ChangeEndpointsCommand: undo" << m_oldP1 << m_oldP2;
    applyEndpoints(m_item, m_oldP1, m_oldP2);
}

void ChangeEndpointsCommand::redo()
{
    qInfo() << "ChangeEndpointsCommand: redo" << m_newP1 << m_newP2;
    applyEndpoints(m_item, m_newP1, m_newP2);
}

ChangeBlurBoxCommand::ChangeBlurBoxCommand(QGraphicsPixmapItem *item, const QPointF &oldPos, const QImage &oldSource,
                                           const QPointF &newPos, const QImage &newSource, QUndoCommand *parent)
    : QUndoCommand(parent)
    , m_item(item)
    , m_oldPos(oldPos)
    , m_oldSource(oldSource)
    , m_newPos(newPos)
    , m_newSource(newSource)
{
    setText(QStringLiteral("Resize blur"));
    qInfo() << "ChangeBlurBoxCommand: construct oldPos=" << m_oldPos << "newPos=" << m_newPos
            << "oldSource=" << m_oldSource.size() << "newSource=" << m_newSource.size();
}

void ChangeBlurBoxCommand::apply(const QPointF &pos, const QImage &source)
{
    if (!m_item) {
        qWarning() << "ChangeBlurBoxCommand: apply null item";
        return;
    }
    m_item->setPos(pos);
    setBlurSource(m_item, source);
    applyBlurRadius(m_item, blurRadius(m_item));
    qInfo() << "ChangeBlurBoxCommand: apply pos=" << pos << "source=" << source.size();
}

void ChangeBlurBoxCommand::undo()
{
    qInfo() << "ChangeBlurBoxCommand: undo";
    apply(m_oldPos, m_oldSource);
}

void ChangeBlurBoxCommand::redo()
{
    qInfo() << "ChangeBlurBoxCommand: redo";
    apply(m_newPos, m_newSource);
}
