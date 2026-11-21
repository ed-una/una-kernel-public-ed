#ifndef GLANCEORBITFX_HPP
#define GLANCEORBITFX_HPP

#include <touchgfx/containers/Container.hpp>
#include <touchgfx/hal/Types.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/widgets/Widget.hpp>
#include <touchgfx/Unicode.hpp>

#include <cstdint>

/**
 * @brief   The Glances scroll moving like the Track menu (prototype).
 * @details OrbitMenu lays every row out whole, every frame, from one
 *          fractional scroll position, picking each label's font and colour
 *          by its distance from the centre (OrbitTiers). The Glances wheel
 *          instead splits an item between two templates in mid-scroll. So
 *          for these variants the wheel jumps straight to the new item, and
 *          this container covers the wheel's area for the frames in between:
 *          black boxes hide what the wheel drew (TouchGFX then skips drawing
 *          it), and the glance names are laid out on top the way OrbitMenu
 *          lays out its rows. On the frame the scroll settles it hides, and
 *          the wheel at rest shows the new item, its names drawn in the
 *          distance-1 tier (MenuGlancesItem::setOrbitStyle()) so nothing
 *          changes at the hand-over.
 *
 *          Names are drawn in three regions, the two neighbour bands and the
 *          centre window, each clipping what it holds, so a name crossing a
 *          teal separator line is cut at the line rather than drawn over it.
 *          The separator lines, the side arcs and the button marks are never
 *          covered. A glance's own content is not moved or scaled: it appears
 *          when the scroll settles. See Mode for what each variant puts in
 *          the centre.
 */
class GlanceOrbitFx : public touchgfx::Container
{
public:
    /// The wheel's area, in screen pixels, between the side arcs.
    static constexpr int16_t kX = 22;
    static constexpr int16_t kY = 41;
    static constexpr int16_t kWidth = 175;
    static constexpr int16_t kHeight = 158;

    /// Where the wheel puts a row: the selected item's top, the row pitch,
    /// and the name's horizontal extent (MenuGlancesItemBase).
    static constexpr int16_t kCentreRowTop = 90;
    static constexpr int16_t kRowPitch = 60;
    static constexpr int16_t kNameX = 35;
    static constexpr int16_t kNameW = 170;

    /// Names kept for one scroll: the outgoing item and three either side.
    static constexpr int8_t kReach = 3;
    static constexpr uint16_t kNameLen = 24;

    GlanceOrbitFx();

    /// Build the children. Call once, after setting the position.
    void initialize();

    /// What the centre window shows during a scroll.
    enum class Mode : uint8_t {
        kNamesOverBox,      ///< variant 4: names; a black box hides the content
        kNamesOverCubes,    ///< variant 5: names over GlanceScrollFx's cubes
        kNamesInBands,      ///< variant 6: no names; the centre is the cubes'
    };
    void setMode(Mode mode);

    /// Frames per scroll, settled frame included (OrbitMenu's animation
    /// steps). Takes effect on the next scroll.
    void setSteps(uint8_t steps);
    uint8_t getSteps() const { return mSteps; }

    /// The name of the item @p offset rows from the outgoing one (-kReach
    /// to kReach). Kept until the next scroll.
    void setName(int8_t offset, const touchgfx::Unicode::UnicodeChar* name);
    void setName(int8_t offset, const char* utf8);

    /// Start one scroll: +1 when the new item comes from below.
    void start(int8_t direction);

    /// End at once; the wheel under it shows the new item.
    void cancel();

    /// Once per frame, from the view's tick.
    void tick();

    bool isRunning() const { return mStep != 0; }

    /// 0 idle, else the in-between frame being drawn (1 to getSteps() - 1).
    uint8_t getStep() const { return mStep; }

private:
    // Marks drawn first and last, so the draw of everything between them
    // (boxes and names) can be timed whichever way TouchGFX walks the tree.
    class Mark : public touchgfx::Widget
    {
    public:
        explicit Mark(const GlanceOrbitFx& owner, bool end) : mOwner(owner), mEnd(end) {}
        virtual void draw(const touchgfx::Rect&) const override;
        virtual touchgfx::Rect getSolidRect() const override { return touchgfx::Rect(); }
    private:
        const GlanceOrbitFx& mOwner;
        bool mEnd;
    };

    static constexpr uint8_t kMaxSteps = 4;
    static constexpr int kRows = 4;     ///< at most four names in a region at once

    /// One clipping region: a black background and the names in it.
    struct Region {
        touchgfx::Container clip;       ///< its children are clipped to it
        touchgfx::Box background;
        touchgfx::TextAreaWithOneWildcard rows[kRows];
        touchgfx::Rect screen;          ///< where it is, in screen pixels
    };
    enum { kTop = 0, kCentre = 1, kBottom = 2, kRegions = 3 };

    void layout();
    void logCost() const;

    Mark mStart;
    Region mRegions[kRegions];
    Mark mEnd;

    /// Not on screen: sizes a name in a tier's font before it is placed.
    touchgfx::TextAreaWithOneWildcard mMeasure;
    Mode mMode = Mode::kNamesOverBox;

    touchgfx::Unicode::UnicodeChar mNames[2 * kReach + 1][kNameLen] = {};

    uint8_t mSteps = 3;
    uint8_t mStep = 0;
    int8_t mDirection = 1;

    mutable uint8_t mDrawnStep = 0;
    mutable uint32_t mMarkTicks = 0;
    mutable uint32_t mStepUs[kMaxSteps] = {};
};

#endif // GLANCEORBITFX_HPP
