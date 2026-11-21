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

} // namespace GlanceTiming
} // namespace App

#endif // APP_GLANCE_TIMING_HPP
