#include <gui/containers/MenuGlances.hpp>
#include "gui/common/GuiLogger.hpp"
#include "Components/Shared/GlanceListOrder.hpp"

#include <algorithm>
#include <cstring>

namespace {
const std::vector<App::GlanceData> kNoGlances;
}

MenuGlances::MenuGlances()
{

}

MenuGlances::~MenuGlances()
{
    clearGlanceControls();
}

void MenuGlances::initialize()
{
    MenuGlancesBase::initialize();

   // Default buttons state
    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::NONE);
    buttons.setR2(Buttons::WHITE);

    wheel.setNumberOfItems(Id::COUNT);
    wheel.setCircular(false);
    sideBar.setCount(Id::COUNT);

    // Over everything: the effect on the wheel's selected item, then the
    // Track-menu names over the whole wheel, above the effect so variant 5's
    // cubes stay under the names. Added in the simulator whatever the
    // setting, so the v key can bring any variant in.
#if !defined(SIMULATOR)
    if (Gui::Config::kGlancesScrollFx != 0)
#endif
    {
        constexpr int16_t kSelectedItemOffset = 49;   // MenuGlancesBase: wheel.setSelectedItemOffset()
        mScrollFx.setPosition(GlanceScrollFx::kX, wheel.getY() + kSelectedItemOffset,
                              GlanceScrollFx::kWidth, GlanceScrollFx::kHeight);
        add(mScrollFx);

        mOrbitFx.setPosition(GlanceOrbitFx::kX, GlanceOrbitFx::kY,
                             GlanceOrbitFx::kWidth, GlanceOrbitFx::kHeight);
        mOrbitFx.initialize();
        mOrbitFx.setSteps(Gui::Config::kGlancesOrbitSteps);
        add(mOrbitFx);

        setScrollVariant(Gui::Config::kGlancesScrollFx);
    }
}

void MenuGlances::setScrollVariant(uint8_t variant)
{
    mScrollVariant = (variant < kScrollVariants) ? variant : 0;
    mScrollFx.cancel();
    mOrbitFx.cancel();

    // 1 to 3 are GlanceScrollFx's own; 5 uses its cubes under the names.
    GlanceScrollFx::Variant fx = GlanceScrollFx::kNone;
    if (mScrollVariant >= 1 && mScrollVariant <= 3) {
        fx = static_cast<GlanceScrollFx::Variant>(mScrollVariant);
    } else if (mScrollVariant == 5) {
        fx = GlanceScrollFx::kCubeBlend;
    }
    mScrollFx.setVariant(fx);
    mOrbitFx.setCentreBox(mScrollVariant == 4);

    // The resting names take the Track menu's distance-1 tier, so the scroll
    // hands over to them without a change (update() restyles every row).
    update();
}

bool MenuGlances::isOrbitScroll() const
{
    return mScrollVariant == 4 || mScrollVariant == 5;
}

bool MenuGlances::isScrolling() const
{
    return wheel.isAnimating() || mScrollFx.isRunning() || mOrbitFx.isRunning();
}

void MenuGlances::tickScrollFx()
{
    if (mScrollVariant == 5) {
        // The names set the pace; the cubes follow them step for step.
        mOrbitFx.tick();
        mScrollFx.followStep(mOrbitFx.getStep());
        return;
    }
    mOrbitFx.tick();
    mScrollFx.tick();
}

void MenuGlances::cycleScrollFx()
{
    static const char* const kNames[kScrollVariants] = {
        "wheel (today)", "pixel break-up", "dithered wipe", "cube build",
        "Track-menu names", "Track-menu names with cubes",
    };
    setScrollVariant(static_cast<uint8_t>((mScrollVariant + 1) % kScrollVariants));
    LOG_DEBUG("GlanceFx: variant %u, %s\n", static_cast<unsigned>(mScrollVariant),
              kNames[mScrollVariant]);
}

void MenuGlances::toggleOrbitSteps()
{
    mOrbitFx.setSteps(mOrbitFx.getSteps() == 3 ? 4 : 3);
    LOG_DEBUG("GlanceFx: Track-menu names, %u steps\n", static_cast<unsigned>(mOrbitFx.getSteps()));
}

