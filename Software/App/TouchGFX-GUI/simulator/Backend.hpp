
#include <queue>
#include <algorithm>
#include <chrono>
#include <ctime>

#include "touchgfx/UIEventListener.hpp"

#include "../../../../Libs/Header/Components/Shared/GuiBackendInterface.hpp"
#include "../../../../Libs/Header/Components/App/IconPool.hpp"
#include "../../../../Libs/Header/Components/SleepMode/SleepSchedule.hpp"
#include "../../../../Libs/Header/Components/App/GlanceTiming.hpp"
#include "../../../../Libs/Header/Components/App/GlanceListSignal.hpp"
#include "../../../../Libs/Header/Components/App/GlanceCache.hpp"
#include "../../../../Libs/Header/Components/App/GlanceSelect.hpp"


#include "SDK/Glance/GlanceControl.hpp"

#ifndef BACKEND_HPP
#define BACKEND_HPP


class Backend : virtual public IGuiBackend, public touchgfx::UIEventListener {

public:

    static Backend &GetInstance()
    {
        static Backend sInstance;
        return sInstance;
    }

    // Tick
    virtual void handleTickEvent() override
    {
        // Everything this stub produced last tick becomes visible to the GUI
        // now, and not before. See sendToGui().
        while (!mPendingTx.empty()) {
            mGuiTxQueue.push(mPendingTx.front());
            mPendingTx.pop();
        }

        updTime();
        updGps();
        updMag();
        stepGlances();
        releaseBackendHold();

        // While the Backend is held, what the GUI sent waits for it, as on
        // the watch, where the Backend thread is still in startGlances().
        while (mBackendHeldTicks == 0 && !mGuiRxQueue.empty()) {
            auto data = mGuiRxQueue.front();
            mGuiRxQueue.pop();

            std::visit([this](auto &&value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    break;
                } else if constexpr (std::is_same_v<T, G2BEvent::SettingsLocal>) {
                    // Kept, so the sleep answer below does not hand the GUI
                    // back settings older than the ones it just saved.
                    mLocalSettings = SleepMode::mergeFromGui(mLocalSettings, value.settings);
                } else if constexpr (std::is_same_v<T, G2BEvent::SleepModeSet>) {
                    LOG_DEBUG("Backend. Sleep mode %s\n", value.on ? "on" : "off");
                    SleepMode::set(mLocalSettings, value.on);
                    sendToGui(B2GEvent::SettingsLocal {mLocalSettings});
                } else  if constexpr (std::is_same_v<T, G2BEvent::BleStartPairing>) {
                    LOG_DEBUG("Backend. Start pairing\n");
                    mBlePairingWait = true;
                } else  if constexpr (std::is_same_v<T, G2BEvent::BleAbortPairing>) {
                    LOG_DEBUG("Backend. Abort pairing\n");
                    mBlePairingWait = false;
                    mBleCodeWait = false;
                } else if constexpr (std::is_same_v<T, G2BEvent::MagFeedStart>) {
                    mMagFeed     = true;
                    mMagFeedTick = 0;
                } else if constexpr (std::is_same_v<T, G2BEvent::MagFeedStop>) {
                    // Ends a run too; nobody listens for its MagCalDone.
                    mMagFeed     = false;
                    mMagRunTicks = 0;
                } else if constexpr (std::is_same_v<T, G2BEvent::MagCalStart>) {
                    if ((mMagRunTicks > 0) || (value.seconds == 0U)) {
                        // Refused, and answered so the screen does not wait.
                        sendToGui(B2GEvent::MagCalDone {false, false});
                    } else {
                        mMagRunTicks = value.seconds * 10U;   // 10 Hz tick
                        mMagRunLeft  = mMagRunTicks;
                        mMagRunTick  = 0;

                        sendToGui(B2GEvent::MagCalProgress {0});
                    }
                } else if constexpr (std::is_same_v<T, G2BEvent::MagCalCancel>) {
                    // Always answered, as on the watch.
                    mMagRunTicks = 0;

                    sendToGui(B2GEvent::MagCalDone {false, false});
                } else  if constexpr (std::is_same_v<T, G2BEvent::BleUnpair>) {
                    mBlePairingWait = false;
                    mBleCodeWait = false;
                    mBleBonded = false;
                } else if constexpr (std::is_same_v<T, G2BEvent::PhoneNotificationNegativeAction>) { 
                    sendToGui(B2GEvent::PhoneNotificationRemove {value.notification->getUid(), false});
                    if (value.notification->getCategory() == PhoneNotification::Category::INCOMING_CALL) {
                        std::tm timeNow = getSystemTime();
                        auto notification = std::make_shared<PhoneNotification>(
                            getTimeMs(),
                            PhoneNotification::Platform::IOS,
                            static_cast<uint32_t>(timeNowUTC()),
                            PhoneNotification::Category::MISSED_CALL, timeNow,
                            "Joe Doe",
                            "",
                            "Missed call",
                            "com.google.Gmail", "Dial", "Clear");

                        sendToGui(B2GEvent::PhoneNotificationAdd {notification, true});
                    }
                } else if constexpr (std::is_same_v<T, G2BEvent::PhoneNotificationPositiveAction>) {
                    if (value.notification->getCategory() == PhoneNotification::Category::INCOMING_CALL) {
                        std::tm timeNow = getSystemTime();
                        auto notification = std::make_shared<PhoneNotification>(
                            getTimeMs(),
                            PhoneNotification::Platform::IOS, static_cast<uint32_t>(timeNowUTC()),
                            PhoneNotification::Category::ACTIVE_CALL, timeNow,
                            "Joe Doe",
                            "",
                            "Active call",
                            "com.google.Gmail", "", "End Call");

                        sendToGui(B2GEvent::PhoneNotificationAdd {notification, true});

                        sendToGui(B2GEvent::PhoneNotificationRemove {value.notification->getUid(), false});
                    }
                } else if constexpr (std::is_same_v<T, G2BEvent::UserAppListReq>) {
                    B2GEvent::UserAppList rsp{};
                    rsp.type = value.type;
                    if (value.type == App::Type::kActivity) {
                        rsp.list = std::make_shared<std::vector<App::Item>>(mApps);
                    } else if (value.type == App::Type::kUtility) {
                        rsp.list = std::make_shared<std::vector<App::Item>>(mApps);
                    } else if (value.type == App::Type::kClockface) {
                        rsp.list = std::make_shared<std::vector<App::Item>>(mApps);
                    } else if (value.type == App::Type::kGlance) {
                        rsp.list = nullptr;
                    }
                    sendToGui(rsp);
                } else if constexpr (std::is_same_v<T, G2BEvent::UserAppIconWindow>) {
                    // Mirrors Backend::guiHandler: hold exactly the window
                    // asked for, reuse what is already in it, and let the rest
                    // go. Taking a fresh slot for every id on every request --
                    // which is the obvious thing to write here -- burns through
                    // the icon pool at a rate the watch never would, and makes
                    // the sim disagree with the firmware about when it runs out.
                    B2GEvent::UserAppIcons rsp{};
                    rsp.type  = value.type;
                    rsp.count = 0;

                    App::IconRef kept[App::kIconWindowMax];
                    uint8_t keptCount = 0;

                    for (uint8_t i = 0; i < value.count; ++i) {
                        const uint64_t appId = value.appIds[i];

                        App::IconRef found;
                        for (uint8_t h = 0; h < mIconWindowCount; ++h) {
                            if (mIconWindow[h] && mIconWindow[h]->appId == appId) {
                                found = mIconWindow[h];
                                break;
                            }
                        }

                        if (!found) {
                            for (const auto& set : mAppIcons) {
                                if (set.appId != appId) {
                                    continue;
                                }

                                found = App::IconPool::take();
                                if (found) {
                                    found.mutableSet() = set;
                                }
                                break;
                            }
                        }

                        if (!found) {
                            continue;
                        }

                        if (keptCount < App::kIconWindowMax) {
                            kept[keptCount++] = found;
                        }
                        if (rsp.count < B2GEvent::UserAppIcons::kMax) {
                            rsp.icons[rsp.count++] = found;
                        }
                    }

                    for (uint8_t i = 0; i < App::kIconWindowMax; ++i) {
                        mIconWindow[i] = (i < keptCount) ? kept[i] : App::IconRef();
                    }
                    mIconWindowCount = keptCount;

                    sendToGui(rsp);
                } else if constexpr (std::is_same_v<T, G2BEvent::UserAppStart>) {
                    LOG_DEBUG("Backend. UserAppStart [pid= 0x%08X]\n", value.pid);

                    // 'F' arms a reason: answer the way the watch does when a
                    // launch cannot start, so each sentence can be read on the
                    // screen that shows it. Without this the stub always
                    // succeeds, and the only failure the simulator could reach
                    // was the GUI's own timeout.
                    if (mForcedLaunchFailure != App::LaunchFailure::NONE) {
                        LOG_DEBUG("Backend. Forced launch failure %u\n",
                                static_cast<unsigned>(mForcedLaunchFailure));
                        sendToGui(B2GEvent::UserAppStartFailed{
                                value.pid, value.attempt, mForcedLaunchFailure });
                    } else if (auto item = findApp(value.pid)) {
                        sendToGui(B2GEvent::UserAppStartReq{ item->pidGUI,
                                                            value.attempt });

                        // Move it to the front, as App::SorterFreqUse does on
                        // the watch: it bumps the launched app's count and
                        // decays the rest, so a launch puts that app first in
                        // every list handed out afterwards. Without this the
                        // stub answered every request with the same order, so
                        // nothing exercised a menu holding its place across a
                        // re-sorted list -- and the tracking wheel restored the
                        // row rather than the app, landing one place off.
                        const auto pid = value.pid;
                        const auto it = std::find_if(mApps.begin(), mApps.end(),
                            [pid](const App::Item &a) { return a.pidGUI == pid; });
                        if (it != mApps.end() && it != mApps.begin()) {
                            std::rotate(mApps.begin(), it, it + 1);
                        }
                    }
                } else if constexpr (std::is_same_v<T, G2BEvent::UserAppResume>) {
                    LOG_DEBUG("Backend. UserAppResume [pid= 0x%08X]\n", value.pid);
                    // No confirmation, just resume app
                } else if constexpr (std::is_same_v<T, G2BEvent::UserAppPause>) {
                    LOG_DEBUG("Backend. UserAppPause [pid= 0x%08X]\n", value.pid);
                    // No confirmation, just pause app
                } else if constexpr (std::is_same_v<T, G2BEvent::UserAppStop>) {
                    LOG_DEBUG("Backend. UserAppStop [pid= 0x%08X]\n", value.pid);
                    // No confirmation, just stop app
                } else if constexpr (std::is_same_v<T, G2BEvent::UserAppGlancesStart>) {
                    LOG_DEBUG("Backend. UserAppGlancesStart\n");
                    glancesOpen();

                } else if constexpr (std::is_same_v<T, G2BEvent::UserAppGlancesStop>) {
                    LOG_DEBUG("Backend. UserAppGlancesStop\n");
                    glancesClose();

                } else if constexpr (std::is_same_v<T, G2BEvent::UserAppGlancesListReq>) {
                    LOG_DEBUG("Backend. UserAppGlancesListReq\n");
                    if (mGlanceSignal.markDirty()) {
                        sendGlanceList("request");
                    }
                }
            }, data);
        }

    }

    // Keys
    virtual void handleKeyEvent(uint8_t c) override
    {
        LOG_DEBUG("%c\n", c);

        // Glances: x makes glance_1's app exit, or start again, so the list
        // can be seen losing and regaining an entry while it is on screen.
        if ('x' == c && mGlancesOpen) {
            glanceToggleExit(1);
        }

        // Battery level
        if ('+' == c) {
            if (mBatteryLevel < 100) { 
                mBatteryLevel++;
                sendToGui(B2GEvent::BatteryLevel {static_cast<float>(mBatteryLevel)});
            }
        }
        if ('-' == c) {
            if (mBatteryLevel > 0) {
                mBatteryLevel--;
                sendToGui(B2GEvent::BatteryLevel {static_cast<float>(mBatteryLevel)});
            }
        }

        // BLE
        // p - pin code, s - success, f - fail
        if (!mBlePairingWait && !mBleCodeWait && 'p' == c) {
            sendToGui(B2GEvent::BlePinCode {123456});
            mBleCodeWait = true;
        } else if (mBlePairingWait) {
            mBlePairingWait = false;
            if ('s' == c) {
                sendToGui(B2GEvent::BleConnect {true});
                sendToGui(B2GEvent::BlePinCode {123456});
                mBleCodeWait = true;
            } else if ('f' == c) {
                sendToGui(B2GEvent::BleConnect {true});
                sendToGui(B2GEvent::BlePair {false});
                sendToGui(B2GEvent::BleConnect {false});
            }
        } else if (mBleCodeWait) {
            mBleCodeWait = false;
            if ('s' == c) {
                sendToGui(B2GEvent::BlePair {true});

                mBleBonded = true;
                sendToGui(B2GEvent::BleBond {mBleBonded});
            } else if ('f' == c) {
                sendToGui(B2GEvent::BlePair {false});
            }
        }


        // USB
        if ('U' == c) {
            mUsbConnected = !mUsbConnected;
            sendToGui(B2GEvent::UsbConnect {mUsbConnected});
        }
        
        if ('u' == c) {
            mUsbPortOpened = !mUsbPortOpened;
            sendToGui(B2GEvent::UsbPortOpen {mUsbPortOpened});
        }

        if ('N' == c) {
            std::tm timeNow = getSystemTime();
            auto notification = std::make_shared<PhoneNotification>(
                getTimeMs(),
                PhoneNotification::Platform::IOS,
                static_cast<uint32_t>(timeNowUTC()),
                PhoneNotification::Category::INCOMING_CALL, timeNow,
                "Joe Doe", 
                "",
                "Incoming call", 
                "com.google.Gmail", "Answer", "Decline");

            sendToGui(B2GEvent::PhoneNotificationAdd {notification, true});
        }

        if ('M' == c) {
            std::tm timeNow = getSystemTime();
            char timestr[20] {};
            sprintf_s(timestr, "%u", timeNow.tm_sec);

            auto notification = std::make_shared<PhoneNotification>(
                getTimeMs(),
                PhoneNotification::Platform::IOS,
                static_cast<uint32_t>(timeNowUTC()),
                PhoneNotification::Category::MESSAGE, timeNow,
                "Message",
                timestr,
                "Hey mate, how are you? We are meeting Marcus at the station pub at 10am tomorrow."
                "Will you join us ? \nWe can grab a bite as well! qwerty",
                "com.google.Gmail", "", "Clear");

            sendToGui(B2GEvent::PhoneNotificationAdd {notification, true});
        }

        if ('C' == c) {
            std::tm timeNow = getSystemTime();
            auto notification = std::make_shared<PhoneNotification>(
                getTimeMs(),
                PhoneNotification::Platform::IOS, static_cast<uint32_t>(timeNowUTC()),
                PhoneNotification::Category::ACTIVE_CALL, timeNow,
                "Joe Doe",
                "",
                "Active call",
                "com.google.Gmail", "", "End Call");

            sendToGui(B2GEvent::PhoneNotificationAdd {notification, true});
        }

        if ('K' == c) {
            sendToGui(B2GEvent::PhoneNotificationRemove {0, true});
        }

        if ('R' == c) {
            sendToGui(B2GEvent::UserAppStartReq{ mApps[0].pidGUI, 0 });
        }
        if ('r' == c) {
            sendToGui(B2GEvent::UserAppStartReq{ mApps[1].pidGUI, 0 });
        }
        if ('E' == c) {
            sendToGui(B2GEvent::UserAppStopped{ mApps[0].pidGUI });
        }
        if ('e' == c) {
            sendToGui(B2GEvent::UserAppStopped{ mApps[1].pidGUI });
        }

        // Charging refused by a protection rule - toggles the charging screen
        // out of the priority list while USB stays connected.
        if (';' == c) {
            mChargingBlocked = !mChargingBlocked;
            sendToGui(B2GEvent::ChargingBlocked {mChargingBlocked});
        }

        // Launch failure: each F press arms the next reason, so the next app
        // the user presses fails with it and the wording for every reason can
        // be read on the screen. Wraps back to NONE, which launches normally
        // again.
        if ('F' == c) {
            static constexpr App::LaunchFailure kReasons[] = {
                App::LaunchFailure::NONE,
                App::LaunchFailure::OUT_OF_MEMORY,
                App::LaunchFailure::BAD_IMAGE,
                App::LaunchFailure::TIMEOUT,
                App::LaunchFailure::NOT_FOUND,
                App::LaunchFailure::OTHER,
            };
            mForcedReason = (mForcedReason + 1)
                    % (sizeof(kReasons) / sizeof(kReasons[0]));
            mForcedLaunchFailure = kReasons[mForcedReason];
            LOG_DEBUG("Backend. Launch failure armed: %u\n",
                    static_cast<unsigned>(mForcedLaunchFailure));
        }

        // System error: each ' press raises the next SystemError code so the
        // error screen text can be checked for every code. Wraps at COUNT.
        if ('\'' == c) {
            mErrorCode = (mErrorCode + 1) % static_cast<uint32_t>(SystemError::COUNT);
            LOG_DEBUG("Backend. Error code %u\n", mErrorCode);
            sendToGui(B2GEvent::Error {static_cast<SystemError>(mErrorCode)});
        }

    }





    virtual bool receiveGuiEvent(B2GEvent::Data &data) override
    {
        if (!mGuiTxQueue.empty()) {
            data = mGuiTxQueue.front();
            mGuiTxQueue.pop();

            // As the kernel's Backend does: the GUI taking a glance list is
            // when the next one may be built, if anything changed since.
            const auto *glances = std::get_if<B2GEvent::UserAppGlanceList>(&data);
            if (glances && !glances->atRest && mGlanceSignal.delivered()) {
                sendGlanceList("push");
            }
            return true;
        }
        return false;
    }

    virtual bool sendEventToBackend(const G2BEvent::Data &data) override
    {
        mGuiRxQueue.emplace(data);
        return true;
    }

    uint32_t getTimeMs()
    { 
        return static_cast<uint32_t>(GetTickCount());
    }

