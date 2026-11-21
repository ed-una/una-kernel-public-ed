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

// Cycle counter on the watch (already running: the eMMC driver starts it),
// steady clock in the simulator.
uint32_t ticksNow()
{
#if defined(SIMULATOR)
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
#else
    return *reinterpret_cast<volatile uint32_t*>(0xE0001004);  // DWT->CYCCNT
#endif
}

uint32_t ticksToUs(uint32_t ticks)
{
#if defined(SIMULATOR)
    return ticks;
#else
    const uint32_t perUs = SystemCoreClock / 1000000u;
    return perUs ? ticks / perUs : 0;
#endif
}

} // namespace

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
        default:          return "?";
    }
}

void GlanceScrollFx::setVariant(Variant v)
{
    mStep = 0;
    mVariant = (v < kVariantCount) ? v : kNone;

    // Kept for the life of the screen, so a press never allocates.
    if (mVariant == kPixelBreak || mVariant == kDitherWipe) {
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
    if (mVariant == kNone || isBusy()) {
        return;
    }
    mDirection = (direction < 0) ? -1 : 1;
    mStep = 1;
    mDrawnStep = 0;
    std::fill(std::begin(mStepUs), std::end(mStepUs), 0u);
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
    if (mStep < kFxFrames) {
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
    LOG_DEBUG("GlanceFx: variant %u (%s): frame 1 drew in %lu us, frame 2 in %lu us, "
              "last clean copy %lu us\n",
              static_cast<unsigned>(mVariant), variantName(mVariant),
              static_cast<unsigned long>(mStepUs[1]),
              static_cast<unsigned long>(mStepUs[2]),
              static_cast<unsigned long>(mCopyUs));
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
    constexpr int16_t cols = kWidth / kCell;
    constexpr int16_t rows = kHeight / kCell;
    constexpr int16_t x0 = (kWidth - cols * kCell) / 2;
    constexpr int kMinInk = 4;

    // Read in place, before anything is written: the new item was drawn
    // under this a moment ago.
    uint8_t colour[rows][cols];
    for (int16_t cy = 0; cy < rows; ++cy) {
        for (int16_t cx = 0; cx < cols; ++cx) {
            const Rect c = Rect(x0 + cx * kCell, cy * kCell, kCell, kCell) & clip;
            const Ink ink = c.isEmpty() ? Ink() : readInk(origin, stride, c);
            colour[cy][cx] = (ink.count >= kMinInk) ? ink.colour : 0;
        }
    }

    fill(origin, stride, clip, kBlack);

    const int16_t inset = (kCell - kCube) / 2;
    for (int16_t cy = 0; cy < rows; ++cy) {
        for (int16_t cx = 0; cx < cols; ++cx) {
            const uint8_t c = colour[cy][cx];
            if (c == 0) {
                continue;
            }
            int16_t dy = 0;
            if (mStep == 1) {
                dy = static_cast<int16_t>(mDirection * (((cx + cy) & 1) ? 6 : 3));
            }
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
