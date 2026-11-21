// Before every include, so the logger takes this name.
#define LOG_MODULE_PRX      "GUI.GlanceFx"

#include <gui/containers/GlanceScrollFx.hpp>
#include "gui/common/GuiLogger.hpp"

#include <touchgfx/hal/HAL.hpp>
#include <touchgfx/lcd/LCD.hpp>

#include <algorithm>

#if defined(SIMULATOR)
#include <chrono>
#else
extern "C" uint32_t SystemCoreClock;
#endif

using touchgfx::HAL;
using touchgfx::Rect;

namespace {

// ABGR2222: alpha in the top two bits, then blue, green, red.
constexpr uint8_t kAlpha = 0xC0;
constexpr uint8_t kBlack = kAlpha;

inline bool hasInk(uint8_t p)
{
    return (p & 0x3F) != 0;
}

inline int luma(uint8_t p)
{
    return 2 * (p & 3) + 5 * ((p >> 2) & 3) + ((p >> 4) & 3);
}

// Every colour channel one step up (d > 0) or down, alpha kept opaque: the
// shading stays exactly within the 64 colours.
uint8_t shade(uint8_t p, int d)
{
    const int r = std::clamp((p & 3) + d, 0, 3);
    const int g = std::clamp(((p >> 2) & 3) + d, 0, 3);
    const int b = std::clamp(((p >> 4) & 3) + d, 0, 3);
    return static_cast<uint8_t>(kAlpha | (b << 4) | (g << 2) | r);
}

// 4 x 4 ordered (Bayer) dither thresholds, 0..15.
const uint8_t kBayer[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 },
};

// An 8 x 8 isometric cube: T top face, L left face, R right face.
const char* const kCubeSprite[8] = {
    "...TT...",
    ".TTTTTT.",
    "TTTTTTTT",
    "LLTTTTRR",
    "LLLLRRRR",
    "LLLLRRRR",
    ".LLLRRR.",
    "...LR...",
};

// The strongest ink in an area of the framebuffer, and how many pixels have
// any: a block or a cell takes the colour of its brightest pixel, so thin
// strokes of text survive being made coarse.
struct Ink {
    uint8_t colour = kBlack;
    int count = 0;
};

Ink readInk(const uint8_t* origin, int32_t stride, const Rect& r)
{
    Ink ink;
    int best = -1;
    for (int16_t y = r.y; y < r.bottom(); ++y) {
        const uint8_t* row = origin + y * stride;
        for (int16_t x = r.x; x < r.right(); ++x) {
            const uint8_t p = row[x];
            if (!hasInk(p)) {
                continue;
            }
            ink.count++;
            const int l = luma(p);
            if (l > best) {
                best = l;
                ink.colour = p;
            }
        }
    }
    return ink;
}

void fill(uint8_t* origin, int32_t stride, const Rect& r, uint8_t colour)
{
    for (int16_t y = r.y; y < r.bottom(); ++y) {
        std::fill_n(origin + y * stride + r.x, r.width, colour);
    }
}

} // namespace

// Cycle counter on the watch (already running: the eMMC driver starts it),
// steady clock in the simulator.
uint32_t GlanceScrollFx::ticksNow()
{
#if defined(SIMULATOR)
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
#else
    return *reinterpret_cast<volatile uint32_t*>(0xE0001004);  // DWT->CYCCNT
#endif
}

uint32_t GlanceScrollFx::ticksToUs(uint32_t ticks)
{
#if defined(SIMULATOR)
    return ticks;
#else
    const uint32_t perUs = SystemCoreClock / 1000000u;
    return perUs ? ticks / perUs : 0;
#endif
}

// ABGR2222: kAlpha | blue << 4 | green << 2 | red, every channel 1 or 2.
const uint8_t GlanceScrollFx::kCubePalette[kCubePaletteSize] = {
    kAlpha | (1 << 4) | (1 << 2) | 2,   // red      R2 G1 B1
    kAlpha | (1 << 4) | (2 << 2) | 1,   // green    R1 G2 B1
    kAlpha | (2 << 4) | (1 << 2) | 1,   // blue     R1 G1 B2
    kAlpha | (1 << 4) | (2 << 2) | 2,   // yellow   R2 G2 B1
    kAlpha | (2 << 4) | (1 << 2) | 2,   // magenta  R2 G1 B2
    kAlpha | (2 << 4) | (2 << 2) | 1,   // cyan     R1 G2 B2
};

