#include "TestHarness.h"
#include "markdown/AutoPair.h"

using namespace md;

namespace {

PairDecision at(char16_t typed, std::u16string_view before, std::u16string_view after, bool selection = false,
                PairOptions options = {})
{
    return pairFor(typed, before, after, selection, options);
}

} // namespace

TEST_CASE(brackets_pair_where_a_phrase_can_start)
{
    CHECK(at(u'(', u"see ", u"").action == PairAction::Insert);
    CHECK(at(u'(', u"see ", u"").close == u')');
    CHECK(at(u'[', u"", u" more").close == u']');
    CHECK(at(u'{', u"x", u").").action == PairAction::Insert);
    CHECK(at(u'(', u"", u"word").action == PairAction::None); // before a word: just the bracket
    // [[ : the second '[' goes inside the first pair
    CHECK(at(u'[', u"[", u"]").action == PairAction::Insert);
}

TEST_CASE(quotes_pair_only_at_the_start_of_a_word)
{
    CHECK(at(u'"', u"say ", u"").action == PairAction::Insert);
    CHECK(at(u'\'', u"don", u"").action == PairAction::None);   // an apostrophe
    CHECK(at(u'\'', u"5", u"").action == PairAction::None);
    CHECK(at(u'"', u"", u"word").action == PairAction::None);
    CHECK(at(u'`', u"run ", u"").action == PairAction::Insert);
    CHECK(at(u'`', u"run ", u"", false, {true, false}).action == PairAction::None); // Markdown pairs off
    CHECK(at(u'"', u"say ", u"", false, {false, true}).action == PairAction::None); // brackets off
}

TEST_CASE(typing_the_closing_character_steps_over_it)
{
    CHECK(at(u')', u"(x", u")").action == PairAction::Skip);
    CHECK(at(u']', u"[[Note", u"]]").action == PairAction::Skip);
    CHECK(at(u'"', u"\"hi", u"\"").action == PairAction::Skip);
    CHECK(at(u'`', u"`", u"`").action == PairAction::Skip);  // `` then a third ` for a fence
    CHECK(at(u'`', u"``", u"").action == PairAction::None);
    CHECK(at(u')', u"x", u"y").action == PairAction::None);
}

TEST_CASE(selections_get_wrapped)
{
    const PairDecision bold = at(u'*', u"a ", u" b", true);
    CHECK(bold.action == PairAction::Wrap && bold.open == u'*' && bold.close == u'*');
    CHECK(at(u'(', u"", u"", true).close == u')');
    CHECK(at(u'=', u"", u"", true).action == PairAction::Wrap);
    CHECK(at(u'$', u"", u"", true).action == PairAction::Wrap);
    CHECK(at(u'x', u"", u"", true).action == PairAction::None);
    CHECK(at(u'*', u"", u"", true, {true, false}).action == PairAction::None);
    // Without a selection, * _ ~ = $ are just typed (list bullets, prices, emphasis by hand).
    CHECK(at(u'*', u"", u"").action == PairAction::None);
    CHECK(at(u'$', u"costs ", u"").action == PairAction::None);
}

TEST_CASE(backspace_removes_an_empty_pair)
{
    CHECK(deletesPair(u"x (", u")", {}));
    CHECK(deletesPair(u"\"", u"\"", {}));
    CHECK(deletesPair(u"`", u"`", {}));
    CHECK(!deletesPair(u"`", u"`", {true, false}));
    CHECK(!deletesPair(u"(", u"x)", {}));
    CHECK(!deletesPair(u"", u")", {}));
}
