/**
 * Host unit tests for App::GlanceListSignal -- what turns glance apps' reports
 * into lists for the Glances screen.
 *
 * Two things can go wrong, and they pull in opposite directions. Lists can
 * pile up: the GUI takes one backend event per frame, so every extra list in
 * its queue is a frame of stale data and a slot the user's input needs. Or a
 * report can be lost: an app sends its first real value, nothing asks for a
 * list, and the screen shows the placeholder until something else happens to
 * change. The single-threaded tests walk each step of the hand-off; the last
 * test runs it on real threads and checks both at once.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "App/GlanceListSignal.hpp"

namespace {

using App::GlanceListSignal;

TEST(GlanceListSignal, FirstReportAsksForAList)
{
    GlanceListSignal s;
    EXPECT_TRUE(s.markDirty());
    EXPECT_TRUE(s.inFlight());
}

// Several apps reporting before the list is built: one list, not several.
TEST(GlanceListSignal, ReportsWhileInFlightAreCoalesced)
{
    GlanceListSignal s;
    ASSERT_TRUE(s.markDirty());
    EXPECT_FALSE(s.markDirty());
    EXPECT_FALSE(s.markDirty());
    s.beginBuild();
    EXPECT_FALSE(s.markDirty()) << "the list is still waiting for the GUI";
}

// Reported after the list was built: the GUI taking it asks for the next.
TEST(GlanceListSignal, ReportAfterTheBuildIsDeliveredNext)
{
    GlanceListSignal s;
    ASSERT_TRUE(s.markDirty());
    s.beginBuild();
    EXPECT_FALSE(s.markDirty());
    EXPECT_TRUE(s.delivered());
    EXPECT_TRUE(s.inFlight());
    s.beginBuild();
    EXPECT_FALSE(s.delivered()) << "nothing new since the second build";
    EXPECT_FALSE(s.inFlight());
}

// Reported before the build read the data: that list already holds it.
TEST(GlanceListSignal, ReportBeforeTheBuildNeedsNoSecondList)
{
    GlanceListSignal s;
    ASSERT_TRUE(s.markDirty());
    EXPECT_FALSE(s.markDirty());
    s.beginBuild();
    EXPECT_FALSE(s.delivered());
}

TEST(GlanceListSignal, ReportAfterDeliveryAsksAgain)
{
    GlanceListSignal s;
    ASSERT_TRUE(s.markDirty());
    s.beginBuild();
    ASSERT_FALSE(s.delivered());
    EXPECT_TRUE(s.markDirty());
}

// A post dropped on a full queue is not the end of it: the state stays
// dirty, and the next report asks again.
TEST(GlanceListSignal, FailedPostIsRecoveredByTheNextReport)
{
    GlanceListSignal s;
    ASSERT_TRUE(s.markDirty());
    s.postFailed();
    EXPECT_FALSE(s.inFlight());
    EXPECT_TRUE(s.dirty());
    EXPECT_TRUE(s.markDirty());
}

TEST(GlanceListSignal, ResetForgetsEverything)
{
    GlanceListSignal s;
    ASSERT_TRUE(s.markDirty());
    s.reset();
    EXPECT_FALSE(s.inFlight());
    EXPECT_FALSE(s.dirty());
    EXPECT_TRUE(s.markDirty());
}

// The hand-off on real threads: reporters standing in for glance apps, one
// Backend thread building lists, one GUI thread taking them. Checks that at
// most one list is ever in flight, and that once the reporters stop, the
// last list the GUI took holds the last report.
TEST(GlanceListSignal, ThreadedNeverPilesUpAndNeverLosesTheLastReport)
{
    constexpr int kReporters = 4;
    constexpr int kReportsEach = 2000;

    GlanceListSignal signal;
    std::atomic<uint32_t> version {0};      // the glance data, as a number
    std::atomic<int> inFlight {0};          // posted or queued for the GUI
    std::atomic<int> maxInFlight {0};
    std::atomic<uint32_t> lastSeen {0};
    std::atomic<bool> stop {false};

    std::mutex m;
    std::condition_variable cv;
    std::deque<int> backendQueue;           // posts asking for a build
    std::deque<uint32_t> guiQueue;          // built lists

    auto post = [&] {
        const int now = ++inFlight;
        int seen = maxInFlight.load();
        while (now > seen && !maxInFlight.compare_exchange_weak(seen, now)) {}
        std::lock_guard<std::mutex> lk(m);
        backendQueue.push_back(0);
        cv.notify_all();
    };

    std::thread backend([&] {
        std::unique_lock<std::mutex> lk(m);
        while (!stop) {
            if (backendQueue.empty()) {
                cv.wait_for(lk, std::chrono::milliseconds(1));
                continue;
            }
            backendQueue.pop_front();
            signal.beginBuild();
            guiQueue.push_back(version.load());
            cv.notify_all();
        }
    });

    std::thread gui([&] {
        for (;;) {
            uint32_t list;
            {
                std::unique_lock<std::mutex> lk(m);
                if (guiQueue.empty()) {
                    if (stop) {
                        return;
                    }
                    cv.wait_for(lk, std::chrono::milliseconds(1));
                    continue;
                }
                list = guiQueue.front();
                guiQueue.pop_front();
            }
            --inFlight;
            lastSeen = list;
            if (signal.delivered()) {
                post();
            }
        }
    });

    std::vector<std::thread> reporters;
    for (int r = 0; r < kReporters; ++r) {
        reporters.emplace_back([&] {
            for (int i = 0; i < kReportsEach; ++i) {
                ++version;
                if (signal.markDirty()) {
                    post();
                }
            }
        });
    }
    for (auto &t : reporters) {
        t.join();
    }

    // Let the hand-off settle: nothing posted, queued, or in flight.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        bool idle;
        {
            std::lock_guard<std::mutex> lk(m);
            idle = backendQueue.empty() && guiQueue.empty() && !signal.inFlight();
        }
        if (idle || std::chrono::steady_clock::now() > deadline) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    stop = true;
    cv.notify_all();
    backend.join();
    gui.join();

    EXPECT_LE(maxInFlight.load(), 1);
    EXPECT_EQ(inFlight.load(), 0);
    EXPECT_FALSE(signal.inFlight());
    EXPECT_EQ(lastSeen.load(), static_cast<uint32_t>(kReporters * kReportsEach))
        << "the last report never reached the GUI";
}

} // namespace
