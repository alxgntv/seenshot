#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QGraphicsPixmapItem>
#include <QPainterPath>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>

class QGraphicsPathItem;
class QGraphicsScene;

constexpr int kAnnotateRoleKind = 32;
constexpr int kAnnotateRoleStyle = 33;
constexpr int kAnnotateRoleStepSeq = 34;
constexpr int kAnnotateRoleStrokeWidth = 35;
constexpr int kAnnotateRoleFillAlpha = 36;
constexpr int kAnnotateRoleBlurRadius = 37;
constexpr int kAnnotateRoleBlurSource = 38;
constexpr int kAnnotateRoleP1 = 39;
constexpr int kAnnotateRoleP2 = 40;

enum class AnnotateKind {
    None = 0,
    Highlight = 1,
    Arrow = 2,
    Text = 3,
    Blur = 4,
    Step = 5,
    Line = 6,
    Photo = 7,
};

enum class HighlightStyle {
    Border = 0,
    Fill = 1,
    FillText = 2,
    Steps = 3,
};

void setAnnotateKind(QGraphicsItem *item, AnnotateKind kind);
AnnotateKind annotateKind(const QGraphicsItem *item);
void setHighlightStyle(QGraphicsItem *item, HighlightStyle style);
HighlightStyle highlightStyle(const QGraphicsItem *item);
void setHighlightStrokeWidth(QGraphicsItem *item, int width);
int highlightStrokeWidth(const QGraphicsItem *item);
void setHighlightFillAlpha(QGraphicsItem *item, int alpha);
int highlightFillAlpha(const QGraphicsItem *item);
bool isColorableKind(AnnotateKind kind);
QColor contrastInk(const QColor &fill);
void syncStepNumbers(QGraphicsScene *scene);
void applyHighlightAppearance(QGraphicsRectItem *rect, const QColor &color, HighlightStyle style);
void attachHighlightStepBadge(QGraphicsRectItem *rect, const QColor &color, int seq, const QRectF &shot);
void attachHighlightStepBadge(QGraphicsRectItem *rect, const QColor &color, int seq, const QRectF &shot,
                              const QPointF &cursorScene);
void placeHighlightStepBadge(QGraphicsRectItem *rect, const QRectF &shot, const QPointF &cursorScene);
QImage boxBlur(const QImage &patch, int radius);
void setBlurSource(QGraphicsItem *item, const QImage &source);
int blurRadius(const QGraphicsItem *item);
void applyBlurRadius(QGraphicsItem *item, int radius);
void layoutHighlightChrome(QGraphicsRectItem *rect);
QPointF annotateP1(const QGraphicsItem *item);
QPointF annotateP2(const QGraphicsItem *item);
void setAnnotateEndpoints(QGraphicsItem *item, const QPointF &p1, const QPointF &p2);
QPainterPath arrowPath(const QPointF &from, const QPointF &to);
void setArrowEndpoints(QGraphicsPathItem *item, const QPointF &from, const QPointF &to);

// ─── Ariadne's Thread [AT-0040] ─────────────────────
// What: In-place text block with wrap width and resize grip
// Why:  PRD-02 Text — caret on image, wrap by width, no Label dialog
// Date: 2026-08-25
// Related: [AT-0011] AnnotationCommands.h, docs/PRD-02-annotate-tools.md
// ─────────────────────────────────────────────────────
class AnnotateTextItem : public QGraphicsTextItem {
public:
    enum { Type = UserType + 21 };
    explicit AnnotateTextItem(const QString &text = QString());
    int type() const override;
    QPainterPath shape() const override;
    bool isResizeHandle(const QPointF &itemPos) const;
    qreal minTextWidth() const;
    void beginEdit();
    void endEdit();
    bool isEditing() const;

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
};

// ─── Ariadne's Thread [AT-0041] ─────────────────────
// What: Number badge on Highlight Steps style rectangles
// Why:  Steps is a Highlight style, not a separate tool
// Date: 2026-08-25
// Related: [AT-0040] AnnotateItems.h:AnnotateTextItem, [AT-0045] AnnotateWindow.cpp
// ─────────────────────────────────────────────────────
class StepBadgeItem : public QGraphicsRectItem {
public:
    enum { Type = UserType + 22 };
    StepBadgeItem(int seq, const QColor &color);
    int type() const override;
    void setNumber(int number);
    int number() const;
    int seq() const;
    void applyColor(const QColor &color);

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    int m_number = 1;
    QColor m_ink = Qt::white;
};

// ─── Ariadne's Thread [AT-0056] ─────────────────────
// What: Screenshot pixmap clipped to a rounded rect via ItemClipsToShape
// Why:  Corner radius of the shot when a gradient backdrop is selected
// Date: 2026-08-25
// Related: [AT-0012] AnnotateWindow.cpp:applyCanvasChrome
// ─────────────────────────────────────────────────────
class ShotPhotoItem : public QGraphicsPixmapItem {
public:
    enum { Type = UserType + 23 };
    explicit ShotPhotoItem(const QPixmap &pixmap);
    int type() const override;
    void setCornerRadius(qreal radius);
    qreal cornerRadius() const;
    QPainterPath shape() const override;

private:
    qreal m_radius = 0;
};

// ─── Ariadne's Thread [AT-0060] ─────────────────────
// What: Person cutout pixmap with corner scale grip
// Why:  PRD-03 Photo is an object on the shot, not a drawing tool
// Date: 2026-08-25
// Related: [AT-0040] AnnotateItems.h:AnnotateTextItem, docs/PRD-03-photo-cutout.md
// ─────────────────────────────────────────────────────
class AnnotatePhotoItem : public QGraphicsPixmapItem {
public:
    enum { Type = UserType + 24 };
    explicit AnnotatePhotoItem(const QPixmap &pixmap);
    int type() const override;
    bool isScaleHandle(const QPointF &itemPos) const;
    qreal minScale() const;
    qreal maxScaleOnShot(const QRectF &shot) const;
    QRectF sceneBox() const;
    void setPhotoScale(qreal scale);
    qreal photoScale() const;

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
};