private:
    // Glances
    //
    // A model of the kernel's side of the Glances screen, so the simulator
    // shows the screen's real timing rather than one of its own. On the
    // watch:
    //  - opening the screen asks every glance app to start. Each one's service
    //    is read off the eMMC first, one after another. skGlanceLoadTicks is a
    //    guess at how long each takes, until the watch's GlanceTiming lines
    //    give the real figure;
    //  - a started app is ticked once straight away and answers with its
    //    placeholder, because its sensor value is still queued behind that
    //    tick. The value arrives a tick later, and then once a second;
    //  - for its first second it is ticked every 100 ms, so it sends the value
    //    on the tick after it arrives (GlanceTiming::kSettlePeriodMs);
    //  - it is also ticked once a second, by one timer that all glance apps
    //    share and that started with the first of them;
    //  - it sends only what has changed since it last sent;
    //  - every app exits when the screen closes, taking what it sent with it;
    //  - every report is pushed to the GUI through App::GlanceListSignal, one
    //    list in flight at a time, and the GUI's own ask goes through it too;
    //  - what each app contributes to a list is App::SelectGlance()'s answer:
    //    live, its values from the last visit while it starts (the kernel
    //    keeps them, App::CachedGlance), or its name alone on the first visit.
    //    The kernel copies the icons when they are sent (CopyGlanceImages),
    //    as the watch does, so a list never points into an app.
    //  - asking them holds the Backend thread (startGlances()): 178 to 249 ms
    //    with 11 glances in the round 2 watch logs (0171, 0172). Whatever the
    //    GUI sent after the start, and every report pushed meanwhile, waits
    //    for it, so the visit's first list is drawn on frame 3;
    //  - at boot the kernel sends every installed glance at rest (by name,
    //    or with last visit's values), which the GUI keeps for the first
    //    frame of the first visit.
    // Ticks here are the GUI's, 100 ms each.
    static constexpr uint32_t skGlancesCount    = 3;
    static constexpr uint32_t skGlanceLoadTicks = 2;    // 200 ms per app, a guess
    static constexpr uint32_t skGlanceTickTicks = 10;   // the kernel's 1 Hz glance tick
    static constexpr uint32_t skStartHoldTicks  = 2;    // 200 ms, the watch's startGlances()
    static constexpr uint32_t skMsPerTick       = 1000 / App::kFrameRate;

    struct MockGlanceApp {
        enum class State { Off, Loading, Running };
        State    state         = State::Off;
        uint32_t readyTick     = 0;      // when its load finishes
        uint32_t startTick     = 0;      // when its glance started
        uint32_t nextValueTick = 0;      // when its sensor next reports
        bool     changed       = false;  // a value it has not sent yet
        uint32_t updates       = 0;      // updates sent since its glance started
    };

    bool          mGlancesOpen        = false;
    uint32_t      mGlanceTick         = 0;      // ticks since the screen opened
    bool          mGlanceTimerRunning = false;
    uint32_t      mGlanceTimerPhase   = 0;      // tick the shared 1 Hz timer started on
    uint32_t      mGlanceListsSent    = 0;
    MockGlanceApp mGlanceApp[skGlancesCount];
    App::GlanceListSignal mGlanceSignal;
    App::CachedGlance     mGlanceCache[skGlancesCount];   // outlives the visit
    uint32_t      mBackendHeldTicks   = 0;      // left of the startGlances() hold
    bool          mGlanceListDeferred = false;  // a list asked for during the hold

    void releaseBackendHold()
    {
        if (mBackendHeldTicks == 0 || --mBackendHeldTicks > 0) {
            return;
        }
        LOG_DEBUG("GlanceTiming: [mock kernel] open: %u glance apps asked to start in %u ms\n",
                  static_cast<unsigned>(skGlancesCount),
                  static_cast<unsigned>(skStartHoldTicks * skMsPerTick));
        if (mGlanceListDeferred) {
            mGlanceListDeferred = false;
            sendGlanceList("push");
        }
    }

    void glancesOpen()
    {
        mBackendHeldTicks   = skStartHoldTicks;
        mGlanceListDeferred = false;
        mGlancesOpen        = true;
        mGlanceTick         = 0;
        mGlanceTimerRunning = false;
        mGlanceListsSent    = 0;
        mGlanceSignal.reset();
        for (uint32_t i = 0; i < skGlancesCount; ++i) {
            mGlanceApp[i]           = MockGlanceApp{};
            mGlanceApp[i].state     = MockGlanceApp::State::Loading;
            mGlanceApp[i].readyTick = (i + 1) * skGlanceLoadTicks;
            mGlance[i].setPlaceholder();
        }
    }

    void glancesClose()
    {
        mGlancesOpen = false;
        mGlanceSignal.reset();
        for (uint32_t i = 0; i < skGlancesCount; ++i) {
            mGlanceApp[i] = MockGlanceApp{};
            mGlance[i].clearStored();
        }
        LOG_DEBUG("GlanceTiming: [mock kernel] close: %u lists sent, t+%u\n",
                  static_cast<unsigned>(mGlanceListsSent),
                  static_cast<unsigned>(mGlanceTick * skMsPerTick));
    }

    void glanceToggleExit(uint32_t i)
    {
        MockGlanceApp &a = mGlanceApp[i];
        if (a.state != MockGlanceApp::State::Off) {
            a = MockGlanceApp{};
            mGlance[i].clearStored();
            LOG_DEBUG("GlanceTiming: [mock kernel] [glance_%u] exited, t+%u\n",
                      static_cast<unsigned>(i), static_cast<unsigned>(mGlanceTick * skMsPerTick));
        } else {
            a.state     = MockGlanceApp::State::Loading;
            a.readyTick = mGlanceTick + skGlanceLoadTicks;
            LOG_DEBUG("GlanceTiming: [mock kernel] [glance_%u] starting again, t+%u\n",
                      static_cast<unsigned>(i), static_cast<unsigned>(mGlanceTick * skMsPerTick));
        }
        if (mGlanceSignal.markDirty()) {
            sendGlanceList("push");
        }
    }

    // The app's REQUEST_GLANCE_UPDATE: the kernel keeps a copy.
    void glanceSend(uint32_t i)
    {
        MockGlanceApp &a = mGlanceApp[i];
        mGlance[i].store();
        a.updates++;
        if (a.updates <= 2) {
            LOG_DEBUG("GlanceTiming: [mock kernel] [glance_%u] update #%u: %u ms after run, t+%u\n",
                      static_cast<unsigned>(i), static_cast<unsigned>(a.updates),
                      static_cast<unsigned>((mGlanceTick - a.startTick) * skMsPerTick),
                      static_cast<unsigned>(mGlanceTick * skMsPerTick));
        }

        // The kernel's Backend builds within milliseconds of the report; here
        // that is this tick, and the GUI sees it on the next, as it would.
        if (mGlanceSignal.markDirty()) {
            sendGlanceList("push");
        }
    }

    // EVENT_GLANCE_TICK, as the stock glance apps answer it: they send only
    // what changed, so the placeholder first, then each value once.
    void glanceTick(uint32_t i)
    {
        MockGlanceApp &a = mGlanceApp[i];
        if (a.updates == 0 || a.changed) {
            a.changed = false;
            glanceSend(i);
        }
    }

    void stepGlances()
    {
        if (!mGlancesOpen) {
            return;
        }
        mGlanceTick++;

        for (uint32_t i = 0; i < skGlancesCount; ++i) {
            MockGlanceApp &a = mGlanceApp[i];
            if (a.state == MockGlanceApp::State::Loading && mGlanceTick >= a.readyTick) {
                a.state         = MockGlanceApp::State::Running;
                a.startTick     = mGlanceTick;
                a.nextValueTick = mGlanceTick + 1;
                if (!mGlanceTimerRunning) {
                    mGlanceTimerRunning = true;
                    mGlanceTimerPhase   = mGlanceTick;
                }
                LOG_DEBUG("GlanceTiming: [mock kernel] [glance_%u] run ok: load %u ms, t+%u\n",
                          static_cast<unsigned>(i),
                          static_cast<unsigned>(skGlanceLoadTicks * skMsPerTick),
                          static_cast<unsigned>(mGlanceTick * skMsPerTick));
                glanceTick(i);    // the kernel's first tick, sent straight away
            } else if (a.state == MockGlanceApp::State::Running) {
                if (mGlanceTick >= a.nextValueTick) {
                    mGlance[i].glanceUpdate();
                    a.changed        = true;
                    a.nextValueTick += skGlanceTickTicks;
                }
                if (App::GlanceTiming::InSettleWindow(a.startTick * skMsPerTick,
                                                      mGlanceTick * skMsPerTick)) {
                    glanceTick(i);    // a settle tick
                }
            }
        }

        if (mGlanceTimerRunning && mGlanceTick != mGlanceTimerPhase &&
                (mGlanceTick - mGlanceTimerPhase) % skGlanceTickTicks == 0) {
            for (uint32_t i = 0; i < skGlancesCount; ++i) {
                if (mGlanceApp[i].state == MockGlanceApp::State::Running) {
                    glanceTick(i);
                }
            }
        }
    }

    // Backend::sendGlancesAtRest(): every glance, kept by the GUI for the first
    // frame of the next visit. Outside the signal, as on the watch.
    void sendGlancesAtRest(const char *reason)
    {
        std::vector<App::GlanceData> v;
        unsigned cached = 0, placeholder = 0;
        for (uint32_t i = 0; i < skGlancesCount; ++i) {
            if (App::SelectGlanceAtRest(mGlanceCache[i].valid()) == App::GlanceSource::kCached) {
                v.push_back(mGlanceCache[i].expand(mGlance[i].name(), mGlance[i].pid()));
                cached++;
            } else {
                App::GlanceData d{};
                snprintf(d.altname, sizeof(d.altname), "%s", mGlance[i].name());
                d.pid = mGlance[i].pid();
                v.push_back(d);
                placeholder++;
            }
        }
        B2GEvent::UserAppGlanceList rsp{};
        rsp.atRest = true;
        rsp.list = std::make_shared<std::vector<App::GlanceData>>(v);
        sendToGui(rsp);
        LOG_DEBUG("GlanceTiming: [mock kernel] list at rest (%s): %u items (cached %u, placeholder %u)\n",
                  reason, static_cast<unsigned>(v.size()), cached, placeholder);
    }

    void sendGlanceList(const char *reason)
    {
        // A report pushed while the Backend is still asking the apps to start
        // is queued behind it on the watch, and built once it is free.
        if (mBackendHeldTicks > 0) {
            mGlanceListDeferred = true;
            return;
        }
        mGlanceSignal.beginBuild();
        std::vector<App::GlanceData> v;
        v.reserve(skGlancesCount);
        unsigned live = 0, cached = 0, placeholder = 0;
        for (uint32_t i = 0; i < skGlancesCount; ++i) {
            const MockGlanceApp &a = mGlanceApp[i];
            const App::GlanceData &stored = mGlance[i].stored();

            App::GlanceInputs in;
            in.requested     = mGlancesOpen;
            in.alive         = a.state != MockGlanceApp::State::Off;
            in.glanceRunning = a.state == MockGlanceApp::State::Running;
            in.updates       = a.updates;
            in.hasData       = stored.count > 0;
            in.msSinceStart  = (mGlanceTick - a.startTick) * skMsPerTick;
            in.hasCache      = mGlanceCache[i].valid();

            switch (App::SelectGlance(in)) {
                case App::GlanceSource::kLive:
                    mGlanceCache[i].store(stored);
                    v.push_back(stored);
                    live++;
                    break;
                case App::GlanceSource::kCached:
                    v.push_back(mGlanceCache[i].expand(mGlance[i].name(), mGlance[i].pid()));
                    cached++;
                    break;
                case App::GlanceSource::kPlaceholder: {
                    App::GlanceData d{};
                    snprintf(d.altname, sizeof(d.altname), "%s", mGlance[i].name());
                    d.pid = mGlance[i].pid();
                    v.push_back(d);
                    placeholder++;
                } break;
                case App::GlanceSource::kOmit:
                    break;
            }
        }

        B2GEvent::UserAppGlanceList rsp{};
        rsp.list = std::make_shared<std::vector<App::GlanceData>>(v);
        sendToGui(rsp);

        LOG_DEBUG("GlanceTiming: [mock kernel] list #%u (%s): %u items "
                  "(live %u, cached %u, placeholder %u), t+%u\n",
                  static_cast<unsigned>(++mGlanceListsSent), reason,
                  static_cast<unsigned>(v.size()), live, cached, placeholder,
                  static_cast<unsigned>(mGlanceTick * skMsPerTick));
    }

    Backend()
    {
        // Initial setup
        startTimeS = timeNowUTC();
        updTime();

        // Device info
        mDeviceInfo.name = "UNA Watch";
        mDeviceInfo.manufacturer = "UNA";
        mDeviceInfo.id = "654321";
        mDeviceInfo.serialNumber = "ABCDEFABCDEFABCDEFABCDEF";
        mDeviceInfo.swVersion = "1.0.23";
        mDeviceInfo.hwVersion = "N/A";
        mDeviceInfo.btVersion = "34.5.78";
        mDeviceInfo.fccId = "N/A";
        mDeviceInfo.ce = "N/A";

        sendToGui(B2GEvent::DevInfo {mDeviceInfo});

        // Battery
        sendToGui(B2GEvent::BatteryLevel {static_cast<float>(mBatteryLevel)});

        mCommonSettings.heartRateZones = {};

        sendToGui(B2GEvent::Settings {mCommonSettings});

        sendToGui(B2GEvent::SettingsLocal {mLocalSettings});

        // BLE
        sendToGui(B2GEvent::BleBond {mBleBonded});


        // APPs
        mAppIcons.resize(mApps.size());
        for (size_t i = 0; i < mApps.size(); ++i) {
            mApps[i].type = App::Type::kActivity;
            mApps[i].appId = skAppIdBase + i;

            // Deliberately not in menu order: these arrive most-used first, so
            // the pinning has to override the order it is given.
            static const struct { uint8_t idx; uint64_t id; const char *name; } kPinned[] = {
                { 3, 0xA19C2A7E4F8B6D31ULL, "Alarm"     },
                { 4, 0xA159B1C005CFD2A5ULL, "Stopwatch" },
                { 1, 0xA1D74A3F0CFF98D3ULL, "Timer"     },
            };

            const char *pinnedName = nullptr;
            for (const auto &p : kPinned) {
                if (p.idx == i) {
                    mApps[i].appId = p.id;
                    pinnedName = p.name;
                    break;
                }
            }
            mApps[i].pidService = 100 + i;
            mApps[i].pidGUI = 200 + i;
            if (pinnedName != nullptr) {
                snprintf(mApps[i].name, App::kNameLen, "%s", pinnedName);
            } else {
                snprintf(mApps[i].name, App::kNameLen, "App_%u", i);
            }

            // Kept out of the Item, as on the watch, and handed over only when
            // a menu asks for this app's window.
            //
            // Cycled rather than derived from the index: ABGR2222 opaque red,
            // green, blue, yellow, cyan, magenta, so neighbouring apps are
            // told apart at a glance however many there are. 0xF0 + i ran off
            // the end of the opaque range past sixteen apps, and anything
            // starting at 0xC0 makes the first app opaque black on black.
            static const uint8_t kShades[] = { 0xC3, 0xCC, 0xF0, 0xCF, 0xFC, 0xF3 };
            const uint8_t shade = kShades[i % (sizeof(kShades) / sizeof(kShades[0]))];

            mAppIcons[i].appId = mApps[i].appId;
            mAppIcons[i].icon.fill(shade);
            mAppIcons[i].smallIcon.fill(shade);
        }

        // Glances
        for (uint32_t i = 0; i < skGlancesCount; ++i) {
            char tmpName[App::kNameLen]{};
            snprintf(tmpName, App::kNameLen, "glance_%u", i);
            mGlance[i].createGlance(tmpName, 100 + i);
        }
        // As the kernel does right after its app scan, ahead of Booted.
        sendGlancesAtRest("boot");

        // Daily health
        sendToGui(B2GEvent::DailyHealth {mSteps, mFloors, mActivityMinutes, mRestingHeartRate, mAverageHeartRate});
        sendToGui(B2GEvent::HeartRate {mHeartRate});

        // Final
        sendToGui(B2GEvent::Booted {});
    }
    
    virtual ~Backend() = default;

    std::queue<G2BEvent::Data> mGuiRxQueue;
    std::queue<B2GEvent::Data> mGuiTxQueue;

    /// Written this tick, readable the next. Nothing goes straight onto
    /// mGuiTxQueue -- see sendToGui().
    std::queue<B2GEvent::Data> mPendingTx;

    /**
     * @brief Hand an event to the GUI, arriving no earlier than the next tick.
     *
     * On the watch the backend is its own FreeRTOS task, so a request the GUI
     * sends cannot be answered before its next frame. This stub runs inside the
     * GUI's tick, so replying straight onto mGuiTxQueue would land the answer
     * in the very frame that asked for it -- and anything whose correctness
     * depends on whether the answer has arrived yet then passes here and fails
     * on hardware. It has done, twice: the top menu's icon window served a
     * stale cache on open, and later opened the lower half of its arc a frame
     * late, both verified clean here first.
     *
     * So every event waits a tick. It is not the full picture -- the real
     * backend can take longer, and does when it reads an icon off the eMMC --
     * but it is the difference between "same frame" and "not the same frame",
     * which is what that class of bug turns on.
     */
    void sendToGui(const B2GEvent::Data &event)
    {
        mPendingTx.push(event);
    }

    // Time
    time_t startTimeS = 0;
    std::tm mTime;

    time_t timeNowUTC() { 
        return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    }

    std::tm getSystemTime()
    {
        auto currentTime = std::chrono::system_clock::now();

        auto time = std::chrono::system_clock::to_time_t(currentTime);
        std::tm localTime;
        localtime_s(&localTime, &time);
        return localTime;
    }

    void updTime() { 
        std::tm timeNow = getSystemTime();
        if (timeNow.tm_hour != mTime.tm_hour || timeNow.tm_min != mTime.tm_min || timeNow.tm_sec != mTime.tm_sec) {
            mTime = timeNow;
            sendToGui(B2GEvent::Time {mTime});
        }
    }

    // Device info
    DeviceInfo mDeviceInfo {};

    // GPS Fix
    bool mIsGpsFix = false;
    void updGps() { 
        if (std::difftime(timeNowUTC(), startTimeS) > 10 && !mIsGpsFix) { 
            mIsGpsFix = true;
            sendToGui(B2GEvent::GpsFix {mIsGpsFix});
        }
    }

    // Magnetometer
    //
    // Enough of the part to drive the compass screen: a bearing only once a
    // calibration has been done, and a run that fills at a fixed rate.
    static constexpr uint32_t kMagFeedTicks     = 2;   // 200 ms at a 10 Hz tick
    static constexpr uint32_t kMagProgressTicks = 5;   // 500 ms

    bool     mMagFeed       = false;
    bool     mMagCalibrated = false;
    float    mMagHeading    = 0.0f;
    uint32_t mMagFeedTick   = 0;
    uint32_t mMagRunTicks   = 0;   // 0 when no run is going
    uint32_t mMagRunLeft    = 0;
    uint32_t mMagRunTick    = 0;

    void updMag()
    {
        if (mMagRunTicks > 0) {
            mMagRunLeft--;
            mMagRunTick++;

            if (mMagRunLeft == 0) {
                mMagRunTicks   = 0;
                mMagCalibrated = true;

                sendToGui(B2GEvent::MagCalProgress {100});
                sendToGui(B2GEvent::MagCalDone {true, true});
            } else if (mMagRunTick >= kMagProgressTicks) {
                mMagRunTick = 0;

                const uint32_t done = mMagRunTicks - mMagRunLeft;

                sendToGui(B2GEvent::MagCalProgress {
                        static_cast<uint8_t>((done * 100U) / mMagRunTicks)});
            }
        }

        if (!mMagFeed) {
            return;
        }

        mMagFeedTick++;

        if (mMagFeedTick < kMagFeedTicks) {
            return;
        }

        mMagFeedTick = 0;

        // Turning on its own, so the rose can be watched moving.
        mMagHeading += 3.0f;

        if (mMagHeading >= 360.0f) {
            mMagHeading -= 360.0f;
        }

        // Face up: the simulator has no tilt to model.
        sendToGui(B2GEvent::MagAzimuth {mMagHeading, mMagCalibrated, 0.0f});
    }

    // Battery
    uint8_t mBatteryLevel = 73;

    // Settings
    WatchSettings mCommonSettings {};
    WatchSettingsLocal mLocalSettings {};

    // APPs
    static constexpr uint64_t skAppIdBase = 0x1000;
    // Six, not two, and three of them carrying the uapp ids the top menu pins
    // to fixed places -- Alarm first, Stopwatch and Timer just after Settings.
    // With ids the order does not recognise, the stub never put an app and a
    // fixed entry next to each other, so nothing here exercised the interleave
    // and two bugs in it reached hardware: the restored position was thrown
    // away, and the alerts mute state was lost on every open.
    std::vector<App::Item> mApps { 6 };
    std::vector<App::IconSet> mAppIcons {};
    App::IconRef mIconWindow[App::kIconWindowMax];
    uint8_t      mIconWindowCount = 0;
    App::Item* findApp(uint32_t pid)
    {
        auto it = std::find_if(mApps.begin(), mApps.end(),
            [&](const App::Item& ctx) {
                return ctx.pidGUI == pid;
            });
        if (it != mApps.end()) {
            return &*it;
        }
        return nullptr;
    }


    // BLE
    bool mBleBonded = true;
    bool mBlePairingWait = false;
    bool mBleCodeWait = false;

    // USB
    bool mUsbConnected = false;
    bool mUsbPortOpened = false;

    // System error (test): current code cycled by the ' key
    uint32_t mErrorCode = 0;
    bool     mChargingBlocked = false;

    /// Reason the next launch is answered with, armed by 'F'. NONE launches
    /// normally, which is where it starts.
    App::LaunchFailure mForcedLaunchFailure = App::LaunchFailure::NONE;
    size_t             mForcedReason = 0;


    // Daily health
    uint32_t mSteps = 5000;
    uint32_t mFloors = 50;
    uint32_t mActivityMinutes = 70;
    uint32_t mRestingHeartRate = 67;
    uint32_t mAverageHeartRate = 85;
    float mHeartRate = 83.1f;




    /* Source: Clock24.png | 24x24 | 576 bytes ABGR2222 */