void MenuGlances::loadOrbitNames(int16_t fromRow)
{
    const int16_t count = wheel.getNumberOfItems();
    for (int8_t k = -GlanceOrbitFx::kReach; k <= GlanceOrbitFx::kReach; ++k) {
        int16_t row = static_cast<int16_t>((fromRow + k) % count);
        if (row < 0) {
            row += count;
        }
        if (row == Id::NOTIFICATION) {
            mOrbitFx.setName(k, touchgfx::TypedText(T_TEXT_NOTIFICATIONS).getText());
        } else if (row - Id::COUNT < static_cast<int16_t>(mWidgetsPool.size())) {
            mOrbitFx.setName(k, mWidgetsPool[row - Id::COUNT].altname);
        } else {
            mOrbitFx.setName(k, "");
        }
    }
}

void MenuGlances::setNotifications(uint32_t callsNum, uint32_t emailsNum, uint32_t msgsNum)
{
    mNotifCallsNum = callsNum;
    mNotifEmailsNum = emailsNum;
    mNotifMsgsNum = msgsNum;
    update();
}

void MenuGlances::updAppList(const std::shared_ptr<std::vector<App::GlanceData>> &list)
{
    // Not in the middle of a scroll: changing the rows under a moving wheel
    // would land it somewhere the user did not ask for. The newest list waits
    // for the wheel to settle (applyPendingList()); older ones are dropped.
    // The same during a scroll transition: the item under it must not change.
    if (isScrolling()) {
        mPendingList = list;
        mHasPendingList = true;
        return;
    }
    mHasPendingList = false;
    mPendingList.reset();
    applyList(list);
}

void MenuGlances::applyPendingList()
{
    if (!mHasPendingList || isScrolling()) {
        return;
    }
    mHasPendingList = false;
    const std::shared_ptr<std::vector<App::GlanceData>> list = std::move(mPendingList);
    applyList(list);
}

void MenuGlances::applyList(const std::shared_ptr<std::vector<App::GlanceData>> &list)
{
    const std::vector<App::GlanceData> &items = list ? *list : kNoGlances;

    // What is on screen now, by identity, before the rows change under it.
    const int16_t oldRow = wheel.getSelectedItem();
    const uint32_t shownId = getSelectedGlance();
    const int16_t oldCount = wheel.getNumberOfItems();

    reconcileGlanceControls(items);
    mList = list;

    const int16_t count = static_cast<int16_t>(Id::COUNT + items.size());
    if (count != oldCount) {
        // Circular first: setNumberOfItems() moves a wheel that is not.
        wheel.setCircular(count >= 2);
        wheel.setNumberOfItems(count);
        sideBar.setCount(count);
    }

    // The same item, wherever it now is; or the item asked for by
    // selectGlance(), the first time a list has it.
    int16_t row = App::GlanceListOrder::KeepRow(items, shownId, oldRow);
    if (mWantedId != kNoWantedId) {
        const int16_t wanted = App::GlanceListOrder::FindRow(items, mWantedId);
        if (wanted >= 0) {
            row = wanted;
            mWantedId = kNoWantedId;
        }
    }
    if (row != wheel.getSelectedItem()) {
        wheel.animateToItem(row, 0);
    }
    sideBar.setActiveId(row);

    updateGlanceControls(list);

    update();
}

void MenuGlances::selectGlance(uint32_t id)
{
    const int16_t row = App::GlanceListOrder::FindRow(mList ? *mList : kNoGlances, id);
    if (row < 0) {
        // Not listed yet: taken up by the first list that has it.
        mWantedId = id;
        return;
    }
    mWantedId = kNoWantedId;
    if (row != wheel.getSelectedItem()) {
        wheel.animateToItem(row, 0);
    }
    sideBar.setActiveId(row);
    update();
}

uint32_t MenuGlances::getSelectedGlance()
{
    return App::GlanceListOrder::IdAtRow(mList ? *mList : kNoGlances, wheel.getSelectedItem());
}


int16_t MenuGlances::getNumberOfItems()
{
    return wheel.getNumberOfItems();
}

void MenuGlances::selectNext()
{
    scrollBy(1);
}

void MenuGlances::selectPrev()
{
    scrollBy(-1);
}

