#pragma once

#include <QAbstractButton>

// Small flat close button for editor tabs. Shows a dot instead of the cross while the
// tab has unsaved changes, and the cross again when hovered.
class TabCloseButton : public QAbstractButton {
    Q_OBJECT

public:
    explicit TabCloseButton(QWidget *parent = nullptr);

    void setModified(bool modified);
    QSize sizeHint() const override { return {18, 18}; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool m_modified = false;
};
