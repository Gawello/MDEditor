#include "TestHarness.h"
#include "app/SymbolPicker.h"
#include "app/SymbolSets.h"
#include "document/Document.h"
#include "editor/EditorTheme.h"
#include "editor/MarkdownEditor.h"
#include "markdown/InlineParser.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFontInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QSet>
#include <QTextDocumentFragment>
#include <QTemporaryDir>
#include <QToolButton>

namespace {

bool wellFormedUtf16(const QString &text)
{
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i].isHighSurrogate() && (i + 1 == text.size() || !text[i + 1].isLowSurrogate()))
            return false;
        if (text[i].isLowSurrogate() && (i == 0 || !text[i - 1].isHighSurrogate()))
            return false;
    }
    return true;
}

class KeyCounter : public QObject {
public:
    explicit KeyCounter(int &count)
        : m_count(count)
    {
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress)
            ++m_count;
        return false;
    }

private:
    int &m_count;
};

QToolButton *buttonFor(SymbolPicker &picker, const QString &symbol)
{
    for (QToolButton *button : picker.findChildren<QToolButton *>()) {
        if (button->text() == symbol)
            return button;
    }
    return nullptr;
}

} // namespace

TEST_CASE(symbol_sets_are_complete_and_clean)
{
    const std::vector<SymbolSet> sets = symbolSets();
    CHECK(sets.size() == 7);
    for (const SymbolSet &set : sets) {
        CHECK(!set.name.isEmpty());
        CHECK(!set.symbols.isEmpty());
        CHECK(QSet<QString>(set.symbols.begin(), set.symbols.end()).size() == set.symbols.size());
        for (const QString &symbol : set.symbols)
            CHECK(wellFormedUtf16(symbol));
    }
}

TEST_CASE(symbol_sets_hold_arrows_and_multi_code_point_emoji)
{
    const std::vector<SymbolSet> sets = symbolSets();
    CHECK(sets.front().symbols.contains(QStringLiteral("→")));
    const QStringList &emoji = sets.back().symbols;
    CHECK(emoji.contains(QStringLiteral("🚀")));
    CHECK(emoji.contains(QString::fromUtf8("\xE2\x9D\xA4\xEF\xB8\x8F")));                 // heart + variation selector
    CHECK(emoji.contains(QString::fromUtf8("\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB"))); // woman + ZWJ + laptop
    CHECK(emoji.size() >= 150);
}

TEST_CASE(symbol_picker_reports_the_symbol_and_stays_open)
{
    SymbolPicker picker;
    QStringList picked;
    QObject::connect(&picker, &SymbolPicker::symbolPicked, &picker, [&](const QString &text) { picked << text; });
    picker.show();

    QToolButton *arrow = buttonFor(picker, QStringLiteral("→"));
    QToolButton *rocket = buttonFor(picker, QStringLiteral("🚀"));
    CHECK(arrow != nullptr);
    CHECK(rocket != nullptr);
    if (!arrow || !rocket)
        return;
    arrow->click();
    CHECK(picker.isVisible());
    rocket->click();
    CHECK(picker.isVisible());
    CHECK(picked == QStringList({QStringLiteral("→"), QStringLiteral("🚀")}));
}

namespace {

QString shownTooltip(const QToolButton *button)
{
    return QTextDocumentFragment::fromHtml(button->toolTip()).toPlainText();
}

} // namespace

TEST_CASE(symbol_tooltips_show_how_to_type_the_symbol)
{
    SymbolPicker picker;
    CHECK(shownTooltip(buttonFor(picker, QStringLiteral("→"))).startsWith(QStringLiteral("&rarr;")));
    CHECK(shownTooltip(buttonFor(picker, QStringLiteral("→"))).contains(QStringLiteral("U+2192")));
    CHECK(shownTooltip(buttonFor(picker, QStringLiteral("🚀"))).startsWith(QStringLiteral("&#x1F680;")));
    CHECK(shownTooltip(buttonFor(picker, QStringLiteral("…"))).startsWith(QStringLiteral("&hellip;")));
}

// What the tooltip tells people to type has to give them exactly that symbol.
TEST_CASE(symbol_tooltip_entities_all_give_the_symbol_back)
{
    SymbolPicker picker;
    int checked = 0;
    for (const SymbolSet &set : symbolSets()) {
        for (const QString &symbol : set.symbols) {
            QToolButton *button = buttonFor(picker, symbol);
            CHECK(button != nullptr);
            if (!button)
                continue;
            const QString shown = shownTooltip(button);
            const QString typed = shown.left(shown.indexOf(QStringLiteral("U+"))).trimmed();
            CHECK(typed.contains(u'&') || typed == symbol);

            const std::u16string text = typed.toStdU16String();
            const md::InlineResult parsed = md::parseInline(text);
            QString decoded;
            int position = 0;
            for (const md::EntityRange &entity : parsed.entities) {
                decoded += typed.mid(position, entity.start - position) + QString::fromStdU16String(entity.text);
                position = entity.end;
            }
            decoded += typed.mid(position);
            CHECK(decoded == symbol);
            ++checked;
        }
    }
    CHECK(checked > 300);
}

