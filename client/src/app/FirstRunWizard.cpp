#include "app/FirstRunWizard.h"

#include <QComboBox>
#include <QDebug>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

FirstRunWizard::FirstRunWizard(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("SeenShot Setup"));
    setAttribute(Qt::WA_QuitOnClose, false);
    qInfo() << "FirstRunWizard: WA_QuitOnClose=" << testAttribute(Qt::WA_QuitOnClose);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral(
        "macOS already uses Cmd+Shift+3, Cmd+Shift+4, and Cmd+Shift+5 for screenshots.\n"
        "SeenShot will not take those unless you choose them.")));
    m_combo = new QComboBox(this);
    m_combo->addItem(QStringLiteral("Cmd+Shift+2 (recommended)"), QStringLiteral("cmd+shift+2"));
    m_combo->addItem(QStringLiteral("Cmd+Shift+6"), QStringLiteral("cmd+shift+6"));
    m_combo->addItem(QStringLiteral("Cmd+Shift+3 (system screenshot)"), QStringLiteral("cmd+shift+3"));
    m_combo->addItem(QStringLiteral("Cmd+Shift+4 (system screenshot)"), QStringLiteral("cmd+shift+4"));
    m_combo->addItem(QStringLiteral("Cmd+Shift+5 (system screenshot)"), QStringLiteral("cmd+shift+5"));
    layout->addWidget(m_combo);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
    qInfo() << "FirstRunWizard: opened";
}

QString FirstRunWizard::selectedHotkey() const
{
    const QString spec = m_combo->currentData().toString();
    qInfo() << "FirstRunWizard: selected" << spec;
    return spec;
}
