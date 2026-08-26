#pragma once

#include <QColor>
#include <QSlider>

// ─── Ariadne's Thread [AT-0042] ─────────────────────
// What: Hue-spectrum slider with thumb painted in the current color
// Why:  PRD-02 Color slider — rainbow track, not a gray QSlider
// Date: 2026-08-25
// Related: [AT-0013] AnnotateWindow.cpp, docs/PRD-02-annotate-tools.md
// ─────────────────────────────────────────────────────
class HueColorSlider : public QSlider {
public:
    explicit HueColorSlider(QWidget *parent = nullptr);
    QColor currentColor() const;

protected:
    void paintEvent(QPaintEvent *event) override;
};
