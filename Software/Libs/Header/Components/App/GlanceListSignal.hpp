/**
 ******************************************************************************
 * @file    GlanceListSignal.hpp
 * @brief   Decides when a new glance list is built for the GUI
 ******************************************************************************
 */

#ifndef APP_GLANCE_LIST_SIGNAL_HPP
#define APP_GLANCE_LIST_SIGNAL_HPP

#include <atomic>

namespace App {

/**
 * @brief   Turns glance apps' reports into lists for the GUI, at most one
 *          waiting at a time.
 *
 * @details
 * Every glance app that sends new data says so, from its own thread, and the
 * Glances screen should show it within a frame. Building a list per report is
 * the wrong answer: the GUI takes one backend event per frame, so a burst of
 * lists is drawn one frame at a time, oldest first, while the newest waits
 * behind them, and every one of them holds a queue slot the user's input
 * needs. What the screen wants is the newest state, once per frame at most.
 *
 * So one list is in flight at a time: posted for building, or built and
 * waiting in the GUI's queue. Reports that come while one is in flight only
 * mark the state dirty. When the GUI takes the list, a dirty state asks for
 * the next one, built then, so it holds everything reported in between. No
 * report is lost: one either finds nothing in flight and asks for a list
 * itself, or is seen by the delivery of the list in flight.
 *
 * Who calls what:
 *  - markDirty():  whoever has news (an app thread, or the Backend for the
 *                  GUI's own request). True means "ask for a list now".
 *  - beginBuild(): the Backend thread, just before it reads the glance data.
 *  - delivered():  the GUI thread, as it takes a list from its queue. True
 *                  means "ask for the next list now".
 *  - postFailed(): whoever was told to ask and could not (a full queue). The
 *                  state stays dirty, so the next report or delivery asks.
 *  - reset():      when the screen opens or closes.
 *
 * Sequentially consistent throughout: the guarantee that no report is lost
 * rests on a report's store to dirty and a delivery's store to busy being
 * seen in one order by both sides.
 */
class GlanceListSignal {
public:
    bool markDirty()
    {
        mDirty.store(true);
        return !mBusy.exchange(true);
    }

    void beginBuild()
    {
        mDirty.store(false);
    }

    bool delivered()
    {
        mBusy.store(false);
        return mDirty.load() && !mBusy.exchange(true);
    }

    void postFailed()
    {
        mBusy.store(false);
    }

    void reset()
    {
        mDirty.store(false);
        mBusy.store(false);
    }

    /// A list is posted for building or waiting in the GUI's queue.
    bool inFlight() const { return mBusy.load(); }

    /// Something was reported that no list built since has read.
    bool dirty() const { return mDirty.load(); }

private:
    std::atomic<bool> mDirty {false};
    std::atomic<bool> mBusy {false};
};

} // namespace App

#endif // APP_GLANCE_LIST_SIGNAL_HPP
