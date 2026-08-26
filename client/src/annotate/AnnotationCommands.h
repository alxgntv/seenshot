#pragma once

#include <QColor>
#include <QGraphicsItem>
#include <QImage>
#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QUndoCommand>

class QGraphicsScene;
class QGraphicsTextItem;
class QGraphicsRectItem;
class QGraphicsPixmapItem;

// ─── Ariadne's Thread [AT-0011] ─────────────────────
// What: QUndoCommand types for annotate tools and last-item color
// Why:  Native QUndoStack undo/redo from the plan
// Date: 2026-08-25
// Related: client/src/annotate/AnnotationCommands.cpp
// ─────────────────────────────────────────────────────
class AddItemCommand : public QUndoCommand {
public:
    AddItemCommand(QGraphicsScene *scene, QGraphicsItem *item, QUndoCommand *parent = nullptr);
    ~AddItemCommand() override;
    void undo() override;
    void redo() override;

private:
    QGraphicsScene *m_scene;
    QGraphicsItem *m_item;
    bool m_owns = false;
};

class RemoveItemCommand : public QUndoCommand {
public:
    RemoveItemCommand(QGraphicsScene *scene, QGraphicsItem *item, QUndoCommand *parent = nullptr);
    ~RemoveItemCommand() override;
    void undo() override;
    void redo() override;

private:
    QGraphicsScene *m_scene;
    QGraphicsItem *m_item;
    bool m_owns = false;
};

class ChangeColorCommand : public QUndoCommand {
public:
    ChangeColorCommand(QGraphicsItem *item, const QColor &color, int gestureId, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;
    int id() const override;
    bool mergeWith(const QUndoCommand *other) override;

private:
    QGraphicsItem *m_item;
    QColor m_old;
    QColor m_new;
    int m_gestureId = 0;
};

class ChangeStrokeWidthCommand : public QUndoCommand {
public:
    ChangeStrokeWidthCommand(QGraphicsItem *item, int width, int gestureId, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;
    int id() const override;
    bool mergeWith(const QUndoCommand *other) override;

private:
    QGraphicsItem *m_item;
    int m_old = 2;
    int m_new = 2;
    int m_gestureId = 0;
};

class ChangeFillAlphaCommand : public QUndoCommand {
public:
    ChangeFillAlphaCommand(QGraphicsItem *item, int alpha, int gestureId, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;
    int id() const override;
    bool mergeWith(const QUndoCommand *other) override;

private:
    QGraphicsItem *m_item;
    int m_old = 80;
    int m_new = 80;
    int m_gestureId = 0;
};

class ChangeBlurRadiusCommand : public QUndoCommand {
public:
    ChangeBlurRadiusCommand(QGraphicsItem *item, int radius, int gestureId, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;
    int id() const override;
    bool mergeWith(const QUndoCommand *other) override;

private:
    QGraphicsItem *m_item;
    int m_old = 8;
    int m_new = 8;
    int m_gestureId = 0;
};

class ChangeTextCommand : public QUndoCommand {
public:
    ChangeTextCommand(QGraphicsTextItem *item, const QString &oldText, const QString &newText,
                      QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    QGraphicsTextItem *m_item;
    QString m_old;
    QString m_new;
};

class ChangePhotoPosCommand : public QUndoCommand {
public:
    ChangePhotoPosCommand(QGraphicsItem *item, const QPointF &oldPos, const QPointF &newPos, int gestureId,
                          QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;
    int id() const override;
    bool mergeWith(const QUndoCommand *other) override;

private:
    QGraphicsItem *m_item = nullptr;
    QPointF m_old;
    QPointF m_new;
    int m_gestureId = 0;
};

class ChangePhotoScaleCommand : public QUndoCommand {
public:
    ChangePhotoScaleCommand(QGraphicsItem *item, qreal oldScale, qreal newScale, int gestureId,
                            QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;
    int id() const override;
    bool mergeWith(const QUndoCommand *other) override;

private:
    QGraphicsItem *m_item = nullptr;
    qreal m_old = 1;
    qreal m_new = 1;
    int m_gestureId = 0;
};

class ChangeRectCommand : public QUndoCommand {
public:
    ChangeRectCommand(QGraphicsRectItem *item, const QRectF &oldRect, const QRectF &newRect,
                      QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    QGraphicsRectItem *m_item = nullptr;
    QRectF m_old;
    QRectF m_new;
};

class ChangeEndpointsCommand : public QUndoCommand {
public:
    ChangeEndpointsCommand(QGraphicsItem *item, const QPointF &oldP1, const QPointF &oldP2, const QPointF &newP1,
                           const QPointF &newP2, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    QGraphicsItem *m_item = nullptr;
    QPointF m_oldP1;
    QPointF m_oldP2;
    QPointF m_newP1;
    QPointF m_newP2;
};

class ChangeBlurBoxCommand : public QUndoCommand {
public:
    ChangeBlurBoxCommand(QGraphicsPixmapItem *item, const QPointF &oldPos, const QImage &oldSource,
                         const QPointF &newPos, const QImage &newSource, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    void apply(const QPointF &pos, const QImage &source);
    QGraphicsPixmapItem *m_item = nullptr;
    QPointF m_oldPos;
    QImage m_oldSource;
    QPointF m_newPos;
    QImage m_newSource;
};

class ChangeTextWidthCommand : public QUndoCommand {
public:
    ChangeTextWidthCommand(QGraphicsTextItem *item, qreal oldWidth, qreal newWidth, QUndoCommand *parent = nullptr);
    void undo() override;
    void redo() override;

private:
    QGraphicsTextItem *m_item;
    qreal m_old = 0;
    qreal m_new = 0;
};

void applyItemColor(QGraphicsItem *item, const QColor &color);
QColor itemColor(QGraphicsItem *item);
