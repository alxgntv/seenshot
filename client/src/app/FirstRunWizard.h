#pragma once

#include <QDialog>
#include <QString>

class QComboBox;

class FirstRunWizard : public QDialog {
    Q_OBJECT
public:
    explicit FirstRunWizard(QWidget *parent = nullptr);
    QString selectedHotkey() const;

private:
    QComboBox *m_combo = nullptr;
};