uint32_t GlanceScrollFx::sRng = 0;
int GlanceScrollFx::sLastColour = -1;

void GlanceScrollFx::seedCubeColours(uint32_t seed)
{
    if (sRng != 0) {
        return;     // once per boot
    }
    sRng = seed ? seed : 0x9E3779B9u;   // xorshift32 must not start at 0
}

uint8_t GlanceScrollFx::nextCubeColour()
{
    if (sRng == 0) {
        seedCubeColours(ticksNow());
    }
    // xorshift32 (Marsaglia).
    sRng ^= sRng << 13;
    sRng ^= sRng >> 17;
    sRng ^= sRng << 5;
    // Any colour the first time; after that one of the five others, so two
    // scrolls in a row never share a colour.
    const int pick = (sLastColour < 0)
            ? static_cast<int>(sRng % kCubePaletteSize)
            : (sLastColour + 1 + static_cast<int>(sRng % (kCubePaletteSize - 1))) % kCubePaletteSize;
    sLastColour = pick;
    return kCubePalette[pick];
}

GlanceScrollFx::GlanceScrollFx()
{
    setTouchable(false);
}

const char* GlanceScrollFx::variantName(Variant v)
{
    switch (v) {
        case kNone:       return "wheel (today)";
        case kPixelBreak: return "pixel break-up";
        case kDitherWipe: return "dithered wipe";
        case kCubeBuild:  return "cube build";
        case kCubeBlend:  return "centre cubes";
        default:          return "?";
    }
}

void GlanceScrollFx::setVariant(Variant v)
{
    mStep = 0;
    mVariant = (v < kVariantCount) ? v : kNone;

    // Kept for the life of the screen, so a press never allocates.
    if (mVariant == kPixelBreak || mVariant == kDitherWipe || mVariant == kCubeBlend) {
        mClean.assign(static_cast<size_t>(kWidth) * kHeight, kBlack);
    } else {
        std::vector<uint8_t>().swap(mClean);
    }

    // The copy is empty until the window is next drawn clean in full; until
    // then a press gets no effect.
    mAwaitingClean = true;
    mCleanCover = Rect();
    invalidate();
}

void GlanceScrollFx::start(int8_t direction)
{
    if (mVariant == kNone || mVariant == kCubeBlend || isBusy()) {
        return;
    }
    mDirection = (direction < 0) ? -1 : 1;
    mFrames = kFxFrames;
    mStep = 1;
    mDrawnStep = 0;
    std::fill(std::begin(mStepUs), std::end(mStepUs), 0u);
    invalidate();
}

void GlanceScrollFx::startBlend(int8_t direction, uint8_t frames, bool settleInPlace, uint8_t logAs)
{
    if (mVariant != kCubeBlend || isBusy() || frames == 0) {
        return;
    }
    mDirection = (direction < 0) ? -1 : 1;
    mFrames = (frames > kMaxFxFrames) ? kMaxFxFrames : frames;
    mSettleInPlace = settleInPlace;
    mLogAs = logAs;
    mCubeColour = 0;
    mStep = 1;
    mDrawnStep = 0;
    std::fill(std::begin(mStepUs), std::end(mStepUs), 0u);

    // The outgoing content, from the clean copy: complete, since nothing
    // starts while isBusy().
    readCells(mClean.data(), kWidth, Rect(0, 0, kWidth, kHeight), mOldCells);
    invalidate();
}

void GlanceScrollFx::followStep(uint8_t step)
{
    if (mVariant != kCubeBlend || step == mStep) {
        return;
    }
    if (step == 0 || step > mFrames) {
        if (mStep != 0) {
            mStep = 0;
            logCost();
            invalidate();   // the clean frame
        }
        return;
    }
    mStep = step;
    invalidate();
}

void GlanceScrollFx::cancel()
{
    if (mStep != 0) {
        LOG_DEBUG("GlanceFx: variant %u cut: press during a transition, next item drawn clean\n",
                  static_cast<unsigned>(mVariant));
    }
    if (mStep != 0 || mAwaitingClean) {
        mStep = 0;
        invalidate();
    }
}

