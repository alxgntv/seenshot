#include "app/FirstRunWizard.h"

#include "app/MacIcons.h"
#include "app/MacLoginItem.h"
#include "app/MacPermissions.h"
#include "errors/ErrorCatalog.h"
#include "local/LocalStore.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWizardPage>

namespace {

void configureHotkeyEdit(QKeySequenceEdit *edit)
{
    edit->setClearButtonEnabled(false);
    edit->setMaximumSequenceLength(1);
    edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    edit->setFocusPolicy(Qt::ClickFocus);
    edit->setAttribute(Qt::WA_MacShowFocusRect, true);
}

class WelcomePage : public QWizardPage {
public:
    explicit WelcomePage(QWidget *parent = nullptr)
        : QWizardPage(parent)
    {
        setTitle(QStringLiteral("Welcome"));
        auto *layout = new QVBoxLayout(this);
        auto *icon = new QLabel(this);
        icon->setAlignment(Qt::AlignCenter);
        const QPixmap mark = macBundleIcon(96);
        icon->setPixmap(mark);
        auto *body = new QLabel(
            QStringLiteral("Welcome to SeenShot.\n\nCapture, annotate, and share screenshots on your Mac."),
            this);
        body->setWordWrap(true);
        layout->addWidget(icon);
        layout->addWidget(body);
        qInfo() << "FirstRunWizard: WelcomePage constructed iconNull=" << mark.isNull();
    }
};

class HotkeyPage : public QWizardPage {
public:
    enum Kind { FullScreen, Path };

    HotkeyPage(Kind kind, QWidget *parent = nullptr)
        : QWizardPage(parent)
        , m_kind(kind)
    {
        if (kind == FullScreen) {
            setTitle(QStringLiteral("Full Screen Shot"));
        } else {
            setTitle(QStringLiteral("Path"));
        }
        auto *layout = new QVBoxLayout(this);
        // ─── Ariadne's Thread [AT-0306] ─────────────────────
        // What: Drop the Cmd+Shift+3/4/5 system-screenshot warning from HotkeyPage
        // Why:  Onboarding must not show that copy
        // Date: 2026-08-28
        // Related: [AT-0302] FirstRunWizard.cpp:HotkeyPage
        // ─────────────────────────────────────────────────────
        auto *prompt = new QLabel(
            kind == FullScreen ? QStringLiteral("Choose the shortcut for a full-screen capture.")
                               : QStringLiteral("Choose the shortcut for Path."),
            this);
        prompt->setWordWrap(true);
        layout->addWidget(prompt);
        m_edit = new QKeySequenceEdit(this);
        configureHotkeyEdit(m_edit);
        layout->addWidget(m_edit);
        qInfo() << "FirstRunWizard: HotkeyPage constructed kind=" << (kind == FullScreen ? "full" : "path");
    }

    void initializePage() override
    {
        const QString spec = m_kind == FullScreen ? LocalStore::fullScreenHotkeySpec() : LocalStore::hotkeySpec();
        const QKeySequence seq = LocalStore::keySequenceFromSpec(spec);
        m_edit->setKeySequence(seq);
        qInfo() << "FirstRunWizard: HotkeyPage initialize kind=" << (m_kind == FullScreen ? "full" : "path")
                << " spec=" << spec << " seq=" << seq.toString(QKeySequence::NativeText);
    }

    bool validatePage() override
    {
        const QString spec = LocalStore::specFromKeySequence(m_edit->keySequence());
        const QString other = m_kind == FullScreen ? LocalStore::hotkeySpec() : LocalStore::fullScreenHotkeySpec();
        qInfo() << "FirstRunWizard: HotkeyPage validate kind=" << (m_kind == FullScreen ? "full" : "path")
                << " spec=" << spec << " other=" << other;
        if (spec.isEmpty()) {
            qWarning() << "FirstRunWizard: empty hotkey";
            QMessageBox::warning(this, QStringLiteral("SeenShot"),
                                 ErrorCatalog::message(QStringLiteral("HOTKEY_IN_USE")));
            return false;
        }
        if (spec == other) {
            qWarning() << "FirstRunWizard: hotkeys collide spec=" << spec;
            QMessageBox::warning(this, QStringLiteral("SeenShot"),
                                 ErrorCatalog::message(QStringLiteral("HOTKEY_IN_USE")));
            return false;
        }
        if (m_kind == FullScreen) {
            LocalStore::setFullScreenHotkeySpec(spec);
        } else {
            LocalStore::setHotkeySpec(spec);
        }
        qInfo() << "FirstRunWizard: HotkeyPage saved kind=" << (m_kind == FullScreen ? "full" : "path")
                << " spec=" << spec;
        return true;
    }

private:
    Kind m_kind = Path;
    QKeySequenceEdit *m_edit = nullptr;
};

class ScreenRecordingPage : public QWizardPage {
public:
    explicit ScreenRecordingPage(QWidget *parent = nullptr)
        : QWizardPage(parent)
    {
        setTitle(QStringLiteral("Screen Recording"));
        auto *layout = new QVBoxLayout(this);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        m_guide = new QLabel(this);
        m_guide->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        m_guide->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        m_guide->setScaledContents(false);
        const QString imagePath = QDir(QCoreApplication::applicationDirPath())
                                      .filePath(QStringLiteral("../Resources/screen-recording-toggle.png"));
        m_guidePix = QPixmap(imagePath);
        qInfo() << "FirstRunWizard: Screen Recording guide path=" << imagePath
                << " null=" << m_guidePix.isNull() << " size=" << m_guidePix.size();
        if (m_guidePix.isNull()) {
            qWarning() << "FirstRunWizard: screen-recording-toggle.png missing";
        }
        m_allow = new QPushButton(QStringLiteral("Allow Screen Recording"), this);
        connect(m_allow, &QPushButton::clicked, this, [this]() { onAllow(); });
        layout->addWidget(m_status);
        layout->addWidget(m_guide);
        layout->addWidget(m_allow);
        qInfo() << "FirstRunWizard: ScreenRecordingPage constructed";
    }

