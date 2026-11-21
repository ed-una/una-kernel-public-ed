/**
 ******************************************************************************
 * @file    GlanceTimingLog.hpp
 * @brief   The "GlanceTiming" lines that time the Glances screen
 ******************************************************************************
 */

#ifndef APP_GLANCE_TIMING_LOG_HPP
#define APP_GLANCE_TIMING_LOG_HPP

#include "Logger_fwd.h"
#include "App/GlanceTiming.hpp"

/**
 * One module name for every line that times the Glances screen, so a single
 * search for "GlanceTiming" pulls the whole visit out of a log: the screen
 * opening, each glance app starting and reporting, and each list sent to the
 * GUI. The GUI's own lines carry the same word.
 *
 * LOGM rather than LOG_DEBUG, because the files these lines live in log at
 * INFO, which compiles their LOG_DEBUG out. LOGM is gated on the global level
 * alone, so the lines are in every build that logs at DEBUG (the one CI
 * publishes) without bringing the rest of those files' debug output with them.
 *
 * Kept to one line per event: the log's UART mirror transmits while the
 * caller waits, so every line adds a little to the very delays it measures.
 */
#define GLANCE_TIMING_LOG(FMT, ...) \
    LOGM("GlanceTiming", LOG_LEVEL_DEBUG, FMT, ##__VA_ARGS__)

#endif // APP_GLANCE_TIMING_LOG_HPP
