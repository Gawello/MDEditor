#pragma once

#include <QString>

// A Markdown file on disk. Remembers the encoding details of the loaded file
// (UTF-8 BOM, CRLF line endings) so that saving doesn't rewrite every line.
class Document {
public:
    QString filePath() const { return m_path; }
    bool isUntitled() const { return m_path.isEmpty(); }
    QString displayName() const;

    void reset();
    bool load(const QString &path, QString &text, QString &error);
    bool save(const QString &path, const QString &text, QString &error);

private:
    QString m_path;
    bool m_crlf = false;
    bool m_bom = false;
};
