// Reads the document and reports in the status bar.

app.registerCommand({ id: "count", name: "Word Count", shortcut: "Ctrl+Alt+W" }, function () {
    var selection = editor.selectedText();
    var text = selection.length > 0 ? selection : editor.text();
    var words = text.split(/\s+/).filter(function (w) { return w.length > 0; }).length;
    app.showMessage(words + " words, " + text.length + " characters" + (selection.length > 0 ? " (selection)" : ""));
});
