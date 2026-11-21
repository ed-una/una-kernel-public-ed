/**
 * Host unit tests for App::SelectGlance -- what the Glances screen shows for
 * each glance app while the apps start.
 *
 * The rule exists to keep two promises at once. The list has its full length
 * from the first frame, so nothing is inserted into it as the apps report and
 * the wheel does not jump. And a real value, once shown, is not swapped for a
 * placeholder and back: an app's first update is usually "---", so over a
 * cached value it waits for the second. The tests pin down both, and the ways
 * an app drops out of the list: never asked, or gone before it sent anything.
 */

#include <cstdint>

#include <gtest/gtest.h>

#include "App/GlanceSelect.hpp"

namespace {

using App::GlanceInputs;
using App::GlanceSource;
using App::SelectGlance;
using App::kGlanceHoldFirstUpdateMs;

GlanceInputs Starting(bool cached)
{
    GlanceInputs in;
    in.requested = true;
    in.alive = true;
    in.hasCache = cached;
    return in;
}

GlanceInputs Reported(uint32_t updates, uint32_t msSinceStart, bool cached)
{
    GlanceInputs in = Starting(cached);
    in.glanceRunning = true;
    in.updates = updates;
    in.hasData = true;
    in.msSinceStart = msSinceStart;
    return in;
}

// The first visit after boot: the app's name, so its row is there from the
// first list and the list does not grow as the apps report.
TEST(GlanceSelect, StartingWithNothingCachedIsAPlaceholder)
{
    EXPECT_EQ(SelectGlance(Starting(false)), GlanceSource::kPlaceholder);
}

TEST(GlanceSelect, StartingWithACacheShowsTheCache)
{
    EXPECT_EQ(SelectGlance(Starting(true)), GlanceSource::kCached);

    // Running, but nothing sent yet on this visit.
    GlanceInputs in = Starting(true);
    in.glanceRunning = true;
    EXPECT_EQ(SelectGlance(in), GlanceSource::kCached);
}

TEST(GlanceSelect, FirstUpdateWithNothingCachedIsShownAtOnce)
{
    EXPECT_EQ(SelectGlance(Reported(1, 30, false)), GlanceSource::kLive);
}

// The first update is usually the placeholder: over a cached value it waits.
TEST(GlanceSelect, FirstUpdateIsHeldBehindTheCache)
{
    EXPECT_EQ(SelectGlance(Reported(1, 30, true)), GlanceSource::kCached);
    EXPECT_EQ(SelectGlance(Reported(1, kGlanceHoldFirstUpdateMs - 1, true)), GlanceSource::kCached);
}

TEST(GlanceSelect, SecondUpdateEndsTheHold)
{
    EXPECT_EQ(SelectGlance(Reported(2, 130, true)), GlanceSource::kLive);
}

// An app whose real value is slow (a heart rate still settling) shows what it
// has once the hold runs out, rather than the old value for ever.
TEST(GlanceSelect, HoldExpires)
{
    EXPECT_EQ(SelectGlance(Reported(1, kGlanceHoldFirstUpdateMs, true)), GlanceSource::kLive);
}

TEST(GlanceSelect, AnEmptyUpdateIsNotLive)
{
    GlanceInputs in = Reported(2, 200, false);
    in.hasData = false;
    EXPECT_EQ(SelectGlance(in), GlanceSource::kPlaceholder);
}

// Failed to start, or exited before sending: it has nothing to show, and a
// row for it would never fill.
TEST(GlanceSelect, GoneBeforeReportingIsLeftOut)
{
    GlanceInputs in = Starting(true);
    in.alive = false;
    EXPECT_EQ(SelectGlance(in), GlanceSource::kOmit);
    in.hasCache = false;
    EXPECT_EQ(SelectGlance(in), GlanceSource::kOmit);
}

TEST(GlanceSelect, NotAskedOnThisVisitIsLeftOut)
{
    GlanceInputs in = Reported(3, 900, true);
    in.requested = false;
    EXPECT_EQ(SelectGlance(in), GlanceSource::kOmit);
}

} // namespace
