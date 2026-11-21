// Before every include: the view's headers bring the logger in through
// MenuGlances.hpp, and it names the module "GUI" if nothing has by then.
#define LOG_MODULE_PRX      "GUI.Glances"

#include <gui/glances_screen/GlancesView.hpp>
#include "gui/common/GuiLogger.hpp"

GlancesView::GlancesView()
{

}

void GlancesView::setupScreen()
{
    GlancesViewBase::setupScreen();

    title.set(T_TEXT_GLANCES_UC);
}

void GlancesView::tearDownScreen()
{
    GlancesViewBase::tearDownScreen();
}

void GlancesView::setPositionId(uint16_t id)
{
    menu.selectItem(id);
}

uint16_t GlancesView::getPositionId()
{
    return menu.getSelectedItem();
}

void GlancesView::updAppList(const std::shared_ptr<std::vector<App::GlanceData>> &list)
{
    const int16_t itemsBefore = menu.getNumberOfItems();
    menu.updAppList(list);

    // Counted in frames, the unit the screen is drawn in: frame 1 is the first
    // one after the screen opened. Lists arrive in the model's tick, which runs
    // before this screen's, so this frame is one more than the screen has
    // counted. A change in the item count is what makes the wheel jump, so it
    // is printed before and after.
    LOG_DEBUG("GlanceTiming: list applied at frame %lu: %lu glances, items %d -> %d\n",
              static_cast<unsigned long>(mFramesSinceOpen + 1),
              static_cast<unsigned long>(list ? list->size() : 0),
              static_cast<int>(itemsBefore),
              static_cast<int>(menu.getNumberOfItems()));
}

void GlancesView::setNotifications(uint32_t callsNum, uint32_t emailsNum, uint32_t msgsNum)
{
    menu.setNotifications(callsNum, emailsNum, msgsNum);
}

void GlancesView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {
        mCounter = Gui::Config::kGlanceScreenTimeout;
        menu.selectPrev();
    }

    if (key == Gui::Config::Button::L2) {
        mCounter = Gui::Config::kGlanceScreenTimeout;
        menu.selectNext();
    }

    if (key == Gui::Config::Button::R1) {
        if (menu.isNotificationSelected()) {
            application().gotoPhoneNotificationListScreenNoTransition();
        }
        menu.selectNextLastActivity();
    }

    if (key == Gui::Config::Button::R2) {
        application().gotoHomeScreenNoTransition();
    }
}

void GlancesView::handleTickEvent()
{
    mFramesSinceOpen++;

    if (mCounter > 0) {
        mCounter--;
    }
    if (mCounter == 0) {
        application().gotoHomeScreenNoTransition();
    }

    if (mGlanceUpdCounter % GUI_CONFIG_MS_2_TICKS(1000) == 0) {
        presenter->requestGlanceList();
    }
    mGlanceUpdCounter++;
}
