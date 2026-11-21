#ifndef MENUGLANCES_HPP
#define MENUGLANCES_HPP

#include <gui_generated/containers/MenuGlancesBase.hpp>
#include <touchgfx/widgets/canvas/Line.hpp>
#include <touchgfx/widgets/BoxWithBorder.hpp>
#include <gui/containers/GlanceScrollFx.hpp>
#include <gui/containers/GlanceOrbitFx.hpp>

#include "gui/common/GuiLogger.hpp"


class MenuGlances : public MenuGlancesBase
{
public:
    MenuGlances();
    virtual ~MenuGlances();

    virtual void initialize();

    enum Id {
        NOTIFICATION = 0,
        COUNT
    };

    void setNotifications(uint32_t callsNum, uint32_t emailsNum, uint32_t msgsNum);

    /**
     * @brief   Show a new glance list without moving the user.
     * @details The wheel stays on the same item, matched by app, wherever the
     *          new list puts it, and only rows whose app came or went are
     *          rebuilt. A list that arrives during a scroll waits for it to
     *          end (applyPendingList()).
     */
    void updAppList(const std::shared_ptr<std::vector<App::GlanceData>> &list);

    /// Apply a list that arrived during a scroll, once the wheel has settled.
    /// Call every tick.
    void applyPendingList();

    /**
     * @brief   Select an item by identity (App::GlanceListOrder): a glance
     *          app's PID, or kNotifications.
     * @details If it is not listed yet, the first list that has it selects it,
     *          unless the user has moved by then.
     */
    void selectGlance(uint32_t id);

    /// The identity of the selected item, for selectGlance() on the next visit.
    uint32_t getSelectedGlance();


    int16_t getNumberOfItems();

    void selectNext();
    void selectPrev();
    void selectItem(int16_t itemIndex);

    /// Advance the scroll transition. Call every tick.
    void tickScrollFx();

    /// The next scroll transition (Gui::Config::kGlancesScrollFx). Simulator
    /// only, for comparing them in one build.
    void cycleScrollFx();

    /// 3 or 4 frames for the Track-menu scroll (variants 4 and 5). Simulator
    /// only, for comparing them.
    void toggleOrbitSteps();
    uint16_t getSelectedItem();

    bool isNotificationSelected();

    void selectNextLastActivity();


protected:
    virtual void wheelUpdateItem(MenuGlancesItem &item, int16_t itemIndex) override;
    virtual void wheelUpdateCenterItem(MenuGlancesCenterItem &item, int16_t itemIndex) override;

    void update();

    /// One press of L1 or L2: @p direction +1 next, -1 previous.
    void scrollBy(int8_t direction);

    /// A wheel animation or a scroll transition is under way: a new list
    /// waits for it (applyPendingList()).
    bool isScrolling() const;

    /// Drawn over the centre item when a scroll transition is chosen.
    GlanceScrollFx mScrollFx;

    /// The names moving like the Track menu's rows (variants 4 and 5).
    GlanceOrbitFx mOrbitFx;

    /// Gui::Config::kGlancesScrollFx, or what the simulator's v key chose.
    static constexpr uint8_t kScrollVariants = 7;
    uint8_t mScrollVariant = 0;

    void setScrollVariant(uint8_t variant);
    bool isOrbitScroll() const;

    /// The names of the rows a scroll from @p fromRow passes.
    void loadOrbitNames(int16_t fromRow);

    uint32_t mNotifCallsNum = 0;
    uint32_t mNotifEmailsNum = 0;
    uint32_t mNotifMsgsNum = 0;

    class TextDrawable : public touchgfx::TextAreaWithOneWildcard {
    public:
        touchgfx::Unicode::UnicodeChar buffer[GLANCE_TEXT_SIZE];

        TextDrawable(int16_t x, int16_t y, int16_t w, int16_t h, touchgfx::colortype newColor, const char* txt, const touchgfx::TypedText& t)
        {
            setWildcard(buffer);
            update(x, y, newColor, w, h, txt, t);
        }

        virtual ~TextDrawable()
        {}

