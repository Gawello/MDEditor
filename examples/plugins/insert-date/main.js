// A minimal command: inserts today's date at the cursor.

function today() {
    var d = new Date();
    function pad(n) { return (n < 10 ? "0" : "") + n; }
    return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate());
}

app.registerCommand({ id: "insert-date", name: "Insert Date", shortcut: "Ctrl+Alt+D" }, function () {
    editor.insert(today());
});
