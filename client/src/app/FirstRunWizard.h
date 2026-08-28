#pragma once

#include <QWizard>

class QCheckBox;
class QKeySequenceEdit;
class QLabel;

// ─── Ariadne's Thread [AT-0302] ─────────────────────
// What: Replace the one-combo first-run dialog with a 6-page QWizard
// Why:  Post-install setup is Welcome, both hotkeys, Screen Recording, login, first capture
// Date: 2026-08-28
// Related: [AT-0303] Application.cpp:start, [AT-0301] LocalStore.cpp:onboardingVersion
// ─────────────────────────────────────────────────────
class FirstRunWizard : public QWizard {
    Q_OBJECT
public:
    explicit FirstRunWizard(QWidget *parent = nullptr);
};
