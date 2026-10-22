#include "tc_test.h"

#include "tc_surface.h"

void tc_suite_surface(void);

static void test_new_surface_is_black(void)
{
    tc_surface s;
    TC_CHECK_INT(tc_surface_init(&s, 4, 3), TC_OK);
    TC_CHECK_INT(s.stride, 4);
    TC_CHECK_INT(tc_surface_get(&s, 3, 2), 0);
    tc_surface_free(&s);
}

static void test_fill_is_clipped(void)
{
    tc_surface s;
    tc_surface_init(&s, 4, 4);
    tc_surface_fill(&s, tc_rect_make(-10, 2, 12, 100), TC_RGB(1, 2, 3));
    TC_CHECK_INT(tc_surface_get(&s, 0, 1), 0);
    TC_CHECK_INT(tc_surface_get(&s, 0, 2), 0x010203);
    TC_CHECK_INT(tc_surface_get(&s, 1, 3), 0x010203);
    TC_CHECK_INT(tc_surface_get(&s, 2, 3), 0);
    tc_surface_free(&s);
}

static void test_blend_half_alpha(void)
{
    TC_CHECK_INT(tc_color_blend(0x000000, TC_RGBA(255, 255, 255, 128)), 0x808080);
    TC_CHECK_INT(tc_color_blend(0x123456, TC_RGBA(9, 9, 9, 0)), 0x123456);
    TC_CHECK_INT(tc_color_blend(0x123456, TC_RGBA(9, 9, 9, 255)), 0x090909);
}

static void test_transparent_fill_does_nothing(void)
{
    tc_surface s;
    tc_surface_init(&s, 2, 2);
    tc_surface_fill(&s, tc_rect_make(0, 0, 2, 2), TC_RGB(200, 0, 0));
    tc_surface_fill(&s, tc_rect_make(0, 0, 2, 2), TC_RGBA(0, 0, 255, 0));
    TC_CHECK_INT(tc_surface_get(&s, 1, 1), 0xC80000);
    tc_surface_free(&s);
}

static void test_gradient_endpoints(void)
{
    tc_surface s;
    tc_surface_init(&s, 1, 10);
    tc_surface_gradient_v(&s, tc_rect_make(0, 0, 1, 10), TC_RGB(0, 0, 255), TC_RGB(0, 0, 0));
    TC_CHECK_INT(tc_surface_get(&s, 0, 0), 0x0000FF);
    TC_CHECK_INT(tc_surface_get(&s, 0, 9), 0x000000);
    TC_CHECK(TC_BLUE(tc_surface_get(&s, 0, 4)) < 255 && TC_BLUE(tc_surface_get(&s, 0, 4)) > 0);
    tc_surface_free(&s);
}

static void test_wrapped_surface_uses_stride(void)
{
    uint32_t pixels[3 * 2] = { 0 };
    tc_surface s;
    tc_surface_wrap(&s, pixels, 2, 2, 3);
    tc_surface_fill(&s, tc_rect_make(0, 1, 2, 1), TC_RGB(0, 255, 0));
    TC_CHECK_INT(pixels[2], 0);
    TC_CHECK_INT(pixels[3], 0x00FF00);
    TC_CHECK_INT(pixels[4], 0x00FF00);
    TC_CHECK_INT(pixels[5], 0);
    tc_surface_free(&s);
    TC_CHECK_INT(pixels[3], 0x00FF00);
}

static void test_rect_intersect(void)
{
    tc_rect r = tc_rect_intersect(tc_rect_make(0, 0, 10, 10), tc_rect_make(5, -5, 10, 10));
    TC_CHECK_INT(r.x, 5);
    TC_CHECK_INT(r.y, 0);
    TC_CHECK_INT(r.w, 5);
    TC_CHECK_INT(r.h, 5);
    TC_CHECK(tc_rect_is_empty(tc_rect_intersect(tc_rect_make(0, 0, 1, 1), tc_rect_make(2, 2, 1, 1))));
    r = tc_rect_union(tc_rect_make(0, 0, 10, 10), tc_rect_make(5, -5, 10, 10));
    TC_CHECK(r.x == 0 && r.y == -5 && r.w == 15 && r.h == 15);
    r = tc_rect_union(tc_rect_make(100, 100, 0, 5), tc_rect_make(1, 2, 3, 4)); /* empty: ignored */
    TC_CHECK(r.x == 1 && r.y == 2 && r.w == 3 && r.h == 4);
    TC_CHECK(tc_rect_is_empty(tc_rect_union(tc_rect_make(1, 1, 0, 0), tc_rect_make(5, 5, 2, 0))));
}

void tc_suite_surface(void)
{
    TC_RUN(test_new_surface_is_black);
    TC_RUN(test_fill_is_clipped);
    TC_RUN(test_blend_half_alpha);
    TC_RUN(test_transparent_fill_does_nothing);
    TC_RUN(test_gradient_endpoints);
    TC_RUN(test_wrapped_surface_uses_stride);
    TC_RUN(test_rect_intersect);
}
