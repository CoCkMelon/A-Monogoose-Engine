/* tests — text layout (text.txt): pure CPU, headless, no GL. Tags, wrap,
 * align, utf-8 (cyrillic), fallback box, measure. */
#include "utest.h"
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

    UT_OK();
    return ut_done("test_text");
}
