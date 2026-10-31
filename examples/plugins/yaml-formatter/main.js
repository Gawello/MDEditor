// A language formatter: used by Format (Shift+Alt+F) for ```yaml / ```yml code blocks.
// It receives the code and { indent } and returns the formatted code (or throws an Error).

function formatYaml(text, options) {
    var levels = [0]; // original indentation columns of the open levels
    var out = [];
    text.split("\n").forEach(function (raw, index) {
        var line = raw.replace(/\s+$/, "");
        if (line.trim().length === 0) {
            out.push("");
            return;
        }
        var column = line.length - line.replace(/^ +/, "").length;
        if (line.charAt(column) === "\t")
            throw new Error("tabs are not allowed in YAML (line " + (index + 1) + ")");

        while (levels.length > 1 && column < levels[levels.length - 1])
            levels.pop();
        if (column > levels[levels.length - 1])
            levels.push(column);

        var content = line.substring(column);
        // "key:value" -> "key: value" (only for plain keys, not inside quoted strings or URLs).
        content = content.replace(/^(-\s+)?([A-Za-z0-9_.-]+):(?=\S)(?!\/\/)/, "$1$2: ");
        out.push(new Array((levels.length - 1) * options.indent + 1).join(" ") + content);
    });
    return out.join("\n");
}

app.registerFormatter(["yaml", "yml"], formatYaml);