void GlanceScrollFx::tick()
{
    // Only once the current step has been on screen.
    if (mStep == 0 || mDrawnStep != mStep) {
        return;
    }
    if (mStep < mFrames) {
        mStep++;
        invalidate();
        return;
    }
    mStep = 0;
    logCost();
    invalidate();   // the clean frame
}

void GlanceScrollFx::logCost() const
{
    // Numbered as Gui::Config::kGlancesScrollFx numbers them: the cubes under
    // the names are variant 5's.
    const unsigned shown = (mVariant == kCubeBlend) ? (mLogAs ? mLogAs : 5u)
                                                    : static_cast<unsigned>(mVariant);
    if (mCubeColour != 0) {
        LOG_DEBUG("GlanceFx: variant %u (%s): cube colour 0x%02X, frame 1 drew in %lu us, "
                  "frame 2 in %lu us, frame 3 in %lu us, last clean copy %lu us\n",
                  shown, variantName(mVariant), static_cast<unsigned>(mCubeColour),
                  static_cast<unsigned long>(mStepUs[1]),
                  static_cast<unsigned long>(mStepUs[2]),
                  static_cast<unsigned long>(mStepUs[3]),
                  static_cast<unsigned long>(mCopyUs));
    } else if (mFrames >= 3) {
        LOG_DEBUG("GlanceFx: variant %u (%s): frame 1 drew in %lu us, frame 2 in %lu us, "
                  "frame 3 in %lu us, last clean copy %lu us\n",
                  shown, variantName(mVariant),
                  static_cast<unsigned long>(mStepUs[1]),
                  static_cast<unsigned long>(mStepUs[2]),
                  static_cast<unsigned long>(mStepUs[3]),
                  static_cast<unsigned long>(mCopyUs));
    } else {
        LOG_DEBUG("GlanceFx: variant %u (%s): frame 1 drew in %lu us, frame 2 in %lu us, "
                  "last clean copy %lu us\n",
                  shown, variantName(mVariant),
                  static_cast<unsigned long>(mStepUs[1]),
                  static_cast<unsigned long>(mStepUs[2]),
                  static_cast<unsigned long>(mCopyUs));
    }
}

Rect GlanceScrollFx::getSolidRect() const
{
    // Never solid: what is under it is drawn first, and read back by some
    // variants.
    return Rect();
}

void GlanceScrollFx::draw(const Rect& invalidatedArea) const
{
    // Only where TouchGFX asked, and only inside the centre window.
    const Rect clip = invalidatedArea & Rect(0, 0, getWidth(), getHeight());
    if (clip.isEmpty()) {
        return;
    }

    if (mVariant == kNone) {
        return;
    }

    // Byte-per-pixel framebuffer, not rotated: anything else is drawn as the
    // plain wheel would be, with no effect.
    if (HAL::DISPLAY_ROTATION != touchgfx::rotate0 || HAL::lcd().bitDepth() != 8) {
        return;
    }

    const uint32_t t0 = ticksNow();
    uint8_t* fb = reinterpret_cast<uint8_t*>(HAL::getInstance()->lockFrameBuffer());
    const int32_t stride = HAL::lcd().framebufferStride();
    const Rect me = getAbsoluteRect();
    uint8_t* origin = fb + me.y * stride + me.x;

    if (mStep == 0) {
        // A clean draw: what is under this is the item as it is now. Kept as
        // the outgoing item of the next press.
        if (!mClean.empty()) {
            for (int16_t y = clip.y; y < clip.bottom(); ++y) {
                std::copy_n(origin + y * stride + clip.x, clip.width,
                            mClean.data() + y * kWidth + clip.x);
            }
        }
        HAL::getInstance()->unlockFrameBuffer();
        mCopyUs = ticksToUs(ticksNow() - t0);

        // Once clean draws have covered the whole window, nothing of an
        // effect is left on screen and the copy is complete.
        if (mAwaitingClean) {
            mCleanCover.expandToFit(clip);
            if (mCleanCover.width >= getWidth() && mCleanCover.height >= getHeight()) {
                mAwaitingClean = false;
            }
        }
        return;
    }

    switch (mVariant) {
        case kPixelBreak: drawPixelBreak(origin, stride, clip); break;
        case kDitherWipe: drawDitherWipe(origin, stride, clip); break;
        case kCubeBuild:  drawCubeBuild(origin, stride, clip); break;
        case kCubeBlend:  drawCubeBlend(origin, stride, clip); break;
        default: break;
    }

    HAL::getInstance()->unlockFrameBuffer();
    mStepUs[mStep] += ticksToUs(ticksNow() - t0);

    mDrawnStep = mStep;
    mAwaitingClean = true;
    mCleanCover = Rect();
}

