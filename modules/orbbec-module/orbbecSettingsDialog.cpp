#include "orbbecSettingsDialog.h"
#include "orbbecmodule.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>

OrbbecSettingsDialog::OrbbecSettingsDialog(OrbbecModule *module, QWidget *parent)
    : QDialog(parent),
      m_module(module)
{
    setWindowTitle("Orbbec Settings");
    setupUi();
    readCurrentValues();
}

OrbbecSettingsDialog::~OrbbecSettingsDialog()
{
}

void OrbbecSettingsDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // Subject Name
    QHBoxLayout *subjectLayout = new QHBoxLayout();
    QLabel *subjectLabel = new QLabel("Subject Name:", this);
    m_subjectNameLineEdit = new QLineEdit(this);
    subjectLayout->addWidget(subjectLabel);
    subjectLayout->addWidget(m_subjectNameLineEdit);
    mainLayout->addLayout(subjectLayout);

    // Session Name
    QHBoxLayout *sessionLayout = new QHBoxLayout();
    QLabel *sessionLabel = new QLabel("Session Name:", this);
    m_sessionNameLineEdit = new QLineEdit(this);
    sessionLayout->addWidget(sessionLabel);
    sessionLayout->addWidget(m_sessionNameLineEdit);
    mainLayout->addLayout(sessionLayout);

    // Metadata Path
    QHBoxLayout *pathLayout = new QHBoxLayout();
    QLabel *pathLabel = new QLabel("Metadata Path:", this);
    m_metadataPathLineEdit = new QLineEdit(this);
    m_metadataPathButton = new QPushButton("Browse", this);
    pathLayout->addWidget(pathLabel);
    pathLayout->addWidget(m_metadataPathLineEdit);
    pathLayout->addWidget(m_metadataPathButton);
    mainLayout->addLayout(pathLayout);

    // Apply and Cancel buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    m_applyButton = new QPushButton("Apply", this);
    m_cancelButton = new QPushButton("Cancel", this);
    buttonLayout->addWidget(m_applyButton);
    buttonLayout->addWidget(m_cancelButton);
    mainLayout->addLayout(buttonLayout);

    // Connect signals
    connect(m_subjectNameLineEdit, &QLineEdit::textChanged, this, &OrbbecSettingsDialog::onSubjectNameChanged);
    connect(m_sessionNameLineEdit, &QLineEdit::textChanged, this, &OrbbecSettingsDialog::onSessionNameChanged);
    connect(m_metadataPathButton, &QPushButton::clicked, this, &OrbbecSettingsDialog::onMetadataPathButtonClicked);
    connect(m_applyButton, &QPushButton::clicked, this, &OrbbecSettingsDialog::applyValues);
    connect(m_cancelButton, &QPushButton::clicked, this, &OrbbecSettingsDialog::reject);

    setLayout(mainLayout);
}

void OrbbecSettingsDialog::readCurrentValues()
{
    m_subjectNameLineEdit->setText(m_subjectName);
    m_sessionNameLineEdit->setText(m_sessionName);
    m_metadataPathLineEdit->setText(m_metadataPath);
}

void OrbbecSettingsDialog::applyValues()
{
    m_subjectName = m_subjectNameLineEdit->text();
    m_sessionName = m_sessionNameLineEdit->text();
    m_metadataPath = m_metadataPathLineEdit->text();

    accept();
}

void OrbbecSettingsDialog::onSubjectNameChanged(const QString &arg1)
{
    m_subjectName = arg1;
}

void OrbbecSettingsDialog::onSessionNameChanged(const QString &arg1)
{
    m_sessionName = arg1;
}

void OrbbecSettingsDialog::onMetadataPathButtonClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Choose Directory"),
                                                    "/home",
                                                    QFileDialog::ShowDirsOnly
                                                    | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        m_metadataPathLineEdit->setText(dir);
        m_metadataPath = dir;
    }
}

void OrbbecSettingsDialog::setSubjectName(const QString &name)
{
    m_subjectName = name;
    m_subjectNameLineEdit->setText(name);
}

void OrbbecSettingsDialog::setSessionName(const QString &name)
{
    m_sessionName = name;
    m_sessionNameLineEdit->setText(name);
}

void OrbbecSettingsDialog::setMetadataPath(const QString &path)
{
    m_metadataPath = path;
    m_metadataPathLineEdit->setText(path);
}