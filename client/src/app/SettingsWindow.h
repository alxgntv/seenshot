#pragma once

#include <QWidget>

class AuthSession;
class CloudClient;
class QCheckBox;
class QGroupBox;
class QKeySequenceEdit;
class QLabel;
class QPushButton;

class SettingsWindow : public QWidget {
    Q_OBJECT
public:
    SettingsWindow(AuthSession *auth, CloudClient *cloud, QWidget *parent = nullptr);

signals:
    void hotkeysChanged();

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void openSignIn();
    void onWebsiteSignInSettled(const QString &errorCode);
    void signOut();
    void exportData();
    void deleteAccount();
    void refreshQuota();
    void applyHotkeys();
    void onLaunchAtLoginToggled(bool on);
    void onSessionChanged();

private:
    void updateAccountUi();
    void loadHotkeys();
    void loadLaunchAtLogin();
    void showAuthError(const QString &code);

    AuthSession *m_auth = nullptr;
    CloudClient *m_cloud = nullptr;
    QKeySequenceEdit *m_fullScreenHotkey = nullptr;
    QKeySequenceEdit *m_pathHotkey = nullptr;
    QCheckBox *m_launchAtLogin = nullptr;
    QGroupBox *m_signedOutBox = nullptr;
    QGroupBox *m_signedInBox = nullptr;
    QPushButton *m_signInBtn = nullptr;
    QLabel *m_profile = nullptr;
    QPushButton *m_signOutBtn = nullptr;
    QPushButton *m_proBtn = nullptr;
    QPushButton *m_exportBtn = nullptr;
    QPushButton *m_deleteBtn = nullptr;
    QLabel *m_version = nullptr;
    bool m_syncingLaunch = false;
    bool m_websiteSignInBusy = false;
};