// Frame 1: the old item as half its blocks (a checkerboard). Frame 2: the new
// item as the other half. No frame shows the two items together.
void GlanceScrollFx::drawPixelBreak(uint8_t* origin, int32_t stride, const Rect& clip) const
{
    const int16_t cols = (kWidth + kBlock - 1) / kBlock;
    const int parity = (mStep == 1) ? 0 : 1;
    for (int16_t by = 0; by * kBlock < kHeight; ++by) {
        for (int16_t bx = 0; bx < cols; ++bx) {
            const Rect b = Rect(bx * kBlock, by * kBlock, kBlock, kBlock) & clip;
            if (b.isEmpty()) {
                continue;
            }
            uint8_t colour = kBlack;
            if (((bx + by) & 1) == parity) {
                // The old item from the clean copy; the new one was drawn
                // under this a moment ago, so its block is read in place,
                // before it is written. Blocks do not overlap.
                if (mStep == 1) {
                    const Rect whole = Rect(bx * kBlock, by * kBlock, kBlock, kBlock) &
                                       Rect(0, 0, kWidth, kHeight);
                    colour = readInk(mClean.data(), kWidth, whole).colour;
                } else {
                    colour = readInk(origin, stride, b).colour;
                }
            }
            fill(origin, stride, b, colour);
        }
    }
}

// The edge travels with the scroll: a third of the way in on frame 1, two
// thirds on frame 2. Across a 24-row band either side of it, each 2 x 2 cell
// shows the new item once the ramp passes its dither threshold.
void GlanceScrollFx::drawDitherWipe(uint8_t* origin, int32_t stride, const Rect& clip) const
{
    if (mClean.size() < static_cast<size_t>(kWidth) * kHeight) {
        return;
    }
    constexpr int kHalfBand = 12;
    const int edge = kHeight - (mStep * kHeight) / (kFxFrames + 1);

    for (int16_t y = clip.y; y < clip.bottom(); ++y) {
        // Rows counted from where the new item comes in.
        const int yy = (mDirection > 0) ? y : (kHeight - 1 - y);
        const int fromBandTop = yy - (edge - kHalfBand);
        if (fromBandTop >= 2 * kHalfBand) {
            continue;   // all new: already drawn under this
        }
        const uint8_t* oldRow = mClean.data() + y * kWidth;
        uint8_t* row = origin + y * stride;
        if (fromBandTop < 0) {
            std::copy(oldRow + clip.x, oldRow + clip.right(), row + clip.x);
            continue;
        }
        const int ramp = (fromBandTop * 16) / (2 * kHalfBand);   // 0..15
        const uint8_t* bayer = kBayer[(yy / 2) & 3];
        for (int16_t x = clip.x; x < clip.right(); ++x) {
            if (bayer[(x / 2) & 3] >= ramp) {
                row[x] = oldRow[x];
            }
        }
    }
}

// Every 10 px cell of the new item with ink in it becomes a cube in its
// colour. Frame 1: cubes still 3 or 6 px short of their place, coming from the
// direction of travel. Frame 2: in place. Then the crisp item. The old item is
// gone from the first frame, so no frame shows the two together.
void GlanceScrollFx::drawCubeBuild(uint8_t* origin, int32_t stride, const Rect& clip) const
{
    // Read in place, before anything is written: the new item was drawn
    // under this a moment ago.
    uint8_t cells[kCellRows][kCellCols] = {};
    readCells(origin, stride, clip, cells);
    fill(origin, stride, clip, kBlack);
    paintCubes(origin, stride, clip, cells, (mStep == 1) ? mDirection : 0, true);
}

