// text_wrap.hpp — Breaking a run of text into lines that fit a width.
//
// Pulled out of the renderer so it can be tested without a font. The widths
// come from a callable rather than from ImFont, which is the only thing the
// drawing side has that a test does not.
//
// FontStrings never wrapped at all before this: every draw went through one
// AddText at an unbounded width, so a label given a size by its XML or by two
// anchors drew a single line straight out of its own frame.
#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace wowee::ui {

/// A piece of text and whether it carries a colour of its own. The renderer's
/// markup parser produces these; the wrap works on them rather than on the
/// stripped string so a colour run split across two lines keeps its colour on
/// both.
struct WrapRun {
    std::string text;
    bool  hasColor = false;
    float rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};

/// Whether two runs are the same style, so adjacent pieces can be merged back
/// into one rather than left as a string of one-word runs.
inline bool sameStyle(const WrapRun& a, const WrapRun& b) {
    if (a.hasColor != b.hasColor) return false;
    if (!a.hasColor) return true;
    return std::equal(std::begin(a.rgba), std::end(a.rgba), std::begin(b.rgba));
}

/// Break runs into lines no wider than wrapWidth.
///
/// `measure` answers the width of a string. A wrapWidth of zero means no
/// wrapping, which is what an auto-sized label wants — it is as wide as its
/// text and there is nothing to break.
///
/// Words, not characters, unless a single word is wider than the whole box and
/// nonSpaceWrap says it may be broken. Thirty-six labels in FrameXML ask for
/// that, all of them prose in a narrow column.
template <typename Measure>
std::vector<std::vector<WrapRun>> wrapText(const std::vector<WrapRun>& runs,
                                           float wrapWidth, bool nonSpaceWrap,
                                           Measure measure) {
    std::vector<std::vector<WrapRun>> lines;
    lines.emplace_back();
    if (wrapWidth <= 0.0f) {
        lines.back() = runs;
        return lines;
    }

    float x = 0.0f;
    auto place = [&](const WrapRun& style, const std::string& piece) {
        if (!lines.back().empty() && sameStyle(lines.back().back(), style)) {
            lines.back().back().text += piece;
        } else {
            WrapRun add = style;
            add.text = piece;
            lines.back().push_back(std::move(add));
        }
    };

    for (const WrapRun& run : runs) {
        size_t at = 0;
        while (at < run.text.size()) {
            // A word and the spaces that follow it, so a break falls between
            // words and the trailing space stays with the line above.
            size_t end = run.text.find(' ', at);
            if (end == std::string::npos) {
                end = run.text.size();
            } else {
                while (end < run.text.size() && run.text[end] == ' ') ++end;
            }
            std::string word = run.text.substr(at, end - at);
            at = end;

            float w = measure(word);
            if (x > 0.0f && x + w > wrapWidth) {
                lines.emplace_back();
                x = 0.0f;
                // The break stands in for the space that separated them.
                while (!word.empty() && word.front() == ' ') word.erase(0, 1);
                w = measure(word);
            }

            if (w > wrapWidth && nonSpaceWrap) {
                // One word wider than the whole box. Broken by character,
                // which is the only thing left and what the attribute asks for.
                std::string part;
                for (char c : word) {
                    if (!part.empty() && x + measure(part + c) > wrapWidth) {
                        place(run, part);
                        lines.emplace_back();
                        x = 0.0f;
                        part.clear();
                    }
                    part += c;
                }
                if (!part.empty()) {
                    place(run, part);
                    x += measure(part);
                }
                continue;
            }

            place(run, word);
            x += w;
        }
    }
    return lines;
}

}  // namespace wowee::ui
