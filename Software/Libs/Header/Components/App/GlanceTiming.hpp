/**
 ******************************************************************************
 * @file    GlanceTiming.hpp
 * @brief   Clock readings shared by the code that times the Glances screen
 ******************************************************************************
 */

#ifndef APP_GLANCE_TIMING_HPP
#define APP_GLANCE_TIMING_HPP

#include <atomic>
#include <cstdint>

namespace App {
namespace GlanceTiming {

/**
 * @brief   Milliseconds from one clock reading to a later one.
 * @details Unsigned subtraction, so it stays right across a 32-bit wrap. The
 *          kernel's millisecond clock wraps at a slightly different point (its
 *          tick runs at 1024 Hz, see Application::waitUntilIdle()), so one
 *          interval that straddles that wrap reads wrong, once in about 49
 *          days. Everything timed with this is a second or less, where that
 *          costs at most one misjudged step.
 */
inline uint32_t ElapsedMs(uint32_t fromMs, uint32_t toMs)
{
    return toMs - fromMs;
}

/**
 * @brief   When the Glances screen was last opened, in kernel milliseconds.
 * @details Lets every timing line say how far into the visit it is, which is
 *          what the lines are read for. Set on the Backend thread when the GUI
 *          starts the glances, read on the apps' own threads, hence atomic.
 *          Relaxed is enough: it orders nothing, it is only printed.
 */
inline std::atomic<uint32_t> gOpenedAtMs {0};

/// Milliseconds since the Glances screen was last opened.
inline uint32_t SinceOpenMs(uint32_t nowMs)
{
    return ElapsedMs(gOpenedAtMs.load(std::memory_order_relaxed), nowMs);
}

/**
 * How often a glance is ticked while it settles, and for how long after it
 * starts.
 *
 * A glance is ticked once the moment it starts, and every stock glance app
 * answers that tick with its placeholder: its sensor value is still queued
 * behind the tick. At the steady 1 Hz the value then waits up to a second for
 * the next one. Ticked every 100 ms (one GUI frame) for its first second, it
 * sends the value a frame after the value arrives. Apps send only what has
 * changed, so a tick with nothing new costs one message and no more, and none
 * of the stock apps reads a tick's number or time. The SDK promises glance
 * ticks are periodic, not that they come once a second.
 */
constexpr uint32_t kSettlePeriodMs = 100;
constexpr uint32_t kSettleWindowMs = 1000;

/// Whether a glance that started at @p startMs is still settling at @p nowMs.
inline bool InSettleWindow(uint32_t startMs, uint32_t nowMs,
                           uint32_t windowMs = kSettleWindowMs)
{
    return ElapsedMs(startMs, nowMs) < windowMs;
}

} // namespace GlanceTiming
} // namespace App

#endif // APP_GLANCE_TIMING_HPP