#define CLOCK24_WIDTH  24
#define CLOCK24_HEIGHT 24
#define CLOCK24_SIZE   576
    static constexpr uint8_t CLOCK24_ABGR2222[576] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x15, 0x2A, 0x2A, 0x00, 0x00, 0x80, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0x40, 0x15, 0x6A, 0x2A,
        0x00, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0x95, 0xAA, 0x2A, 0x40, 0xC1, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xEA, 0x2A,
        0x80, 0xC0, 0xC0, 0xC0, 0xC0, 0xD5, 0xD5, 0xD5, 0xD6, 0xD6, 0xD5, 0xD5, 0xD6, 0xDA, 0xD6, 0xD5,
        0xD6, 0xC1, 0xC0, 0xC0, 0xC0, 0xC0, 0xD5, 0x2A, 0x80, 0xC0, 0xC0, 0xC0, 0xEA, 0xFF, 0xFF, 0xD5,
        0xFF, 0xFF, 0xFF, 0xD5, 0xFF, 0xFF, 0xFF, 0xEA, 0xFF, 0xFF, 0xD5, 0xC0, 0xC0, 0xC0, 0xD5, 0x6A,
        0x80, 0xC0, 0xC0, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xEA, 0xC0, 0xC0, 0xC0, 0xD5, 0x6A, 0x80, 0xC0, 0xC0, 0xC0, 0xD5, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xEA, 0xEA, 0xC0, 0xC0, 0xC0, 0xD5, 0x6A,
        0x80, 0xC0, 0xC0, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xEA, 0xC0, 0xC0, 0xC0, 0xD5, 0x6A, 0x80, 0xC0, 0xC0, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xEA, 0xC0, 0xC0, 0xC0, 0xD5, 0x6A,
        0x80, 0xC0, 0xC0, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xEA, 0xC0, 0xC0, 0xC0, 0xD5, 0x6A, 0x80, 0xC0, 0xC0, 0xC0, 0xD5, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xEA, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xEA, 0xD5, 0xC0, 0xC0, 0xC0, 0xD5, 0x6A,
        0x80, 0xC0, 0xC0, 0xC0, 0xEA, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xD5, 0xC0, 0xC0, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xEA, 0xC0, 0xC0, 0xC0, 0xD5, 0x6A, 0x80, 0xC0, 0xC0, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF,
        0xEA, 0xD5, 0xFF, 0xFF, 0xFF, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF, 0xEA, 0xC0, 0xC0, 0xC0, 0xD5, 0x6A,
        0x80, 0xC0, 0xC0, 0xC0, 0xFF, 0xFF, 0xFF, 0xD5, 0xEA, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xEA, 0xC0, 0xC0, 0xC0, 0xD5, 0x6A, 0x80, 0xC0, 0xC0, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xEA, 0xC0, 0xC0, 0xC0, 0xD5, 0x6A,
        0x80, 0xC0, 0xC0, 0xC0, 0xD5, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xEA, 0xEA, 0xC0, 0xC0, 0xC0, 0xD5, 0x6A, 0x80, 0xC0, 0xC0, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xEA, 0xC0, 0xC0, 0xC0, 0xD5, 0x6A,
        0x80, 0xC0, 0xC0, 0xC0, 0xD5, 0xFF, 0xFF, 0xD5, 0xFF, 0xFF, 0xFF, 0xD5, 0xFF, 0xFF, 0xFF, 0xEA,
        0xFF, 0xFF, 0xD5, 0xC0, 0xC0, 0xC0, 0xD5, 0x6A, 0x80, 0xC0, 0xC0, 0xC0, 0xC0, 0xC5, 0xD5, 0xD5,
        0xD5, 0xD5, 0xD5, 0xC0, 0xD5, 0xD5, 0xD5, 0xD5, 0xD5, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xD6, 0x6A,
        0x40, 0xC1, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xEA, 0x2A, 0x00, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,
        0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0x55, 0xAA, 0x2A,
        0x00, 0x00, 0x80, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,
        0xC0, 0xC0, 0xC0, 0xC0, 0x40, 0x16, 0x6A, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2A, 0x2A, 0x2A
    };



    class GlanceProvider {
    public:
        GlanceProvider(){}

        // What the kernel holds: a copy taken when the app last sent.
        // As the kernel stores an update: a copy, icons included.
        void store()
        {
            App::GlanceData d = getData();
            App::CopyGlanceImages(d, mStored.pixels);
            mStored = d;
        }
        void clearStored() { mStored = App::GlanceData{}; }
        const char *name() const { return mAltName; }
        uint32_t pid() const { return mPid; }
        const App::GlanceData &stored() const { return mStored; }

        // What a stock glance shows before its sensor value arrives.
        void setPlaceholder()
        {
            mTextTime.setText("---");
            mTextTemperature.setText("---");
        }

        App::GlanceData getData() {
            App::GlanceData d{};
            d.count = static_cast<uint8_t>(mUI.size());
            for (uint8_t i = 0; i < d.count; i++) {
                d.ctrls[i] = mUI.data()[i];
            }
            snprintf(d.altname, sizeof(d.altname), mAltName);
            d.pid = mPid;

            return d;
        }
        
        void glanceUpdate()
        {
            mTemperature += 0.1f;
            if (mTemperature > 25) {
                mTemperature = 23.5f;
            }

            mTextTemperature.print("%.1f", mTemperature);

            std::time_t t = time(NULL);
            struct tm tm;
            localtime_s(&tm, &t);

            mTextTime.print("%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
        }


        void createGlance(const char* name, uint32_t pid)
        {
            mPid = pid;
            GlanceProvider& data = *this;
            snprintf(data.mAltName, sizeof(data.mAltName), name);

            // first row
            data.mUI.createRect().init(
                { 50,   0 },
                { 140, 30 },
                GlanceColor_t::GLANCE_COLOR_BLUE,
                GlanceColor_t::GLANCE_COLOR_BLACK);

            SDK::Glance::ControlRectangle panelClock = data.mUI.createRect().init({ 50,  0 },
                { 30, 30 },
                GlanceColor_t::GLANCE_COLOR_BLUE,
                GlanceColor_t::GLANCE_COLOR_BLACK);

            GlanceSize_t  clockImageSize = { CLOCK24_WIDTH, CLOCK24_HEIGHT };
            GlancePoint_t clockImagePos = SDK::Glance::Align::placeWithin(panelClock.pos(),
                panelClock.size(),
                clockImageSize,
                GlanceAlignH_t::GLANCE_ALIGN_H_CENTER,
                GlanceAlignV_t::GLANCE_ALIGN_V_CENTER);

            data.mUI.createImage().init(clockImagePos, clockImageSize, CLOCK24_ABGR2222);

            data.mTextTime = data.mUI.createText();
            data.mTextTime.pos({ 80, 2 }, { 110, 26 })
                .color(GlanceColor_t::GLANCE_COLOR_YELLOW_DARK)
                .setText("00:00:00")
                .alignment(GlanceAlignH_t::GLANCE_ALIGN_H_CENTER);

            // second row
            data.mUI.createRect().init({ 50,  30 }, { 95, 30 },
                GlanceColor_t::GLANCE_COLOR_BLUE,
                GlanceColor_t::GLANCE_COLOR_RED);

            data.mUI.createText().init({ 50, 32 }, { 90, 26 },
                "Temp, C:",
                GlanceFont_t::GLANCE_FONT_POPPINS_REGULAR_18,
                GlanceColor_t::GLANCE_COLOR_WHITE,
                GlanceAlignH_t::GLANCE_ALIGN_H_RIGHT);


            data.mUI.createRect().init({ 145, 30 }, { 45, 30 },
                GlanceColor_t::GLANCE_COLOR_BLUE,
                GlanceColor_t::GLANCE_COLOR_GREEN);

            data.mTextTemperature = data.mUI.createText();
            data.mTextTemperature.pos({ 150, 32 }, { 40, 26 })
                .color(GlanceColor_t::GLANCE_COLOR_WHITE)
                .alignment(GlanceAlignH_t::GLANCE_ALIGN_H_CENTER)
                .setText("23.5");
        }


    private:
        char mAltName[App::kNameLen]{};
        uint32_t mPid = 0;
        SDK::Glance::Form mUI{ 240, 60 };
        SDK::Glance::ControlText mTextTime;
        SDK::Glance::ControlText mTextTemperature;
        float                    mTemperature;
        App::GlanceData          mStored{};
    };

    GlanceProvider mGlance[skGlancesCount];
};

#endif // BACKEND_HPP