namespace {

void pressMouse(QWidget *widget)
{
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(5, 5), widget->mapToGlobal(QPointF(5, 5)), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(5, 5), widget->mapToGlobal(QPointF(5, 5)), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &release);
}

} // namespace

TEST_CASE(symbol_picker_closes_on_a_click_in_the_text_and_the_click_still_lands)
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("some text"));
    editor.resize(400, 200);
    editor.show();
    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::End);
    editor.setTextCursor(cursor);

    SymbolPicker picker;
    picker.show();
    pressMouse(editor.viewport());
    CHECK(!picker.isVisible());
    CHECK(editor.textCursor().position() == 0); // the press wasn't swallowed
}

TEST_CASE(symbol_picker_stays_open_for_clicks_on_itself_and_on_its_button)
{
    QWidget toggle;
    toggle.show();
    SymbolPicker picker(&toggle);
    picker.show();

    pressMouse(buttonFor(picker, QStringLiteral("→")));
    CHECK(picker.isVisible());
    pressMouse(&toggle); // its click handler does the toggling
    CHECK(picker.isVisible());
}

TEST_CASE(symbol_picker_stays_open_while_typing)
{
    MarkdownEditor editor;
    editor.show();
    SymbolPicker picker;
    picker.show();
    QKeyEvent key(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
    QApplication::sendEvent(editor.viewport(), &key);
    CHECK(picker.isVisible());
}

TEST_CASE(symbol_picker_watches_clicks_only_while_open)
{
    QWidget other;
    other.show();
    SymbolPicker picker;
    picker.show();
    pressMouse(&other);
    CHECK(!picker.isVisible());
    picker.show(); // and it works again after reopening
    pressMouse(&other);
    CHECK(!picker.isVisible());
}

// Going between our own windows can pass through "inactive" for a moment. Really leaving the application
// can't be simulated offscreen, where the application stays active.
TEST_CASE(symbol_picker_survives_a_moment_of_inactivity)
{
    SymbolPicker picker;
    picker.show();
    emit qGuiApp->applicationStateChanged(Qt::ApplicationInactive);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 400)
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    CHECK(picker.isVisible());
}

// Esc goes to the editor, which has the focus: the drop-down never does.
TEST_CASE(symbol_picker_escape_closes_it_and_is_used_up)
{
    QLineEdit field;
    field.show();
    int escapesSeen = 0;
    KeyCounter counter(escapesSeen);
    field.installEventFilter(&counter);

    SymbolPicker picker;
    picker.show();
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&field, &escape);
    CHECK(!picker.isVisible());
    CHECK(escapesSeen == 0);

    QApplication::sendEvent(&field, &escape); // closed: Esc is an ordinary key again
    CHECK(escapesSeen == 1);
}

TEST_CASE(symbol_picker_closes_when_its_window_moves)
{
    QWidget window;
    window.show();
    SymbolPicker picker(nullptr, &window);
    picker.show();
    QMoveEvent move(QPoint(50, 50), QPoint(0, 0));
    QApplication::sendEvent(&window, &move);
    CHECK(!picker.isVisible());
}

// Qt::Popup would hold the X11 grab of the whole desktop for as long as it stays open, and the focus would
// keep typing away from the editor.
TEST_CASE(symbol_picker_is_a_frameless_window_without_focus)
{
    SymbolPicker picker;
    CHECK(picker.windowType() == Qt::Tool);
    CHECK(picker.windowFlags().testFlag(Qt::FramelessWindowHint));
    CHECK(picker.windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
}

TEST_CASE(symbols_go_in_at_the_cursor_and_replace_a_selection)
{
    MarkdownEditor editor;
    editor.setMarkdown(QStringLiteral("a b"));
    QTextCursor cursor = editor.textCursor();
    cursor.setPosition(1);
    editor.setTextCursor(cursor);
    editor.insertPlainText(QStringLiteral("→"));
    editor.insertPlainText(QStringLiteral("🚀"));
    CHECK(editor.markdown() == QStringLiteral("a→🚀 b"));

    cursor = editor.textCursor();
    cursor.setPosition(0);
    cursor.setPosition(1, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);
    editor.insertPlainText(QStringLiteral("β"));
    CHECK(editor.markdown() == QStringLiteral("β→🚀 b"));
    editor.undo();
    CHECK(editor.markdown() == QStringLiteral("a→🚀 b"));
}

TEST_CASE(symbols_survive_saving_byte_for_byte)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.md"));
    const QString text = QStringLiteral("→ ✅ 🚀 ") + QString::fromUtf8("\xE2\x9D\xA4\xEF\xB8\x8F");
    Document document;
    CHECK(Document::write(path, document.encode(text)).ok);
    CHECK(Document::read(path).text == text);
}

// An alias in a longer list silently loses its meaning: code stopped being monospace that way.
TEST_CASE(emoji_font_list_starts_with_the_resolved_family)
{
    QFont alias(QStringLiteral("Monospace"));
    QStringList families = EditorTheme::withEmoji(alias);
    CHECK(!families.isEmpty());
    CHECK(families.value(0) == QFontInfo(alias).family());
    CHECK(families.removeDuplicates() == 0);
}
