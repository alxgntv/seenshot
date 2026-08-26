#pragma once

#include "annotate/AnnotateItems.h"

#include <QColor>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineF>
#include <QMainWindow>
#include <QPointF>
#include <QRectF>

class QAction;
class QActionGroup;
class QFrame;
class QGraphicsDropShadowEffect;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class QTimer;
class QToolButton;
class QWidget;
class QGraphicsItem;
class QGraphicsPixmapItem;
class QGraphicsRectItem;
class ShotPhotoItem;
class QGraphicsScene;
class QGraphicsView;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QMoveEvent;
class QResizeEvent;
class QShowEvent;
class QUndoStack;
class AuthSession;
class CloudClient;
class HueColorSlider;
class AnnotateTextItem;
class AnnotatePhotoItem;
class CameraCapture;

// ─── Ariadne's Thread [AT-0012] ─────────────────────
// What: Annotation window with tools, color slider, save and share
// Why:  Core SeenShot editor after capture
// Date: 2026-08-25
// Related: client/src/annotate/AnnotateWindow.cpp
// ─────────────────────────────────────────────────────
class AnnotateWindow : public QMainWindow {
    Q_OBJECT
public:
    enum class Tool { Select, Highlight, Arrow, Line, Text, Blur };

    AnnotateWindow(const QImage &image, AuthSession *auth, CloudClient *cloud, QWidget *parent = nullptr);

    QImage exportedImage() const;
    void abortPhoto();
    bool isPhotoCaptureBusy() const;
    bool persistSession(QString *errorCode);
    bool restoreSession(const QJsonObject &json, const QHash<QString, QImage> &assets, QString *errorCode);
    void showUpdateOffer();
    void showUpdateProgress(qint64 received, qint64 expected, const QString &status);
    void showUpdateExtracting(double progress);
    void showUpdateInstalling();
    void resetUpdateOffer();
    void hideUpdateCard();
    void showUpdateError(const QString &code);

signals:
    void updateRequested();
    void photoCycleEnded();

public:
    bool viewPress(QMouseEvent *event, const QPointF &scenePos);
    bool viewMove(QMouseEvent *event, const QPointF &scenePos);
    bool viewRelease(QMouseEvent *event, const QPointF &scenePos);
    bool viewKeyPress(QKeyEvent *event);
    void paintSelectHandles(QPainter *painter) const;

public slots:
    void handlePress(const QPointF &pos);
    void handleMove(const QPointF &pos);
    void handleRelease(const QPointF &pos);

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private slots:
    void undo();
    void redo();
    void saveLocal();
    void share();
    void setToolSelect();
    void setToolHighlight();
    void setToolArrow();
    void setToolLine();
    void setToolText();
    void setToolBlur();
    void setHighlightStyleTool(HighlightStyle style);
    void onHuePressed();
    void onHueChanged(int hue);
    void onHueReleased();
    void onStrokePressed();
    void onStrokeChanged(int width);
    void onStrokeReleased();
    void onFillPressed();
    void onFillChanged(int percent);
    void onFillReleased();
    void setBackgroundPreset(int preset);
    void onRadiusChanged(int radius);
    void onShadowChanged(int amount);
    void startPhotoPreview();
    void startPhotoPicture();
    void startPhotoTimer5();
    void onPhotoStillReady(const QImage &image);
    void onPhotoStillFailed(const QString &code);
    void onPhotoCountdownTick();

private:
    void onSceneMoved(const QPointF &pos);
    void onSceneReleased(const QPointF &pos);
    QGraphicsItem *lastColorableItem() const;
    QGraphicsItem *lastHighlightItem() const;
    AnnotateTextItem *textItemAt(const QPointF &scenePos) const;
    AnnotatePhotoItem *photoItemAt(const QPointF &scenePos) const;
    QGraphicsItem *annotationItemAt(const QPointF &scenePos) const;
    QGraphicsItem *selectedAnnotation() const;
    void selectAnnotation(QGraphicsItem *item);
    enum class SelectHandle { None, Move, TopLeft, TopRight, BottomLeft, BottomRight, P1, P2 };
    bool hitsScenePoint(const QPointF &target, const QPointF &scenePos) const;
    SelectHandle hitSelectHandle(QGraphicsItem *item, const QPointF &scenePos) const;
    void applySelectResize(const QPointF &scenePos);
    void applySelectMove(const QPointF &scenePos);
    void commitSelectGesture();
    void deleteSelectedAnnotation();
    void clampItemToShot(QGraphicsItem *item) const;
    void applyBlurSceneRect(QGraphicsPixmapItem *item, const QRectF &sceneRect);
    QRectF itemSceneBox(QGraphicsItem *item) const;
    void syncToolActions();
    enum class PhotoFollow { None, PreviewOnly, Picture, Timer5 };
    enum class PhotoFlash { Idle, Pre, Capturing, Post };
    void startPhotoWithFollow(PhotoFollow follow);
    void runPhotoFollow();
    bool ensurePhotoPreview();
    void showPhotoCaptureMenu();
    void beginPhotoFlashCapture();
    void finishPhotoStill(const QImage &image);
    bool photoCaptureBusy() const;
    void abortPhotoCycle(bool notifyEnded = true);
    void layoutUpdateCard();
    void ensureUpdateCard();
    QJsonObject serializeSession(QHash<QString, QImage> *assets) const;
    bool restoreItems(const QJsonArray &items, const QHash<QString, QImage> &assets, QString *errorCode);
    void placePhotoCutout(const QImage &cutout);
    void clampPhotoToShot(AnnotatePhotoItem *item) const;
    void layoutPhotoOverlay();
    HighlightStyle currentHighlightStyle() const;
    void beginNewText(const QPointF &scenePos);
    void beginEditText(AnnotateTextItem *item, bool draft);
    void beginFillTextCaption(QGraphicsRectItem *rect);
    void commitTextEdit();
    QRectF shotRect() const;
    bool isOnShot(const QPointF &scenePos) const;
    QPointF clampToShot(const QPointF &scenePos) const;
    void applyWindowScreenLayout();
    void fitShotToWindow();
    void applyCanvasChrome();
    QRectF canvasRect() const;
    QImage shotImage() const;
    bool hasBackground() const;
    qreal maxTextWidthOnShot(const AnnotateTextItem *item) const;
    void showError(const QString &code);
    bool ensureOnlineSignedIn(QString *errorCode);

