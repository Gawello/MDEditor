// Commands that work on the selection.

function sortSelection(descending) {
    var text = editor.selectedText();
    if (text.length === 0) {
        app.showMessage("Select some lines first");
        return;
    }
    var lines = text.split("\n");
    lines.sort(function (a, b) { return a.localeCompare(b); });
    if (descending)
        lines.reverse();
    editor.replaceSelection(lines.join("\n"));
}

app.registerCommand({ id: "sort-asc", name: "Sort Lines (A-Z)" }, function () { sortSelection(false); });
app.registerCommand({ id: "sort-desc", name: "Sort Lines (Z-A)" }, function () { sortSelection(true); });
