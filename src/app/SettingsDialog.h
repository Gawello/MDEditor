#pragma once

#include <QDialog>

class QListWidget;
class QStackedWidget;

// Obsidian-style settings: a list of sections on the left, the selected page on the right.
// Every control writes straight to AppSettings, so changes apply live.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void showPage(const QString &title);

private:
    void addPage(const QString &title, QWidget *page);
    QWidget *createAppearancePage();
    QWidget *createEditorPage();
    QWidget *createFormattingPage();
    QWidget *createPluginsPage();

    QListWidget *m_sections;
    QStackedWidget *m_pages;
};
