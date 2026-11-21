/**
 * Host unit tests for the clock arithmetic behind a starting glance's extra
 * ticks (App::GlanceTiming).
 *
 * A glance is ticked once as it starts and answers with its placeholder, so
 * its first real value waits for the tick after that. For its first second it
 * is ticked every 100 ms instead of once a second, which puts that value on
 * screen a frame after it arrives. Whether a glance is still in that second is
 * one subtraction of two kernel clock readings, and the tests pin down the
 * edges of it: the window's end, a reading that straddles the clock's wrap,
 * and that the period really is no longer than one GUI frame, which is the
 * whole of the reason for it.
 */

#include <cstdint>

#include <gtest/gtest.h>

#include "App/GlanceTiming.hpp"
#include "AppTypes.hpp"

namespace {

using App::GlanceTiming::ElapsedMs;
using App::GlanceTiming::InSettleWindow;
using App::GlanceTiming::kSettlePeriodMs;
using App::GlanceTiming::kSettleWindowMs;

TEST(GlanceSettle, ElapsedIsTheDifference)
{
    EXPECT_EQ(ElapsedMs(1000, 1000), 0u);
    EXPECT_EQ(ElapsedMs(1000, 1250), 250u);
}

// A glance that starts just before the 32-bit wrap is still a few hundred
// milliseconds old just after it, not four billion.
TEST(GlanceSettle, ElapsedSurvivesTheWrap)
{
    EXPECT_EQ(ElapsedMs(0xFFFFFF00u, 0x00000010u), 0x110u);
    EXPECT_TRUE(InSettleWindow(0xFFFFFF00u, 0x00000010u));
}

TEST(GlanceSettle, SettlesForTheWholeWindowAndNoLonger)
{
    const uint32_t start = 50000;
    EXPECT_TRUE(InSettleWindow(start, start));
    EXPECT_TRUE(InSettleWindow(start, start + kSettleWindowMs - 1));
    EXPECT_FALSE(InSettleWindow(start, start + kSettleWindowMs));
    EXPECT_FALSE(InSettleWindow(start, start + 10 * kSettleWindowMs));
}

// A timer that fires every period, whatever its phase against the glance's
// start, lands the same number of ticks inside the window: the window is a
// whole number of periods.
TEST(GlanceSettle, EveryTimerPhaseGivesTheSameNumberOfTicks)
{
    ASSERT_EQ(kSettleWindowMs % kSettlePeriodMs, 0u);
    const uint32_t start = 7000;
    for (uint32_t phase = 0; phase < kSettlePeriodMs; ++phase) {
        uint32_t ticks = 0;
        for (uint32_t t = start + phase; t < start + 2 * kSettleWindowMs; t += kSettlePeriodMs) {
            if (InSettleWindow(start, t)) {
                ++ticks;
            }
        }
        EXPECT_EQ(ticks, kSettleWindowMs / kSettlePeriodMs) << "phase " << phase;
    }
}

// The point of the period: a value that arrives just after a tick goes out
// on the next one before the GUI draws its next frame.
TEST(GlanceSettle, PeriodIsNoLongerThanAGuiFrame)
{
    EXPECT_LE(kSettlePeriodMs, 1000u / App::kFrameRate);
    EXPECT_GT(kSettleWindowMs, kSettlePeriodMs);
}

TEST(GlanceSettle, SinceOpenCountsFromTheLastOpening)
{
    App::GlanceTiming::gOpenedAtMs.store(123000);
    EXPECT_EQ(App::GlanceTiming::SinceOpenMs(123456), 456u);
    App::GlanceTiming::gOpenedAtMs.store(0);
}

} // namespace
