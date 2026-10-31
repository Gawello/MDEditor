// An event handler: "beforeSave" receives (path, text) and may return the text to save instead.
//
// ---
// title: My note
// modified: 2024-01-01      <- updated on every save
// ---

function today() {
    var d = new Date();
    function pad(n) { return (n < 10 ? "0" : "") + n; }
    return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate());
}

app.on("beforeSave", function (path, text) {
    var match = /^---\n([\s\S]*?)\n---(\n|$)/.exec(text);
    if (!match || !/^modified:/m.test(match[1]))
        return text; // no front matter with a modified field: leave the note alone
    var frontMatter = match[1].replace(/^modified:.*$/m, "modified: " + today());
    return "---\n" + frontMatter + "\n---" + match[2] + text.substring(match[0].length);
});

app.on("open", function (path) {
    console.log("opened", path);
});
