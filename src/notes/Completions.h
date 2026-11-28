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
QList<Suggestion> suggest(const NoteIndex &index, const md::CompletionContext &context, const QString &from,
                          const QString &text);

} // namespace Completions
