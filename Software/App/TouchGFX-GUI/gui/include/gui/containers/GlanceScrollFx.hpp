#ifndef GLANCESCROLLFX_HPP
#define GLANCESCROLLFX_HPP

#include <touchgfx/hal/Types.hpp>
#include <touchgfx/widgets/Widget.hpp>

#include <cstdint>
#include <vector>

/**
 * @brief   A stepped transition over the Glances centre item (prototype).
 * @details At 10 fps the wheel's own in-between frames draw two items over
 *          each other. With an effect chosen, the wheel jumps straight to
 *          the new item, so only clean items are ever drawn under this, and
 *          this widget paints a deliberate pixel-art step on top for
 *          kFxFrames frames. It covers the centre window only and writes
 *          the framebuffer directly (one byte a pixel, ABGR2222), inside the
 *          area TouchGFX asks it to draw and never outside its own rect.
 *
 *          The outgoing item comes from a copy of the window that every clean
 *          draw of it refreshes (the rows TouchGFX has just drawn under this),
 *          not from the framebuffer at the press: a double-buffered display
 *          (the simulator's) hands out the frame before last there. start()
 *          is only allowed once the window has been drawn clean in full since
 *          the last effect (isBusy()), so the copy is what is on screen.
 *
 *          Variants (Gui::Config::kGlancesScrollFx):
 *          1. Pixel break-up: the old item as half its 6 px blocks, then the
 *             new item as the other half, then crisp.
 *          2. Dithered wipe: a wipe in the direction of travel whose edge is
 *             a band of 4 x 4 ordered dither between the two items.
 *          3. Cube build: every 10 px cell of the new item with ink in it
 *             becomes a small isometric cube in its colour, arriving from
 *             the direction of travel, then settling, then crisp.
 */
class GlanceScrollFx : public touchgfx::Widget
{
public:
    enum Variant : uint8_t {
        kNone = 0,
        kPixelBreak = 1,
        kDitherWipe = 2,
        kCubeBuild = 3,
        /// Variant 5's centre: the outgoing content as cubes on the first
        /// frame, the incoming content landing as cubes on the last, under
        /// GlanceOrbitFx's names, which drive the steps (followStep()).
        kCubeBlend = 4,
        kVariantCount
    };

    /// Frames drawn with the effect after a press; the next one is clean.
    static constexpr uint8_t kFxFrames = 2;
    static constexpr uint8_t kMaxFxFrames = 3;

    /// Cycle counter on the watch, steady clock in the simulator, for the
    /// GlanceFx cost lines.
    static uint32_t ticksNow();
    static uint32_t ticksToUs(uint32_t ticks);

    /// The centre window, in screen pixels: the wheel's selected item (60 px
    /// tall), between the side arcs (lit up to x 18 on the left and from
    /// x 198 on the right). Glance content measured x 25 to 189.
    static constexpr int16_t kX = 22;
    static constexpr int16_t kWidth = 174;
    static constexpr int16_t kHeight = 60;

    GlanceScrollFx();

    void setVariant(Variant v);
    Variant getVariant() const { return mVariant; }
    static const char* variantName(Variant v);

    /**
     * @brief   An effect is running, or its last frame is still on screen.
     * @details A press while busy must not start another: the framebuffer
     *          holds effect pixels, not a clean outgoing item. The caller
     *          cuts straight to the next item instead (cancel()).
     */
    bool isBusy() const { return mVariant != kNone && (mStep != 0 || mAwaitingClean); }

    /// Effect frames are being drawn: the item under them must not change,
    /// so a new glance list waits. Not the clean frame after them, which
    /// may show a new list as well as any.
    bool isRunning() const { return mStep != 0; }

    /**
     * @brief   Start the transition for one press.
     * @param   direction: +1 when the new item comes from below (next),
     *          -1 when it comes from above (previous).
     * @note    Call from the key handler, before the next frame is drawn and
     *          only when !isBusy(). Does nothing for kNone.
     */
    void start(int8_t direction);