        void update(int16_t x, int16_t y, int16_t w, int16_t h, touchgfx::colortype newColor, const char* txt, const touchgfx::TypedText& t) {
            setPosition(x, y, w, h);
            setColor(newColor);
            setLinespacing(0);
            Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(txt), buffer, GLANCE_TEXT_SIZE);
            setTypedText(t);
            setVisible(true);
            invalidate();
        }
    };

    class ImageDrawable : public touchgfx::Image {
    public:
        BitmapId id = BITMAP_INVALID;

        ImageDrawable(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* pixels)
            : mPixels(nullptr)
            , mOwnedPixels()
        {
            create(x, y, w, h, pixels);
        }

        virtual ~ImageDrawable()
        {
            destroy();
        }

        void update(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* pixels)
        {
            destroy();

            id = BITMAP_INVALID;

            create(x, y, w, h, pixels);
        }

        const uint8_t* getPixels() const
        {
            return mPixels;
        }

    private:
        void create(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t* pixels)
        {
            mPixels = pixels;

            mOwnedPixels.resize(w * h);
            memcpy(mOwnedPixels.data(), pixels, w * h);

            setPosition(x, y, w, h);

            id = Bitmap::dynamicBitmapCreateExternal(w, h, mOwnedPixels.data(), Bitmap::ABGR2222);
            setBitmap(Bitmap(id));
            setVisible(true);

            invalidate();
        }

        void destroy()
        {
            if (id != BITMAP_INVALID) {
                Bitmap::dynamicBitmapDelete(id);
            }
        }

        const uint8_t*                  mPixels;
        std::vector<uint8_t>            mOwnedPixels;
    };

    class BoxDrawable : public touchgfx::BoxWithBorder {
    public:
        BoxDrawable(int16_t x, int16_t y, int16_t w, int16_t h, touchgfx::colortype color, touchgfx::colortype bgColor)
        {
            update(x, y, h, w, color, bgColor);
        }

        virtual ~BoxDrawable() {}

        void update(int16_t x, int16_t y, int16_t w, int16_t h, touchgfx::colortype color, touchgfx::colortype bgColor)
        {
            setPosition(x, y, w, h);
            setBorderColor(color);
            setBorderSize(1);
            setColor(bgColor);
            setVisible(true);
            invalidate();
        }
    };

    class LineDrawable : public touchgfx::Line {
    public:
        touchgfx::PainterABGR2222 painter;

        LineDrawable(int16_t x1, int16_t y1, int16_t x2, int16_t y2, touchgfx::colortype newColor)
        {
            setPainter(painter);
            update(x1, y1, x2, y2, newColor);
        }

        virtual ~LineDrawable() {}

        void update(int16_t x1, int16_t y1, int16_t x2, int16_t y2, touchgfx::colortype newColor)
        {
            setPosition(x1, y1, x2, y2);

            painter.setColor(newColor);
            setStart(x1, y1);
            setEnd(x2, y2);
            setLineWidth(1);
            setLineEndingStyle(touchgfx::Line::BUTT_CAP_ENDING);

            setVisible(true);
            invalidate();
        }
    };

    void applyList(const std::shared_ptr<std::vector<App::GlanceData>> &list);
    void clearGlanceControls();
    void updateGlanceControls(const std::shared_ptr<std::vector<App::GlanceData>> &list);

    struct DrawableGlanceData {
        struct ControlMapItem {
            Drawable* d;
            uint32_t id;
        };
        std::vector<ControlMapItem> controlMap;
        char altname[App::kNameLen]{};
        uint32_t pid = 0;   ///< the glance app's service PID, its identity
    };
    std::vector<DrawableGlanceData> mWidgetsPool;

    /// Make the rows follow @p list, by app.
    void reconcileGlanceControls(const std::vector<App::GlanceData> &list);

    /// The list on screen, for finding an item's row by identity.
    std::shared_ptr<std::vector<App::GlanceData>> mList;

    /// The newest list that arrived during a scroll (may be null: no glances).
    std::shared_ptr<std::vector<App::GlanceData>> mPendingList;
    bool mHasPendingList = false;

    /// An item to select as soon as a list has it (selectGlance()).
    static constexpr uint32_t kNoWantedId = 0xFFFFFFFFu;
    uint32_t mWantedId = kNoWantedId;


    Drawable* create(const GlanceControl_t& ctrl);
    void update(const GlanceControl_t& ctrl, Drawable* d);
    touchgfx::colortype GlanceColorToRGB(uint8_t color) const;
    const touchgfx::TypedText GlanceTextToTypedText(uint8_t font, uint8_t align) const;
};

#endif // MENUGLANCES_HPP
