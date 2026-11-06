#pragma once

#include <QWidget>

// A drop-down of symbols and emoji, by category, looking like TablePicker. Clicking one reports it and the
// drop-down stays open, so several can be inserted in a row. A click anywhere else, Esc, moving the main
// window or leaving the application closes it.
//
// Unlike TablePicker it isn't a Qt::Popup, which would hold the X11 grab of the whole desktop for as long as
// it's open and swallow the click that closes it. It's a frameless window that never takes the focus, so
// typing goes on in the editor.
class SymbolPicker : public QWidget {
    Q_OBJECT

public:
    // `toggleButton` opens and closes the window by itself, so clicking it must not count as clicking away.
    explicit SymbolPicker(QWidget *toggleButton = nullptr, QWidget *parent = nullptr);

    // Shows the window with its top-right corner at `globalTopRight`, kept on screen.
    void showAt(const QPoint &globalTopRight);

signals:
    void symbolPicked(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    QWidget *createPage(const QStringList &symbols);

    QWidget *m_toggleButton;
};
