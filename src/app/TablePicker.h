#pragma once

#include <QWidget>

// Word-style popup grid for choosing the size of a new table: hover to pick
// columns × rows, click (or Enter) to insert. Arrow keys work too.
class TablePicker : public QWidget {
    Q_OBJECT

public:
    explicit TablePicker(QWidget *parent = nullptr);

    // Shows the picker with its top-left corner at `globalPos`, kept on screen.
    void popup(const QPoint &globalPos);

signals:
    void picked(int rows, int columns);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    static constexpr int gridRows = 8;
    static constexpr int gridColumns = 10;
    static constexpr int cellSize = 18;
    static constexpr int gap = 3;
    static constexpr int margin = 10;
    static constexpr int labelHeight = 26;

    QRect cellRect(int row, int column) const;
    void choose();

    int m_rows = 0;
    int m_columns = 0;
};
