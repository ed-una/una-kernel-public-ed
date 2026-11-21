/**
 * Host unit tests for the kernel's own copies of glance data
 * (App::CopyGlanceImages, App::CachedGlance).
 *
 * A glance app's icons live in the app's memory, and every stock glance
 * exits when the screen closes. Glance data now outlives the app twice over,
 * in the cache of its last values and in the list the GUI keeps between
 * visits, so nothing in it may point into the app. These tests stand in for
 * the app with buffers they then overwrite or free, and check that what the
 * kernel kept still holds the original pixels; that an icon that does not
 * change is copied once, not on every update; and that an image the kernel
 * cannot copy is dropped rather than left pointing anywhere.
 */

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "App/GlanceCache.hpp"

namespace {

using App::CachedGlance;
using App::CopyGlanceImages;
using App::GlanceData;

GlanceControl_t Image(uint32_t id, uint16_t w, uint16_t h, const uint8_t *buff)
{
    GlanceControl_t c {};
    c.id = id;
    c.valid = true;
    c.visible = true;
    c.type = GLANCE_TYPE_IMAGE;
    c.payload.image.size = {w, h};
    c.payload.image.buff = buff;
    return c;
}

GlanceControl_t Text(uint32_t id, const char *s)
{
    GlanceControl_t c {};
    c.id = id;
    c.valid = true;
    c.visible = true;
    c.type = GLANCE_TYPE_TEXT;
    std::snprintf(c.payload.text.str, sizeof(c.payload.text.str), "%s", s);
    return c;
}

TEST(GlanceCache, ImagesPointAtTheKernelsCopyNotTheApp)
{
    std::vector<uint8_t> app(4 * 3, 0xAB);
    GlanceData d {};
    d.ctrls[d.count++] = Text(0, "Steps");
    d.ctrls[d.count++] = Image(1, 4, 3, app.data());

    CopyGlanceImages(d, nullptr);

    ASSERT_EQ(d.count, 2);
    ASSERT_TRUE(d.pixels);
    const uint8_t *copy = d.ctrls[1].payload.image.buff;
    EXPECT_NE(copy, app.data());

    // The app exits and its memory is reused: the copy is untouched.
    std::fill(app.begin(), app.end(), 0x00);
    app.clear();
    app.shrink_to_fit();
    for (int i = 0; i < 12; ++i) {
        EXPECT_EQ(copy[i], 0xAB);
    }
    EXPECT_STREQ(d.ctrls[0].payload.text.str, "Steps");
}

// The copies stay alive for as long as anything holds the data.
TEST(GlanceCache, CopiesLiveAsLongAsTheData)
{
    std::vector<uint8_t> app(2 * 2, 0x5C);
    GlanceData d {};
    d.ctrls[d.count++] = Image(7, 2, 2, app.data());
    CopyGlanceImages(d, nullptr);

    GlanceData kept = d;          // as the GUI keeps its last list
    d = GlanceData {};            // the kernel moves on
    EXPECT_EQ(kept.ctrls[0].payload.image.buff[3], 0x5C);
}

// An icon that does not change is copied on the first update only.
TEST(GlanceCache, UnchangedImagesShareTheLastCopy)
{
    std::vector<uint8_t> app(5 * 5, 0x11);
    GlanceData first {};
    first.ctrls[first.count++] = Image(1, 5, 5, app.data());
    CopyGlanceImages(first, nullptr);

    GlanceData second {};
    second.ctrls[second.count++] = Image(1, 5, 5, app.data());
    CopyGlanceImages(second, first.pixels);

    EXPECT_EQ(second.pixels, first.pixels);
    EXPECT_EQ(second.ctrls[0].payload.image.buff, first.ctrls[0].payload.image.buff);
}

// The battery glance swaps its icon as the level changes.
TEST(GlanceCache, ChangedImageGetsANewCopy)
{
    std::vector<uint8_t> full(3 * 3, 0xFF);
    std::vector<uint8_t> empty(3 * 3, 0x00);
    GlanceData first {};
    first.ctrls[first.count++] = Image(0, 3, 3, full.data());
    CopyGlanceImages(first, nullptr);

    GlanceData second {};
    second.ctrls[second.count++] = Image(0, 3, 3, empty.data());
    CopyGlanceImages(second, first.pixels);

    EXPECT_NE(second.pixels, first.pixels);
    EXPECT_EQ(second.ctrls[0].payload.image.buff[0], 0x00);
    EXPECT_EQ(first.ctrls[0].payload.image.buff[0], 0xFF) << "the old list must keep its pixels";
}

TEST(GlanceCache, ImagesThatCannotBeCopiedAreDropped)
{
    std::vector<uint8_t> big(App::kGlanceImageMaxBytes + 1, 0x22);
    GlanceData d {};
    d.ctrls[d.count++] = Image(0, 4, 4, nullptr);               // no pixels
    d.ctrls[d.count++] = Text(1, "kept");
    d.ctrls[d.count++] = Image(2, 0, 9, big.data());            // no area
    d.ctrls[d.count++] = Image(3, 241, 61, big.data());         // past the glance
    CopyGlanceImages(d, nullptr);

    ASSERT_EQ(d.count, 1);
    EXPECT_EQ(d.ctrls[0].id, 1u);
    EXPECT_FALSE(d.pixels) << "nothing to own";
}

TEST(GlanceCache, NoImagesNoPixels)
{
    GlanceData d {};
    d.ctrls[d.count++] = Text(0, "a");
    d.ctrls[d.count++] = Text(1, "b");
    CopyGlanceImages(d, nullptr);
    EXPECT_EQ(d.count, 2);
    EXPECT_FALSE(d.pixels);
}

// The cache keeps the controls in use, and gives them back with the app's
// name and PID, still pointing at pixels it keeps alive.
TEST(GlanceCache, CachedGlanceRoundTrip)
{
    std::vector<uint8_t> app(2 * 3, 0x3C);
    GlanceData d {};
    d.ctrls[d.count++] = Text(0, "1234");
    d.ctrls[d.count++] = Image(1, 2, 3, app.data());
    CopyGlanceImages(d, nullptr);

    CachedGlance cache;
    EXPECT_FALSE(cache.valid());
    cache.store(d);
    d = GlanceData {};
    app.clear();

    ASSERT_TRUE(cache.valid());
    const GlanceData back = cache.expand("Steps", 0x37704A30u);
    EXPECT_EQ(back.count, 2);
    EXPECT_STREQ(back.altname, "Steps");
    EXPECT_EQ(back.pid, 0x37704A30u);
    EXPECT_STREQ(back.ctrls[0].payload.text.str, "1234");
    EXPECT_EQ(back.ctrls[1].payload.image.buff[5], 0x3C);
}

TEST(GlanceCache, CachedNameIsBounded)
{
    CachedGlance cache;
    cache.store(GlanceData {});
    const GlanceData back = cache.expand("A name far longer than sixteen", 1);
    EXPECT_EQ(std::strlen(back.altname), sizeof(back.altname) - 1);
}

} // namespace