    void initializePage() override
    {
        const bool preflight = MacPermissions::hasScreenRecording();
        qInfo() << "FirstRunWizard: ScreenRecordingPage initialize preflight=" << preflight
                << " registered=" << LocalStore::screenRecordingRegistered();
        paintStatus(preflight);
        fitGuide();
    }

    // ─── Ariadne's Thread [AT-0309] ─────────────────────
    // What: Continue on Screen Recording opens Settings until TCC is granted
    // Why:  Next skipped the page; Continue must match Allow Screen Recording
    // Date: 2026-08-28
    // Related: [AT-0308] FirstRunWizard.cpp:onAllow, [AT-0034] MacPermissions.mm:openScreenRecordingSettings
    // ─────────────────────────────────────────────────────
    bool validatePage() override
    {
        const bool preflight = MacPermissions::hasScreenRecording();
        qInfo() << "FirstRunWizard: ScreenRecordingPage Continue preflight=" << preflight
                << " registered=" << LocalStore::screenRecordingRegistered();
        if (preflight) {
            qInfo() << "FirstRunWizard: ScreenRecordingPage Continue advancing, Screen Recording granted";
            return true;
        }
        const bool probe = MacPermissions::probeScreenRecording();
        qInfo() << "FirstRunWizard: ScreenRecordingPage Continue probe=" << probe;
        if (probe) {
            qInfo() << "FirstRunWizard: ScreenRecordingPage Continue advancing after probe";
            return true;
        }
        qInfo() << "FirstRunWizard: ScreenRecordingPage Continue blocked, opening Screen Recording settings";
        onAllow();
        return false;
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWizardPage::resizeEvent(event);
        qInfo() << "FirstRunWizard: ScreenRecordingPage resize=" << event->size();
        fitGuide();
    }

    void showEvent(QShowEvent *event) override
    {
        QWizardPage::showEvent(event);
        qInfo() << "FirstRunWizard: ScreenRecordingPage showEvent";
        fitGuide();
    }

private:
    // ─── Ariadne's Thread [AT-0308] ─────────────────────
    // What: One Allow Screen Recording button; scale the Settings row to the page width
    // Why:  Two buttons and a clipped 440px pixmap hid the toggle
    // Date: 2026-08-28
    // Related: [AT-0307] FirstRunWizard.cpp:ScreenRecordingPage, [AT-0034] MacPermissions.mm:openScreenRecordingSettings
    // ─────────────────────────────────────────────────────
    void fitGuide()
    {
        if (m_guidePix.isNull() || !m_guide) {
            return;
        }
        const int w = m_guide->contentsRect().width();
        if (w < 8) {
            qInfo() << "FirstRunWizard: fitGuide skip width=" << w;
            return;
        }
        const QPixmap fitted = m_guidePix.scaledToWidth(w, Qt::SmoothTransformation);
        m_guide->setPixmap(fitted);
        m_guide->setMinimumHeight(fitted.height());
        qInfo() << "FirstRunWizard: fitGuide labelW=" << w << " fitted=" << fitted.size()
                << " src=" << m_guidePix.size();
    }

    void onAllow()
    {
        const bool registered = LocalStore::screenRecordingRegistered();
        qInfo() << "FirstRunWizard: Allow Screen Recording registered=" << registered;
        if (!registered) {
            const bool requested = MacPermissions::requestScreenRecording();
            LocalStore::setScreenRecordingRegistered();
            qInfo() << "FirstRunWizard: CGRequestScreenCaptureAccess for TCC row ok=" << requested;
        }
        MacPermissions::openScreenRecordingSettings();
        const bool preflight = MacPermissions::hasScreenRecording();
        qInfo() << "FirstRunWizard: after Allow Screen Recording preflight=" << preflight;
        paintStatus(preflight);
    }

    void paintStatus(bool granted)
    {
        if (granted) {
            m_status->setText(QStringLiteral("Screen Recording is on."));
            qInfo() << "FirstRunWizard: Screen Recording granted, Allow still opens Settings";
            return;
        }
        m_status->setText(QStringLiteral("Turn on SeenShot in Screen Recording."));
        qInfo() << "FirstRunWizard: Screen Recording not granted";
    }