void MenuGlances::scrollBy(int8_t direction)
{
    if (wheel.getNumberOfItems() <= 1) {
        return;
    }

    // The user's choice now: an item still waiting to be restored is not.
    mWantedId = kNoWantedId;

    const int16_t from = wheel.getSelectedItem();
    const int16_t p = from + direction;

    if (isOrbitScroll()) {
        // As below: the wheel jumps, and a press while a scroll is still on
        // screen gets no motion at all, only the next item drawn clean.
        // Variant 5 also waits for its cube layer's clean copy.
        if (mOrbitFx.isRunning() || mScrollFx.isBusy()) {
            mOrbitFx.cancel();
            mScrollFx.cancel();
            sideBar.animateToId(p, 0);
        } else {
            loadOrbitNames(from);
            mOrbitFx.start(direction);
            if (mScrollVariant == 5) {
                mScrollFx.startBlend(direction, static_cast<uint8_t>(mOrbitFx.getSteps() - 1));
            }
            sideBar.animateToId(p, mOrbitFx.getSteps());
        }
        wheel.animateToItem(p, 0);
        update();
        return;
    }

    if (mScrollFx.getVariant() == GlanceScrollFx::kNone) {
        wheel.animateToItem(p, Gui::Config::kGlancesAnimationSteps);
        sideBar.animateToId(p, Gui::Config::kGlancesAnimationSteps);
        update();
        return;
    }

    // The wheel jumps, so only whole items are ever drawn under the effect.
    // A press while the last one is still on screen gets no effect at all:
    // the framebuffer holds effect pixels then, not a clean item to break
    // up, so the next item is simply drawn clean. Fast presses therefore go
    // item to clean item, never through an empty or half-drawn one.
    if (mScrollFx.isBusy()) {
        mScrollFx.cancel();
        sideBar.animateToId(p, 0);
    } else {
        mScrollFx.start(direction);
        sideBar.animateToId(p, Gui::Config::kGlancesAnimationSteps);
    }
    wheel.animateToItem(p, 0);
    update();
}

void MenuGlances::selectItem(int16_t itemIndex)
{
    if (wheel.getNumberOfItems() <= 1) {
        return;
    }

    mScrollFx.cancel();
    mOrbitFx.cancel();
    wheel.animateToItem(itemIndex, 0);
    sideBar.setActiveId(wheel.getSelectedItem());

    update();
}

uint16_t MenuGlances::getSelectedItem()
{
    return wheel.getSelectedItem();
}

bool MenuGlances::isNotificationSelected()
{
    return wheel.getSelectedItem() == Id::NOTIFICATION;
}

void MenuGlances::selectNextLastActivity()
{

}

void MenuGlances::wheelUpdateItem(MenuGlancesItem &item, int16_t itemIndex)
{
    item.setOrbitStyle(isOrbitScroll());

    if (itemIndex == Id::NOTIFICATION) {
        item.setName(T_TEXT_NOTIFICATIONS);
        return;
    }

    if (itemIndex < Id::COUNT) {
        return; // must be handled above
    }
    itemIndex -= Id::COUNT;

    // Use alternative name for glance
    if (mWidgetsPool.empty() || itemIndex >= static_cast<int16_t>(mWidgetsPool.size())) {
        return;
    }

    DrawableGlanceData& glance = mWidgetsPool.at(itemIndex);
    item.setName(glance.altname);

}

void MenuGlances::wheelUpdateCenterItem(MenuGlancesCenterItem &item, int16_t itemIndex)
{

    item.removeAll();

    if (itemIndex == Id::NOTIFICATION) {
        item.setNotifications(mNotifCallsNum, mNotifEmailsNum, mNotifMsgsNum);
        item.invalidate();
        return;
    }

    if (itemIndex < Id::COUNT) {
        return; // must be handled above
    }
    itemIndex -= Id::COUNT;

    if (itemIndex >= static_cast<int16_t>(mWidgetsPool.size())) {
        return;
    }

    for (DrawableGlanceData::ControlMapItem& mapItem : mWidgetsPool[itemIndex].controlMap) {
        item.add(*mapItem.d);
        //LOG_DEBUG("%s, %d, 0x%08X\n", mWidgetsPool[itemIndex].altname, mapItem.id, mapItem.d);
    }

    item.invalidate();
}

