#pragma once

#include "editor/CompletionPopup.h"
#include "markdown/Completion.h"

#include <QList>
#include <QString>

class NoteIndex;

// The suggestions for what's being typed (md::completionAt): notes and their aliases, headings, block ids, tags,
// best matches first.
namespace Completions {

// detail: the folder, heading level, "→ Note" for an alias, tag count.
using Suggestion = CompletionItem;

constexpr int maxSuggestions = 50;

// `from` is the note being edited, `text` its current text (for its own headings and blocks).
// `addBlockId(path, line, text, id)` gives a block its id when a block without one is picked; without it,
// only blocks that have ids are offered.
using BlockIdAdder = std::function<void(const QString &path, int line, const QString &text, const QString &id)>;
QList<Suggestion> suggest(const NoteIndex &index, const md::CompletionContext &context, const QString &from,
                          const QString &text, const BlockIdAdder &addBlockId = {});
QString newBlockId(); // six random letters and digits, like Obsidian's

} // namespace Completions
