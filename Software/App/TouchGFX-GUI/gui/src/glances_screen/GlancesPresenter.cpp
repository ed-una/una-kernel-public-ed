// Before every include: the view's headers bring the logger in through
// MenuGlances.hpp, and it names the module "GUI" if nothing has by then.
#define LOG_MODULE_PRX      "GUI.Glances"

#include <gui/glances_screen/GlancesView.hpp>
#include <gui/glances_screen/GlancesPresenter.hpp>
#include "gui/common/GuiLogger.hpp"

GlancesPresenter::GlancesPresenter(GlancesView& v)
    : view(v)
{

}

void GlancesPresenter::activate()
{
    // The GUI's end of the GlanceTiming lines: the kernel's own "open" follows
    // once the Backend thread takes the start request.
    LOG_DEBUG("GlanceTiming: open, frame 0\n");

    // Set current menu position
    view.setPositionId(model->getPosMenuGlances());

    // Reset nested menu position
    model->setPosNotificationListUid(0);

    // Reset idle timer
    model->resetIdleTimer();

    onPhoneNotificationListChanged(model->phoneNotificationGetAll());

    // Start Glances
    model->appGlancesStart();

    requestGlanceList();
}

void GlancesPresenter::deactivate()
{
    model->setPosMenuGlances(view.getPositionId());
    // Stop Glances
    model->appGlancesStop();
}

void GlancesPresenter::onPhoneNotificationListChanged(const std::vector<std::shared_ptr<PhoneNotification>> &list)
{
    uint32_t calls = 0;
    uint32_t emails = 0;
    uint32_t massages = 0;

    for (auto &notification : list) {
        if (notification->getCategory() == PhoneNotification::Category::ACTIVE_CALL ||
            notification->getCategory() == PhoneNotification::Category::INCOMING_CALL ||
            notification->getCategory() == PhoneNotification::Category::MISSED_CALL) {
            calls++;
        } else if (notification->getCategory() == PhoneNotification::Category::EMAIL) {
            emails++;
        } else {
            massages++;
        }
    }
    view.setNotifications(calls, emails, massages);
}

void GlancesPresenter::onAppGlanceList(const std::shared_ptr<std::vector<App::GlanceData>> &list)
{
    view.updAppList(list);
}

void GlancesPresenter::requestGlanceList()
{
    // Request app list for glances
    model->appGlancesUpdate();
}