    QPixmap m_guidePix;
    QLabel *m_status = nullptr;
    QLabel *m_guide = nullptr;
    QPushButton *m_allow = nullptr;
};

class LoginItemPage : public QWizardPage {
public:
    explicit LoginItemPage(QWidget *parent = nullptr)
        : QWizardPage(parent)
    {
        setTitle(QStringLiteral("Open at Login"));
        auto *layout = new QVBoxLayout(this);
        auto *body = new QLabel(QStringLiteral("Start SeenShot automatically when you log in to this Mac."), this);
        body->setWordWrap(true);
        m_check = new QCheckBox(QStringLiteral("Open SeenShot at login"), this);
        layout->addWidget(body);
        layout->addWidget(m_check);
        qInfo() << "FirstRunWizard: LoginItemPage constructed";
    }

    void initializePage() override
    {
        const bool enabled = MacLoginItem::isEnabled();
        m_check->setChecked(true);
        qInfo() << "FirstRunWizard: LoginItemPage initialize systemEnabled=" << enabled
                << " checkbox=true";
    }

    bool validatePage() override
    {
        const bool on = m_check->isChecked();
        const bool already = MacLoginItem::isEnabled();
        qInfo() << "FirstRunWizard: LoginItemPage apply on=" << on << " already=" << already;
        if (on == already) {
            qInfo() << "FirstRunWizard: LoginItemPage already matches, skip setEnabled";
            return true;
        }
        QString error;
        if (!MacLoginItem::setEnabled(on, &error)) {
            qWarning() << "FirstRunWizard: login item failed code=" << error;
            m_check->setChecked(MacLoginItem::isEnabled());
            QMessageBox::warning(this, QStringLiteral("SeenShot"),
                                 ErrorCatalog::message(error.isEmpty() ? QStringLiteral("LOGIN_ITEM_FAILED")
                                                                        : error));
            qInfo() << "FirstRunWizard: LoginItemPage checkbox after fail=" << m_check->isChecked();
            return true;
        }
        qInfo() << "FirstRunWizard: LoginItemPage applied on=" << on
                << " systemEnabled=" << MacLoginItem::isEnabled();
        return true;
    }

private:
    QCheckBox *m_check = nullptr;
};

class ReadyPage : public QWizardPage {
public:
    explicit ReadyPage(QWidget *parent = nullptr)
        : QWizardPage(parent)
    {
        setTitle(QStringLiteral("You're ready"));
        auto *layout = new QVBoxLayout(this);
        m_body = new QLabel(this);
        m_body->setWordWrap(true);
        layout->addWidget(m_body);
        qInfo() << "FirstRunWizard: ReadyPage constructed";
    }

    void initializePage() override
    {
        const QString full = LocalStore::fullScreenHotkeySpec();
        const QString path = LocalStore::hotkeySpec();
        const QString fullLabel = LocalStore::nativeHotkeyLabel(full);
        const QString pathLabel = LocalStore::nativeHotkeyLabel(path);
        m_body->setText(QStringLiteral("You're ready.\n\n"
                                     "Path: %1\n"
                                     "Full Screen Shot: %2\n\n"
                                     "Take your first screenshot.")
                           .arg(pathLabel, fullLabel));
        qInfo() << "FirstRunWizard: ReadyPage path=" << path << " full=" << full
                << " pathLabel=" << pathLabel << " fullLabel=" << fullLabel;
    }

private:
    QLabel *m_body = nullptr;
};

} // namespace

FirstRunWizard::FirstRunWizard(QWidget *parent)
    : QWizard(parent)
{
    setWindowTitle(QStringLiteral("SeenShot Setup"));
    setAttribute(Qt::WA_QuitOnClose, false);
    setWizardStyle(QWizard::MacStyle);
    setOption(QWizard::NoBackButtonOnStartPage, true);
    setButtonText(QWizard::FinishButton, QStringLiteral("Take screenshot"));
    addPage(new WelcomePage(this));
    // ─── Ariadne's Thread [AT-0318] ─────────────────────
    // What: Path hotkey page before Full Screen Shot; Path is the UI name
    // Why:  Partial capture is Path and must be first in setup
    // Date: 2026-08-28
    // Related: [AT-0302] FirstRunWizard.cpp:HotkeyPage, [AT-0319] SettingsWindow.cpp
    // ─────────────────────────────────────────────────────
    addPage(new HotkeyPage(HotkeyPage::Path, this));
    addPage(new HotkeyPage(HotkeyPage::FullScreen, this));
    addPage(new ScreenRecordingPage(this));
    addPage(new LoginItemPage(this));
    addPage(new ReadyPage(this));
    setMinimumWidth(640);
    qInfo() << "FirstRunWizard: opened pages=" << pageIds().size()
            << " WA_QuitOnClose=" << testAttribute(Qt::WA_QuitOnClose)
            << " hotkeyOrder=path,full";
}
