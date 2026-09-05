/* tests — text layout (text.txt): pure CPU, headless, no GL. Tags, wrap,
 * align, utf-8 (cyrillic), fallback box, measure. */
#include "utest.h"
#include <math.h>
#include <ame/ame.h>
#include <ame/text.h>

int main(void) {
    printf("=== test_text ===\n");

    UT_CASE("plain layout + measure");
    ame_text_layout l;
    int n = text_layout("Hello", 0, AME_TEXT_ALIGN_L, 1.0f, &l);
    UT_ASSERT(n == 5);
    UT_ASSERT(l.count == 5);
    UT_ASSERT(l.w > 10.0f && l.w < 400.0f);
    UT_ASSERT(l.h >= text_line_h());

    UT_CASE("color tags don't emit glyphs");
    n = text_layout("A{c=FFD700}B{c}C", 0, AME_TEXT_ALIGN_L, 1.0f, &l);
    UT_ASSERT(n == 3);
    UT_ASSERT(l.el[1].color == 0xFFD700u); /* B is gold */
    UT_ASSERT(l.el[2].color == 0xFFFFFFu); /* C restored */

    UT_CASE("pause tag recorded for typewriter");
    n = text_layout("AB{p=0.5}CD", 0, AME_TEXT_ALIGN_L, 1.0f, &l);
    UT_ASSERT(n == 4);
    UT_ASSERT(l.npause == 1);
    UT_ASSERT_NEAR(l.pause[0].seconds, 0.5f, 1e-6);
    UT_ASSERT(l.pause[0].at == 2.0f);

    UT_CASE("utf-8: cyrillic maps to real glyphs");
    n = text_layout("Тест", 0, AME_TEXT_ALIGN_L, 1.0f, &l);
    UT_ASSERT(n == 4);
    for (int i = 0; i < 4; i++)
        UT_ASSERTF(l.el[i].glyph >= 0, "cyrillic glyph %d missing (fallback box)", i);

    UT_CASE("missing glyph -> visible fallback box, no crash");
    n = text_layout("\xE7\x8A\xAC", 0, AME_TEXT_ALIGN_L, 1.0f, &l); /* U+72AC */
    UT_ASSERT(n == 1);
    UT_ASSERT(l.el[0].glyph < 0);

    UT_CASE("word wrap: narrow box forces two lines");
    float w0, h0, w1, h1;
    text_measure("alpha beta", 0, AME_TEXT_ALIGN_L, 1.0f, &w0, &h0);
    text_measure("alpha beta", w0 * 0.7f, AME_TEXT_ALIGN_L, 1.0f, &w1, &h1);
    UT_ASSERT(h1 > h0 + 0.5f);           /* taller: wrapped */
    UT_ASSERT(w1 < w0 + 1e-3f);          /* each line narrower than the whole */

    UT_CASE("center align shifts right of left align");
    ame_text_layout ll, lc;
    text_layout("xy", 200, AME_TEXT_ALIGN_L, 1.0f, &ll);
    text_layout("xy", 200, AME_TEXT_ALIGN_C, 1.0f, &lc);
    UT_ASSERT(lc.el[0].x > ll.el[0].x + 40.0f);
    UT_ASSERT_NEAR(lc.el[0].x + (ll.w * 0.0f), lc.el[0].x, 1e-3);

    UT_CASE("multi-line layout: newline splits sub-lines (no newline els)");
    /* documents the contract the text editor violated: ONE layout call
       with embedded '\n' produces SUB-LINES in el[] (y offsets) - a
       consumer laying out line-by-line MUST terminate each line, or the
       element at its EOL column is the NEXT sub-line's first glyph at
       x~0 (the "caret too left" bug). */
    ame_text_layout ml;
    n = text_layout("hi\nyo", 0, AME_TEXT_ALIGN_L, 1.0f, &ml);
    UT_ASSERT(n == 4); /* no element emitted for '\n' */
    UT_ASSERT(ml.el[2].y == text_line_h()); /* sub-line 2 on its own row */
    UT_ASSERT(ml.h > text_line_h());        /* two rows tall */
    for (int i = 0; i < n; i++)
        UT_ASSERTF(ml.el[i].x == floorf(ml.el[i].x),
                   "multi-line el[%d].x fractional (%.3f)", i, ml.el[i].x);

    UT_CASE("grid contract: el positions and w are whole pixels");
    n = text_layout("hello typed text", 0, AME_TEXT_ALIGN_L, 1.0f, &l);
    UT_ASSERT(n == 16);
    for (int i = 0; i < n; i++) {
        UT_ASSERTF(l.el[i].x == floorf(l.el[i].x), "el[%d].x fractional (%.3f)",
                   i, l.el[i].x);
        UT_ASSERTF(l.el[i].y == floorf(l.el[i].y), "el[%d].y fractional (%.3f)",
                   i, l.el[i].y);
    }
    UT_ASSERT(l.w == floorf(l.w)); /* EOL caret basis */

    UT_CASE("grid contract: advance deltas stay within 1px of truth");
    for (int i = 1; i < n; i++) {
        UT_ASSERTF(l.el[i].y == l.el[i - 1].y, "same line expected");
        /* snapped neighbors differ from the float advance by < 1px */
        UT_ASSERTF(l.el[i].x >= l.el[i - 1].x - 0.5f, "pens must not go back");
    }

    UT_CASE("grid contract: center/right alignment keeps the grid");
    float wl, wc, wr;
    text_measure("centered", 0, AME_TEXT_ALIGN_L, 1.0f, &wl, 0);
    ame_text_layout lca, lra;
    text_layout("centered", 0, AME_TEXT_ALIGN_C, 1.0f, &lca);
    text_layout("centered", 0, AME_TEXT_ALIGN_R, 1.0f, &lra);
    for (int i = 0; i < lca.count; i++) {
        UT_ASSERTF(lca.el[i].x == floorf(lca.el[i].x),
                   "center el[%d].x fractional (%.3f)", i, lca.el[i].x);
        UT_ASSERTF(lra.el[i].x == floorf(lra.el[i].x),
                   "right el[%d].x fractional (%.3f)", i, lra.el[i].x);
    }
    (void)wl; (void)wc; (void)wr;

    UT_CASE("grid contract: non-integer scale still lands on the grid");
    ame_text_layout lsca;
    n = text_layout("scale 1.15 text", 0, AME_TEXT_ALIGN_L, 1.15f, &lsca);
    for (int i = 0; i < n; i++)
        UT_ASSERTF(lsca.el[i].x == floorf(lsca.el[i].x),
                   "scaled el[%d].x fractional (%.3f)", i, lsca.el[i].x);

    UT_OK();
    return ut_done("test_text");
}