void MenuGlances::update()
{
    if (wheel.getSelectedItem() == NOTIFICATION/* || 
        (wheel.getSelectedItem() == LAST_ACTIVITY && mpApplist && *mpApplist->pNum > 1)*/) 
    {
        buttons.setR1(Buttons::AMBER);
    } else { 
        buttons.setR1(Buttons::NONE);
    }

    for (int i = 0; i < wheel.getNumberOfItems(); i++) {
        wheel.itemChanged(i);
    }

    wheel.invalidate();
}

void MenuGlances::clearGlanceControls()
{
    // 1) Ensure containers don't hold references to these drawables:
    // For each center item in your wheel/selected list, remove children.
    // You need access to those center item instances; e.g.:
    for (int i = 0; i < wheelSelectedListItems.getNumberOfDrawables(); ++i) {
        MenuGlancesCenterItem* center = static_cast<MenuGlancesCenterItem*>(
            wheelSelectedListItems.getDrawable(i));
        if (center) {
            center->removeAll();
        }
    }

    // 2) Now delete owned drawables
    for (auto& glance : mWidgetsPool) {
        for (DrawableGlanceData::ControlMapItem &item : glance.controlMap) {
            delete item.d;
            item.d = nullptr;
        }
        glance.controlMap.clear();
    }
    mWidgetsPool.clear();
}

void MenuGlances::reconcileGlanceControls(const std::vector<App::GlanceData> &list)
{
    // Nothing may still show a drawable that is about to go; update() puts
    // back the ones that stay.
    for (int i = 0; i < wheelSelectedListItems.getNumberOfDrawables(); ++i) {
        MenuGlancesCenterItem* center = static_cast<MenuGlancesCenterItem*>(
            wheelSelectedListItems.getDrawable(i));
        if (center) {
            center->removeAll();
        }
    }

    const auto sameApp = [](const DrawableGlanceData &row, const App::GlanceData &g) {
        return (row.pid != 0 || g.pid != 0) ? row.pid == g.pid
                                            : std::strncmp(row.altname, g.altname, sizeof(row.altname)) == 0;
    };

    // Rows in the new list's order. A row whose app is still listed keeps its
    // drawables, so a list that only adds or drops one glance rebuilds none
    // of the others. Every entry gets a row, an empty one included: the
    // kernel lists a glance by name alone while its app starts on the first
    // visit after boot. Controls are made or brought up to date afterwards,
    // by updateGlanceControls().
    std::vector<DrawableGlanceData> rows;
    rows.reserve(list.size());
    std::vector<bool> taken(mWidgetsPool.size(), false);
    for (const App::GlanceData &g : list) {
        size_t j = 0;
        while (j < mWidgetsPool.size() && (taken[j] || !sameApp(mWidgetsPool[j], g))) {
            ++j;
        }
        if (j < mWidgetsPool.size()) {
            taken[j] = true;
            rows.push_back(std::move(mWidgetsPool[j]));
            mWidgetsPool[j].controlMap.clear();
        } else {
            DrawableGlanceData &row = rows.emplace_back();
            row.pid = g.pid;
            snprintf(row.altname, sizeof(row.altname), "%s", g.altname);
        }
    }

    // The apps no longer listed.
    for (size_t j = 0; j < mWidgetsPool.size(); ++j) {
        if (taken[j]) {
            continue;
        }
        for (DrawableGlanceData::ControlMapItem &item : mWidgetsPool[j].controlMap) {
            delete item.d;
            item.d = nullptr;
        }
    }
    mWidgetsPool = std::move(rows);
}

