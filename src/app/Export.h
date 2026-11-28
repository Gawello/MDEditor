#pragma once

#include <QFont>
#include <QString>

#include <functional>

class QTextDocument;

// A note as a printable document (File → Export to PDF / HTML): rendered Markdown without the front matter,
// [[links]] as their text, images from the note's folder fitted to the page width.
namespace Export {

struct Options {
    QString title;      // shown as the first heading when the note doesn't start with one
    QString folder;     // the note's folder: relative images resolve there
    QFont font;
    qreal pageWidth = 0; // in the document's units; 0: no fitting
    // The text of a note an embed (![[Note]], ![[Note#Heading]]) shows, already cut to the section; empty when
    // there's none. Without it embeds show the note's name.
    std::function<QString(const QString &target)> embed;
    bool links = false; // [[links]] as links (the reading view), not text
};

// The caller owns the document.
QTextDocument *document(const QString &markdown, const Options &options);
// A4, 20 mm margins.
bool toPdf(const QString &markdown, const Options &options, const QString &path, QString *error);
bool toHtml(const QString &markdown, const Options &options, const QString &path, QString *error);

} // namespace Export
