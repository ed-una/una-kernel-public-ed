/**
 ******************************************************************************
 * @file    GlanceSelect.hpp
 * @brief   What the Glances screen shows for each glance app, and when
 ******************************************************************************
 */

#ifndef APP_GLANCE_SELECT_HPP
#define APP_GLANCE_SELECT_HPP

#include <cstdint>

namespace App {

/// Where a glance's entry in a list comes from.
enum class GlanceSource {
    kLive,          ///< what the running app last sent
    kCached,        ///< what it showed on the last visit
    kPlaceholder,   ///< its name only: starting, and nothing to show yet
    kOmit,          ///< not in the list at all
};

/// An app's first update is usually its placeholder ("---"), its second the
/// first real value, about 100 ms later. Over a cached value the first one is
/// held back until the second arrives or this long after the glance started,
/// so a real number is not swapped for "---" and back again. A first update
/// that is already real waits no longer than that.
constexpr uint32_t kGlanceHoldFirstUpdateMs = 500;

struct GlanceInputs {
    bool requested = false;        ///< asked to start its glance on this visit
    bool alive = false;            ///< instance there, service running or a request pending
    bool glanceRunning = false;    ///< in glance mode now
    uint32_t updates = 0;          ///< updates sent since its glance started
    bool hasData = false;          ///< what it sent has at least one control
    uint32_t msSinceStart = 0;     ///< since its glance started
    bool hasCache = false;         ///< values kept from an earlier visit
};

/**
 * @brief   Decide what one glance app contributes to a list.
 * @details
 *  - Live once it has sent something, except that its first update waits up
 *    to kGlanceHoldFirstUpdateMs behind a cached value.
 *  - While it is still starting: its cached values, or its name alone on the
 *    first visit after boot, so the list has its full length from the start
 *    and nothing is inserted into it later.
 *  - Left out once it is plainly not going to show anything: it was not asked
 *    on this visit, or it stopped (failed to start, exited) before sending.
 */
inline GlanceSource SelectGlance(const GlanceInputs &in)
{
    if (!in.requested) {
        return GlanceSource::kOmit;
    }

    if (in.glanceRunning && in.updates > 0 && in.hasData) {
        if (in.hasCache && in.updates == 1 && in.msSinceStart < kGlanceHoldFirstUpdateMs) {
            return GlanceSource::kCached;
        }
        return GlanceSource::kLive;
    }

    if (in.alive) {
        return in.hasCache ? GlanceSource::kCached : GlanceSource::kPlaceholder;
    }
    return GlanceSource::kOmit;
}

} // namespace App

#endif // APP_GLANCE_SELECT_HPP