void MenuGlances::updateGlanceControls(const std::shared_ptr<std::vector<App::GlanceData>> &list)
{
    if (!list || mWidgetsPool.size() != list->size()) {
        return;
    }

    for (size_t i = 0; i < list->size(); i++) {
        const auto& ctrls = (*list)[i];
        DrawableGlanceData &existGlance = mWidgetsPool[i];

        for (uint8_t c = 0; c < ctrls.count; c++) {
            
            auto it = std::find_if(existGlance.controlMap.begin(), existGlance.controlMap.end(),
                [&](const DrawableGlanceData::ControlMapItem& item) {
                    return item.id == ctrls.ctrls[c].id;
                });

            if (it != existGlance.controlMap.end()) {
                DrawableGlanceData::ControlMapItem& item = *it;
                //LOG_DEBUG("upd control: %s, id=%d, 0x%08X\n", existGlance.altname, item.id, item.d);

                update(ctrls.ctrls[c], item.d);
            } else {
                // A control this row has not had yet: a glance listed by name
                // alone while its app started gets its controls here, when it
                // first reports, in a list the same length as the last.
                Drawable* drawable = create(ctrls.ctrls[c]);
                if (drawable) {
                    DrawableGlanceData::ControlMapItem item{};
                    item.id = ctrls.ctrls[c].id;
                    item.d = drawable;
                    existGlance.controlMap.push_back(item);

                    // Straight away, as for every control made: the text and
                    // box constructors take their size in the wrong order,
                    // and update() is what puts it right.
                    update(ctrls.ctrls[c], drawable);
                }
            }
        }
        // Controls the app no longer sends.
        for (auto it = existGlance.controlMap.begin(); it != existGlance.controlMap.end();) {
            const uint32_t id = it->id;
            const bool sent = std::any_of(ctrls.ctrls, ctrls.ctrls + ctrls.count,
                [id](const GlanceControl_t &c) { return c.id == id; });
            if (sent) {
                ++it;
            } else {
                delete it->d;
                it = existGlance.controlMap.erase(it);
            }
        }
    }
}

Drawable* MenuGlances::create(const GlanceControl_t& ctrl)
{
    switch (ctrl.type) {
    case GlanceType_t::GLANCE_TYPE_TEXT:
    {
        TextDrawable* w = new (std::nothrow)TextDrawable(ctrl.payload.text.pos.x, 
                                           ctrl.payload.text.pos.y,
                                           ctrl.payload.text.size.w,
                                           ctrl.payload.text.size.h,
                                           GlanceColorToRGB(ctrl.payload.text.color), 
                                           ctrl.payload.text.str,
                                           GlanceTextToTypedText(ctrl.payload.text.font, ctrl.payload.text.align));
        return w;
    } break;
    case GlanceType_t::GLANCE_TYPE_IMAGE:
    {
        ImageDrawable* w = new (std::nothrow)ImageDrawable(ctrl.payload.image.pos.x,
                                             ctrl.payload.image.pos.y, 
                                             ctrl.payload.image.size.w, 
                                             ctrl.payload.image.size.h, 
                                             ctrl.payload.image.buff);
        return w;
    } break;
    case GlanceType_t::GLANCE_TYPE_RECT:
    {
        BoxDrawable* w = new (std::nothrow)BoxDrawable(ctrl.payload.rect.pos.x,
                                         ctrl.payload.rect.pos.y,
                                         ctrl.payload.rect.size.w, 
                                         ctrl.payload.rect.size.h,
                                         GlanceColorToRGB(ctrl.payload.rect.color),
                                         GlanceColorToRGB(ctrl.payload.rect.bgColor));
        return w;
    } break;
    case GlanceType_t::GLANCE_TYPE_LINE:
    {
        LineDrawable* w = new (std::nothrow)LineDrawable(ctrl.payload.line.start.x,
                                           ctrl.payload.line.start.y,
                                           ctrl.payload.line.stop.x,
                                           ctrl.payload.line.stop.y,
                                           GlanceColorToRGB(ctrl.payload.line.color));
        return w;
    } break;
    default:
        break;
    }

    return nullptr;
}

