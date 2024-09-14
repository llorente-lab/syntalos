#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>

class OrbbecModule;

class OrbbecSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OrbbecSettingsDialog(OrbbecModule *module, QWidget *parent = nullptr);
    ~OrbbecSettingsDialog();

    QString subjectName() const { return m_subjectName; }
    QString sessionName() const { return m_sessionName; }
    QString metadataPath() const { return m_metadataPath; }

    void setSubjectName(const QString &name);
    void setSessionName(const QString &name);
    void setMetadataPath(const QString &path);

    void readCurrentValues();
    void applyValues();

private slots:
    void onSubjectNameChanged(const QString &arg1);
    void onSessionNameChanged(const QString &arg1);
    void onMetadataPathButtonClicked();

private:
    void setupUi();

    OrbbecModule *m_module;
    QLineEdit *m_subjectNameLineEdit;
    QLineEdit *m_sessionNameLineEdit;
    QLineEdit *m_metadataPathLineEdit;
    QPushButton *m_metadataPathButton;
    QPushButton *m_applyButton;
    QPushButton *m_cancelButton;

    QString m_subjectName;
    QString m_sessionName;
    QString m_metadataPath;
};