    /**
     * @brief   Start variant 5's or 6's centre for one press.
     * @param   frames: in-between frames of the scroll it goes with (2 or 3).
     * @param   settleInPlace: with 3 frames, land the incoming cubes short of
     *          their places on frame 2 and in place on frame 3 (variant 6),
     *          rather than leave frame 2 empty and land them on frame 3
     *          (variant 5). With 2 frames both land them on frame 2.
     * @param   logAs: the variant number for the GlanceFx line.
     * @note    Only for kCubeBlend, and only when !isBusy(). The steps then
     *          follow GlanceOrbitFx (followStep()), not tick().
     */
    void startBlend(int8_t direction, uint8_t frames, bool settleInPlace, uint8_t logAs);

    /// Take GlanceOrbitFx's step (0 ends). kCubeBlend only.
    void followStep(uint8_t step);

    /// End at once; the next frame draws the item under it clean.
    void cancel();

    /// Once per frame, from the view's tick.
    void tick();

    virtual void draw(const touchgfx::Rect& invalidatedArea) const override;
    virtual touchgfx::Rect getSolidRect() const override;

private:
    // Pixel break-up
    static constexpr int16_t kBlock = 6;
    // Cube build
    static constexpr int16_t kCell = 10;
    static constexpr int16_t kCube = 8;

    static constexpr int16_t kCellCols = kWidth / kCell;
    static constexpr int16_t kCellRows = kHeight / kCell;

    Variant mVariant = kNone;
    uint8_t mStep = 0;          ///< 0 idle, 1..mFrames the frame to draw
    uint8_t mFrames = kFxFrames;
    int8_t mDirection = 1;
    bool mSettleInPlace = false;    ///< see startBlend()
    uint8_t mLogAs = 0;             ///< see startBlend(); 0: mVariant

    /// Variant 5: each 10 px cell's colour (0 for no ink), of the outgoing
    /// content (from mClean, at the press) and of the incoming one (read in
    /// place on the first frame, when the wheel has just drawn it).
    uint8_t mOldCells[kCellRows][kCellCols] = {};
    mutable uint8_t mNewCells[kCellRows][kCellCols] = {};

    /// The step last drawn, so tick() advances only once a step has been
    /// on screen, whichever order TouchGFX takes keys and ticks in.
    mutable uint8_t mDrawnStep = 0;
    /// The window has not been drawn clean in full since the last effect (or
    /// since the variant changed): the screen may show effect pixels, and
    /// mClean may not be what is on screen.
    mutable bool mAwaitingClean = true;
    /// What clean draws have covered of the window since then.
    mutable touchgfx::Rect mCleanCover;

    /// The window as last drawn clean, kWidth x kHeight, refreshed by every
    /// clean draw: the outgoing item of the next press. Pixel break-up and
    /// dithered wipe only; the cube build needs only the new item.
    mutable std::vector<uint8_t> mClean;

    /// Draw time of each step, and of the last refresh of mClean, for the
    /// GlanceFx line.
    mutable uint32_t mStepUs[kMaxFxFrames + 1] = {};
    mutable uint32_t mCopyUs = 0;

    void drawPixelBreak(uint8_t* fb, int32_t stride, const touchgfx::Rect& clip) const;
    void drawDitherWipe(uint8_t* fb, int32_t stride, const touchgfx::Rect& clip) const;
    void drawCubeBuild(uint8_t* fb, int32_t stride, const touchgfx::Rect& clip) const;
    void drawCubeBlend(uint8_t* fb, int32_t stride, const touchgfx::Rect& clip) const;

    /// Every cell's colour, 0 where it has too little ink to be a cube.
    static void readCells(const uint8_t* origin, int32_t stride, const touchgfx::Rect& clip,
                          uint8_t cells[kCellRows][kCellCols]);
    /// The cubes of @p cells, each moved @p dy (alternate cells 3 or 6 px
    /// times @p dy's sign when @p stagger), only inside @p clip.
    static void paintCubes(uint8_t* origin, int32_t stride, const touchgfx::Rect& clip,
                           const uint8_t cells[kCellRows][kCellCols], int dySign, bool stagger);

    void logCost() const;
};

#endif // GLANCESCROLLFX_HPP