void MenuGlances::update(const GlanceControl_t& ctrl, Drawable* d)
{
    switch (ctrl.type) {

    case GlanceType_t::GLANCE_TYPE_TEXT:
    {
        TextDrawable& w = *(reinterpret_cast<TextDrawable*>(d));
        w.update(ctrl.payload.text.pos.x,
                 ctrl.payload.text.pos.y,
                 ctrl.payload.text.size.w,
                 ctrl.payload.text.size.h,
                 GlanceColorToRGB(ctrl.payload.text.color),
                 ctrl.payload.text.str,
                 GlanceTextToTypedText(ctrl.payload.text.font, ctrl.payload.text.align));
    } break;

    case GlanceType_t::GLANCE_TYPE_IMAGE:
    {
        ImageDrawable& w = *(reinterpret_cast<ImageDrawable*>(d));

        const bool shouldBeUpdated = w.getHeight() != ctrl.payload.image.size.h ||
                                     w.getWidth()  != ctrl.payload.image.size.w ||
                                     w.getPixels() != ctrl.payload.image.buff;

        if (shouldBeUpdated) {
            w.update(ctrl.payload.image.pos.x,
                     ctrl.payload.image.pos.y,
                     ctrl.payload.image.size.w,
                     ctrl.payload.image.size.h,
                     ctrl.payload.image.buff);
        } else {
            w.setXY(ctrl.payload.image.pos.x, ctrl.payload.image.pos.y);
            w.invalidate();
        }
    } break;

    case GlanceType_t::GLANCE_TYPE_RECT:
    {
        BoxDrawable& w = *(reinterpret_cast<BoxDrawable*>(d));
        w.update(ctrl.payload.rect.pos.x,
                 ctrl.payload.rect.pos.y,
                 ctrl.payload.rect.size.w,
                 ctrl.payload.rect.size.h,
                 GlanceColorToRGB(ctrl.payload.rect.color),
                 GlanceColorToRGB(ctrl.payload.rect.bgColor));
    } break;

    case GlanceType_t::GLANCE_TYPE_LINE:
    {
        LineDrawable& w = *(reinterpret_cast<LineDrawable*>(d));
        w.update(ctrl.payload.line.start.x,
                 ctrl.payload.line.start.y,
                 ctrl.payload.line.stop.x,
                 ctrl.payload.line.stop.y,
                 GlanceColorToRGB(ctrl.payload.line.color));
    } break;

    default: return;
    }

    if (d->isVisible() != ctrl.visible) {
        d->setVisible(ctrl.visible);
        d->invalidate();
    }
}

touchgfx::colortype MenuGlances::GlanceColorToRGB(uint8_t color) const
{
    uint8_t r = (color >> 4) << 6;
    uint8_t g = ((color >> 2) & 0x03) << 6;
    uint8_t b = (color & 0x03) << 6;

    return (static_cast<uint32_t>(r) << 16) |
        (static_cast<uint32_t>(g) << 8) |
        (static_cast<uint32_t>(b));
}

const touchgfx::TypedText MenuGlances::GlanceTextToTypedText(uint8_t font, uint8_t align) const
{
    constexpr TEXTS lut[GLANCE_FONTS_COUNT]
    {
        T_TMP_REGULAR_18,
        T_TMP_MEDIUM_10,
        T_TMP_MEDIUM_18,
        T_TMP_MEDIUM_25,
        T_TMP_SEMIBOLD_18,
        T_TMP_SEMIBOLD_20,
        T_TMP_SEMIBOLD_25,
        T_TMP_SEMIBOLD_30,
        T_TMP_SEMIBOLD_35,
        T_TMP_ITALIC_18,
        T_TMP_ITALIC_20,
    };

    constexpr TEXTS lutr[GLANCE_FONTS_COUNT]
    {
        T_TMP_REGULAR_18_R,
        T_TMP_MEDIUM_10_R,
        T_TMP_MEDIUM_18_R,
        T_TMP_MEDIUM_25_R,
        T_TMP_SEMIBOLD_18_R,
        T_TMP_SEMIBOLD_20_R,
        T_TMP_SEMIBOLD_25_R,
        T_TMP_SEMIBOLD_30_R,
        T_TMP_SEMIBOLD_35_R,
        T_TMP_ITALIC_18_R,
        T_TMP_ITALIC_20_R,
    };

    constexpr TEXTS lutl[GLANCE_FONTS_COUNT]
    {
        T_TMP_REGULAR_18_L,
        T_TMP_MEDIUM_10_L,
        T_TMP_MEDIUM_18_L,
        T_TMP_MEDIUM_25_L,
        T_TMP_SEMIBOLD_18_L,
        T_TMP_SEMIBOLD_20_L,
        T_TMP_SEMIBOLD_25_L,
        T_TMP_SEMIBOLD_30_L,
        T_TMP_SEMIBOLD_35_L,
        T_TMP_ITALIC_18_L,
        T_TMP_ITALIC_20_L,
    };

    switch (align) {
    case GLANCE_ALIGN_H_LEFT:
        return touchgfx::TypedText(lutl[font]);
    case GLANCE_ALIGN_H_CENTER: 
        return touchgfx::TypedText(lut[font]);
    case GLANCE_ALIGN_H_RIGHT: 
        return touchgfx::TypedText(lutr[font]);
    default:
        return touchgfx::TypedText(lut[0]);
    }
}
