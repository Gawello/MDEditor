#include "app/SymbolSets.h"

#include <QCoreApplication>

namespace {

// Space-separated, because one symbol can be several code points (pairs, variation selectors, joiners).
QStringList split(const char16_t *symbols)
{
    return QString::fromUtf16(symbols).split(u' ', Qt::SkipEmptyParts);
}

QString tr(const char *text)
{
    return QCoreApplication::translate("SymbolSets", text);
}

} // namespace

std::vector<SymbolSet> symbolSets()
{
    return {
        {tr("Arrows"), split(u"← ↑ → ↓ ⇐ ⇑ ⇒ ⇓ ⇔ ⇕ ↺ ↻ ↦ ↳ ⇄ ⇆ ➜ ➔ ➤ ▲ ▼")},
        {tr("Math"), split(u"± × ÷ ∓ ≠ ≈ ≡ ≤ ≥ ≪ ≫ ∞ ∝ √ ∛ ∑ ∏ ∫ ∂ ∇ ∆ ∈ ∉ ∋ ⊂ ⊃ ⊆ ⊇ ∪ ∩ ∅ ∀ ∃ ∄ ¬ ∧ ∨ ⊕ ⊗ ∴ ∵ ° ′ ″ ‰ ½ ⅓ ¼ ¾ ² ³")},
        {tr("Typography"), split(u"– — … • · ‣ « » ‹ › „ ” “ ‘ ’ ¡ ¿ § ¶ † ‡ © ® ™ ℠ № ⁂ ※ ‖ ¦")},
        {tr("Currency"), split(u"€ £ ¥ ¢ $ ₽ ₹ ₩ ₪ ₫ ₴ ₺ ₿ ₦ ฿ ﷼ zł")},
        {tr("Greek"), split(u"α β γ δ ε ζ η θ ι κ λ μ ν ξ ο π ρ σ ς τ υ φ χ ψ ω Α Β Γ Δ Ε Ζ Η Θ Ι Κ Λ Μ Ν Ξ Ο Π Ρ Σ Τ Υ Φ Χ Ψ Ω")},
        {tr("Marks"), split(u"✓ ✔ ✗ ✘ ☐ ☑ ☒ ★ ☆ ♥ ♡ ♦ ♣ ♠ ● ○ ◆ ◇ ■ □ ▪ ▫ ♪ ♫ ☀ ☁ ☂ ☎ ✉ ✎ ✂ ⚑ ⚐ ⚠ ⚡ ⌘ ⌥ ⇧ ⏎ ⌫ ␣")},
        {tr("Emoji"), split(
            u"😀 😃 😄 😁 😆 😅 😂 🤣 🙂 😉 😊 😇 😍 🥰 😘 😋 😜 🤔 🤨 😐 😑 😶 🙄 😏 😴 😎 🤓 🥳 😕 😟 😮 😲 😳 🥺 😢 😭 😱 😡 🤯 🥶 🤒 🤝 "
            u"👍 👎 👌 ✌️ 🤞 👏 🙌 🙏 💪 👀 👋 ✋ 👉 👈 👆 👇 🧠 👤 👥 👩‍💻 👨‍💻 "
            u"✅ ❌ ❎ ⭐ 🌟 ✨ 🔥 💡 🎉 🎯 🏆 🥇 🚀 💥 ⚡ ⚠️ 🚧 🛑 ❗ ❓ ❤️ 💔 💯 "
            u"📌 📍 📎 📝 📄 📁 📂 📅 📆 📊 📈 📉 📋 📚 📖 🔖 🏷️ 🔗 🔍 🔎 🔒 🔓 🔑 🔔 💬 💭 📧 📞 📷 🎥 🎵 🖥️ 💻 📱 ⌨️ 🖨️ 💾 🗑️ 🛠️ 🔧 🔨 ⚙️ 🧪 🐛 🤖 "
            u"⏰ ⏳ ⌛ 🕐 🌍 🌈 ☀️ 🌙 ☁️ 🌧️ ❄️ 🌱 🌳 🍀 🌸 ☕ 🍕 🍺 🐱 🐶 🏠 🏢 🚗 ✈️ 🚲 💰 💳 🎁 🇵🇱")},
    };
}