    QImage m_source;
    QGraphicsScene *m_scene = nullptr;
    QGraphicsView *m_view = nullptr;
    ShotPhotoItem *m_photo = nullptr;
    QGraphicsRectItem *m_background = nullptr;
    QGraphicsDropShadowEffect *m_photoShadow = nullptr;
    QUndoStack *m_undo = nullptr;
    HueColorSlider *m_hue = nullptr;
    QSlider *m_stroke = nullptr;
    QSlider *m_fill = nullptr;
    QToolButton *m_bgButton = nullptr;
    QActionGroup *m_bgGroup = nullptr;
    int m_bgPreset = 0;
    QActionGroup *m_toolGroup = nullptr;
    QAction *m_selectAction = nullptr;
    QAction *m_highlightAction = nullptr;
    QAction *m_styleStepsAction = nullptr;
    QAction *m_arrowAction = nullptr;
    QAction *m_lineAction = nullptr;
    QAction *m_textToolAction = nullptr;
    QAction *m_blurAction = nullptr;
    QLabel *m_radiusLabel = nullptr;
    QSlider *m_radius = nullptr;
    QLabel *m_shadowLabel = nullptr;
    QSlider *m_shadow = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    Tool m_tool = Tool::Highlight;
    HighlightStyle m_highlightStyle = HighlightStyle::Fill;
    QColor m_color = QColor::fromHsv(0, 255, 230);
    int m_strokeWidth = 2;
    int m_fillAlpha = 80;
    int m_blurRadius = 8;
    int m_cornerRadius = 16;
    int m_shadowAmount = 18;
    QPointF m_dragStart;
    bool m_drawing = false;
    QGraphicsItem *m_draft = nullptr;
    AnnotateTextItem *m_editingText = nullptr;
    QString m_editOldText;
    bool m_textDraft = false;
    QGraphicsRectItem *m_fillTextRect = nullptr;
    AnnotateTextItem *m_resizingText = nullptr;
    qreal m_resizeOldWidth = 0;
    QPointF m_resizeStart;
    int m_nextStepSeq = 1;
    int m_colorGestureId = 0;
    bool m_colorGestureActive = false;
    int m_strokeGestureId = 0;
    bool m_strokeGestureActive = false;
    int m_fillGestureId = 0;
    bool m_fillGestureActive = false;
    bool m_didScreenLayout = false;
    AuthSession *m_auth = nullptr;
    CloudClient *m_cloud = nullptr;
    QString m_cloudShotId;
    CameraCapture *m_camera = nullptr;
    QToolButton *m_photoButton = nullptr;
    QWidget *m_photoOverlay = nullptr;
    QLabel *m_photoCountdown = nullptr;
    QWidget *m_photoFlash = nullptr;
    QTimer *m_photoTimer = nullptr;
    int m_photoCount = 0;
    int m_photoToken = 0;
    bool m_photoCycle = false;
    PhotoFollow m_photoFollow = PhotoFollow::None;
    PhotoFlash m_photoFlashPhase = PhotoFlash::Idle;
    QImage m_pendingStill;
    AnnotatePhotoItem *m_movingPhoto = nullptr;
    AnnotatePhotoItem *m_scalingPhoto = nullptr;
    QPointF m_photoOldPos;
    qreal m_photoOldScale = 1;
    int m_photoMoveGestureId = 0;
    int m_photoScaleGestureId = 0;
    QGraphicsItem *m_selectItem = nullptr;
    SelectHandle m_selectHandle = SelectHandle::None;
    bool m_selectDidMove = false;
    bool m_selectWasSelected = false;
    QPointF m_selectPressScene;
    QPointF m_selectOldPos;
    QRectF m_selectOldRect;
    QPointF m_selectOldP1;
    QPointF m_selectOldP2;
    QImage m_selectOldBlurSource;
    QPointF m_selectOldBlurPos;
    QFrame *m_updateCard = nullptr;
    QLabel *m_updateTitle = nullptr;
    QPushButton *m_updateButton = nullptr;
    QProgressBar *m_updateBar = nullptr;
    QLabel *m_updateStatus = nullptr;
};
