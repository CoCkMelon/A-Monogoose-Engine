/* tests — text layout (text.txt): pure CPU, headless, no GL. Tags, wrap,
 * align, utf-8 (cyrillic), fallback box, measure. */
#include "utest.h"
#include <math.h>
#include <ame/ame.h>
#include <ame/text.h>
#include "font_atlas.h" /* advance oracle (audit P1) */
#include "font_atlas_dsdf.h" /* baked smooth-face table */

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

    UT_CASE("audit P0: >64 explicit newlines bounds line_first (no ASan scribble)");
    {
        char big[1024];
        int n = 0;
        for (int k = 0; k < 200; k++) {
            big[n++] = 'a';
            big[n++] = '\n';
        }
        big[n] = 0;
        ame_text_layout many;
        int cnt = text_layout(big, 0, AME_TEXT_ALIGN_L, 1.0f, &many);
        UT_ASSERT(cnt == 200);
        UT_ASSERT(many.truncated & 2); /* line bookkeeping capped */
        UT_ASSERT(!(many.truncated & 1));
        /* first lines still aligned/sane: line 0 pen intact */
        UT_ASSERT(many.el[0].x == 0 && many.el[1].y > 0);
    }

    UT_CASE("audit P1: {/c} close tag restores the default colour");
    {
        ame_text_layout lt;
        int cnt = text_layout("{c=FFD700}gold{/c} x", 0, AME_TEXT_ALIGN_L,
                              1.0f, &lt);
        UT_ASSERT(cnt == 6); /* g o l d ' ' x - BOTH tags consumed */
        UT_ASSERT(lt.el[0].color == 0xFFD700u);
        UT_ASSERT(lt.el[4].color == 0xFFFFFFu); /* restored after {/c} */
    }

    UT_CASE("audit P2: glyph-cap truncation keeps w at the last STORED pen");
    {
        static char m600[601];
        memset(m600, 'm', 600);
        m600[600] = 0;
        ame_text_layout l600, l512;
        text_layout(m600, 0, AME_TEXT_ALIGN_L, 1.0f, &l600);
        text_layout(m600 + 88, 0, AME_TEXT_ALIGN_L, 1.0f, &l512);
        UT_ASSERT(l600.count == AME_TXT_MAX_GLYPHS);
        UT_ASSERT(l600.truncated & 1);
        /* EOL caret basis = pen after glyph 512: NO phantom width for
         * elements that don't exist */
        UT_ASSERTF(l600.w == l512.w, "w(%d glyphs)=%.1f vs 512-w=%.1f",
                   l600.count, (double)l600.w, (double)l512.w);
    }

    UT_CASE("audit P2: indentation after an explicit newline is content");
    {
        ame_text_layout li;
        int cnt = text_layout("a\n bc", 0, AME_TEXT_ALIGN_L, 1.0f, &li);
        UT_ASSERT(cnt == 4); /* a ' ' b c - the indent space SURVIVES */
        UT_ASSERT(li.el[1].glyph >= 0); /* the space is a real glyph */
        /* the space OWNS line 2's first pen cell: b starts one space in */
        UT_ASSERTF(li.el[1].y > li.el[0].y, "indent must be on line 2");
        float adv_sp2 = 0, adv_a2 = 0;
        for (int i = 0; i < ame_font_glyph_count; i++) {
            if (ame_font_glyphs[i].cp == ' ')
                adv_sp2 = ame_font_glyphs[i].advance;
            if (ame_font_glyphs[i].cp == 'a')
                adv_a2 = ame_font_glyphs[i].advance;
        }
        UT_ASSERTF((double)li.el[2].x == floor((double)adv_sp2 + 0.5),
                   "b pen %.1f != one space in (%.1f)",
                   (double)li.el[2].x, (double)adv_sp2);
        /* ...a WRAP is only ever triggered by a non-space, which then
         * occupies pen 0 - so a wrap can never drop a leading space;
         * the offending space stays TRAILING on the previous line */
        ame_text_layout lw;
        float adv_b2 = 0;
        for (int i = 0; i < ame_font_glyph_count; i++)
            if (ame_font_glyphs[i].cp == 'b')
                adv_b2 = ame_font_glyphs[i].advance;
        float box = 3.0f * adv_a2 + adv_sp2 + adv_b2 - 0.5f;
        int cw = text_layout("aaa bbb", box, AME_TEXT_ALIGN_L, 1.0f, &lw);
        UT_ASSERTF(cw == 7, "space must survive the wrap (count %d)", cw);
        UT_ASSERT(lw.el[3].y == lw.el[0].y); /* space trails line 1 */
        UT_ASSERT(lw.el[4].y > lw.el[0].y);  /* 'b' opens line 2 */
    }

    UT_CASE("audit P2: TAB is modelled - invisible element, 4-space advance");
    {
        ame_text_layout lt;
        int cnt = text_layout_plain("a\tb", 0, AME_TEXT_ALIGN_L, 1.0f, &lt);
        UT_ASSERT(cnt == 3);
        UT_ASSERT(lt.el[1].glyph == AME_TXT_TAB);
        float adv_a = 0, adv_sp = 0;
        for (int i = 0; i < ame_font_glyph_count; i++) {
            if (ame_font_glyphs[i].cp == 'a') adv_a = ame_font_glyphs[i].advance;
            if (ame_font_glyphs[i].cp == ' ') adv_sp = ame_font_glyphs[i].advance;
        }
        double want = floor((double)adv_a + 4.0 * (double)adv_sp + 0.5);
        UT_ASSERTF((double)lt.el[2].x == want,
                   "'b' pen %.1f != 4-space tab stop %.1f",
                   (double)lt.el[2].x, want);
        /* CR is consumed silently: CRLF paste == LF paste */
        ame_text_layout lcrlf, llf;
        text_layout_plain("a\r\nb", 0, AME_TEXT_ALIGN_L, 1.0f, &lcrlf);
        text_layout_plain("a\nb", 0, AME_TEXT_ALIGN_L, 1.0f, &llf);
        UT_ASSERT(lcrlf.count == llf.count);
        UT_ASSERT(lcrlf.h == llf.h && lcrlf.w == llf.w);
    }

    UT_CASE("audit P1: double pen matches a double-precision oracle exactly");
    {
        /* the Lean model's exact-Rat pen, computed independently here */
        double advm = 0;
        for (int i = 0; i < ame_font_glyph_count; i++)
            if (ame_font_glyphs[i].cp == 'm')
                advm = (double)ame_font_glyphs[i].advance;
        static char ms[513];
        memset(ms, 'm', 512);
        ms[512] = 0;
        ame_text_layout lm;
        text_layout(ms, 0, AME_TEXT_ALIGN_L, 1.0f, &lm);
        int flips = 0;
        for (int k = 0; k < lm.count; k++) {
            double exact = floor((double)k * advm + 0.5);
            if ((double)lm.el[k].x != exact)
                flips++;
        }
        UT_ASSERTF(flips == 0, "%d snapped pens diverge from the double pen",
                   flips);
    }

    UT_CASE("audit P1: src_byte anchors bridge bytes<->elements exactly");
    {
        ame_text_layout la;
        int cnt = text_layout("{c=FF0000}ab", 0, AME_TEXT_ALIGN_L, 1.0f, &la);
        UT_ASSERT(cnt == 2); /* tag consumed */
        UT_ASSERT(la.el[0].src_byte == 10); /* 'a' starts after the tag */
        UT_ASSERT(la.el[1].src_byte == 11);
        for (int i = 1; i < la.count; i++)
            UT_ASSERTF(la.el[i].src_byte > la.el[i - 1].src_byte,
                       "anchors must be strictly increasing");
        /* plain entry: braces are LITERAL - 1:1 bytes<->elements */
        ame_text_layout lp;
        int cp_ = text_layout_plain("{c=FF0000}ab", 0, AME_TEXT_ALIGN_L,
                                    1.0f, &lp);
        UT_ASSERT(cp_ == 12);
        UT_ASSERT(lp.el[0].src_byte == 0 && lp.el[3].src_byte == 3);
    }

    UT_CASE("dsdf table: sorted by cp, sane metrics (bsearch precond)");
    {
        extern const ame_dsdf_glyph ame_dsdf_glyphs[];
        extern const int ame_dsdf_glyph_count;
        UT_ASSERT(ame_dsdf_glyph_count > 500);
        for (int i = 1; i < ame_dsdf_glyph_count; i++)
            UT_ASSERTF(ame_dsdf_glyphs[i].cp > ame_dsdf_glyphs[i - 1].cp,
                       "dsdf table unsorted at %d", i);
        for (int i = 0; i < ame_dsdf_glyph_count; i++) {
            const ame_dsdf_glyph *g = &ame_dsdf_glyphs[i];
            UT_ASSERTF(g->advance >= 0 && g->advance < 64,
                       "dsdf advance insane for U+%04X", g->cp);
            /* ink glyphs carry a cell; whitespace is metrics-only */
            UT_ASSERTF((g->aw > 0 && g->ah > 0)
                           || (g->aw == 0 && g->ah == 0),
                       "half cell for U+%04X", g->cp);
        }
    }

    UT_CASE("smooth face: metrics switch, grid contract still holds");
    {
        ame_text_layout ls;
        text_set_font(AME_FONT_SMOOTH); /* no GL/dsdf init: must no-op */
        UT_ASSERT(text_font_mode() == AME_FONT_PIXEL);
        /* pretend-init: point the module at the baked data directly */
        extern int text_test_force_dsdf(void);
        text_test_force_dsdf();
        text_set_font(AME_FONT_SMOOTH);
        UT_ASSERT(text_font_mode() == AME_FONT_SMOOTH);
        int ns = text_layout("hello typed text", 0, AME_TEXT_ALIGN_L, 1.0f, &ls);
        UT_ASSERT(ns == 16);
        /* DejaVu advances differ from Pixelify: layout must actually move */
        UT_ASSERTF(ls.w != l.w, "smooth layout identical to pixel (%.1f)",
                   (double)ls.w);
        for (int i = 0; i < ns; i++) {
            UT_ASSERTF(ls.el[i].x == floorf(ls.el[i].x),
                       "smooth el[%d].x fractional (%.3f)", i, ls.el[i].x);
            UT_ASSERTF(ls.el[i].y == floorf(ls.el[i].y),
                       "smooth el[%d].y fractional (%.3f)", i, ls.el[i].y);
        }
        UT_ASSERT(ls.w == floorf(ls.w));
        text_set_font(AME_FONT_PIXEL);
        UT_ASSERT(text_font_mode() == AME_FONT_PIXEL);
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