// Variant 5, under GlanceOrbitFx's names. Frame 1: the outgoing content as
// cubes lifting away with the scroll. The last in-between frame: the incoming
// content landing, 3 or 6 px short of its place. Any frame between: clear.
// The incoming content is read on frame 1, when the wheel has just drawn it
// under this; GlanceOrbitFx keeps the centre box off for this variant so it is
// drawn at all.
void GlanceScrollFx::drawCubeBlend(uint8_t* origin, int32_t stride, const Rect& clip) const
{
    if (mStep == 1) {
        readCells(origin, stride, clip, mNewCells);
    }
    fill(origin, stride, clip, kBlack);
    if (mStep == 1) {
        paintCubes(origin, stride, clip, mOldCells, -mDirection, true, mCubeColour);
    } else if (mSettleInPlace && mFrames >= 3) {
        // Variant 6: short of their places, then in place, as variant 3's
        // cubes arrive.
        if (mStep == mFrames - 1) {
            paintCubes(origin, stride, clip, mNewCells, mDirection, true, mCubeColour);
        } else if (mStep == mFrames) {
            paintCubes(origin, stride, clip, mNewCells, 0, true, mCubeColour);
        }
    } else if (mStep == mFrames) {
        paintCubes(origin, stride, clip, mNewCells, mDirection, true, mCubeColour);
    }
}

// Cells only where they meet @p clip, so a draw split in parts never reads
// outside what TouchGFX has just drawn.
void GlanceScrollFx::readCells(const uint8_t* origin, int32_t stride, const Rect& clip,
                               uint8_t cells[kCellRows][kCellCols])
{
    constexpr int16_t x0 = (kWidth - kCellCols * kCell) / 2;
    constexpr int kMinInk = 4;
    for (int16_t cy = 0; cy < kCellRows; ++cy) {
        for (int16_t cx = 0; cx < kCellCols; ++cx) {
            const Rect c = Rect(x0 + cx * kCell, cy * kCell, kCell, kCell) & clip;
            if (c.isEmpty()) {
                continue;
            }
            const Ink ink = readInk(origin, stride, c);
            cells[cy][cx] = (ink.count >= kMinInk) ? ink.colour : 0;
        }
    }
}

void GlanceScrollFx::paintCubes(uint8_t* origin, int32_t stride, const Rect& clip,
                                const uint8_t cells[kCellRows][kCellCols], int dySign, bool stagger,
                                uint8_t colour)
{
    constexpr int16_t x0 = (kWidth - kCellCols * kCell) / 2;
    const int16_t inset = (kCell - kCube) / 2;
    for (int16_t cy = 0; cy < kCellRows; ++cy) {
        for (int16_t cx = 0; cx < kCellCols; ++cx) {
            // A cell with ink is a cube: in its own colour, or in the
            // scroll's colour when one is given.
            if (cells[cy][cx] == 0) {
                continue;
            }
            const uint8_t c = colour ? colour : cells[cy][cx];
            const int16_t dy = static_cast<int16_t>(dySign * ((stagger && ((cx + cy) & 1)) ? 6 : 3));
            // Bright colours keep their colour on top and darken down the
            // sides; dim ones lighten on top, so a cube never sinks to black.
            const bool bright = (c & 3) == 3 || ((c >> 2) & 3) == 3 || ((c >> 4) & 3) == 3;
            const uint8_t top   = bright ? c : shade(c, 1);
            const uint8_t left  = bright ? shade(c, -1) : c;
            const uint8_t right = bright ? shade(c, -2) : shade(c, -1);

            const int16_t sx = x0 + cx * kCell + inset;
            const int16_t sy = cy * kCell + inset + dy;
            for (int16_t py = 0; py < kCube; ++py) {
                const int16_t y = sy + py;
                if (y < clip.y || y >= clip.bottom()) {
                    continue;
                }
                uint8_t* row = origin + y * stride;
                for (int16_t px = 0; px < kCube; ++px) {
                    const int16_t x = sx + px;
                    if (x < clip.x || x >= clip.right()) {
                        continue;
                    }
                    switch (kCubeSprite[py][px]) {
                        case 'T': row[x] = top; break;
                        case 'L': row[x] = left; break;
                        case 'R': row[x] = right; break;
                        default: break;
                    }
                }
            }
        }
    }
}
