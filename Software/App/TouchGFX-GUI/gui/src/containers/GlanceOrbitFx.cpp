// Before every include, so the logger takes this name.
#define LOG_MODULE_PRX      "GUI.GlanceFx"

#include <gui/containers/GlanceOrbitFx.hpp>
#include <gui/containers/GlanceScrollFx.hpp>
#include <gui/containers/OrbitTiers.hpp>
#include "gui/common/GuiLogger.hpp"

#include <touchgfx/Color.hpp>
#include <touchgfx/TypedText.hpp>

#include <cmath>
#include <cstring>

using touchgfx::Rect;

GlanceOrbitFx::GlanceOrbitFx()
    : mStart(*this, false)
    , mEnd(*this, true)
{
    setTouchable(false);
}

void GlanceOrbitFx::initialize()
{
    // Three regions, in screen pixels. They stop short of the separator
    // lines (y 79 to 81 and 159 to 161), which stay visible and which no
    // name is drawn over. The bands start at x 37: at those heights the side
    // bar's rail curves in as far as x 34 and its handle's tip to x 35. A
    // centred name up to 166 px wide still fits (the longest installed one is
    // about 130 px at this size). Beside the centre item the side bar stays
    // left of x 22. The wheel's area ends at y 198.
    mRegions[kTop].screen    = Rect(37, kY, kX + kWidth - 37, 79 - kY);
    mRegions[kCentre].screen = Rect(kX, 82, kWidth, 159 - 82);
    mRegions[kBottom].screen = Rect(37, 162, kX + kWidth - 37, kY + kHeight - 162);

    const touchgfx::colortype black = touchgfx::Color::getColorFromRGB(0, 0, 0);
    mStart.setPosition(0, 0, kWidth, kHeight);
    add(mStart);
    for (Region& r : mRegions) {
        r.clip.setPosition(r.screen.x - kX, r.screen.y - kY, r.screen.width, r.screen.height);
        r.background.setPosition(0, 0, r.screen.width, r.screen.height);
        r.background.setColor(black);
        r.clip.add(r.background);
        for (int i = 0; i < kRows; ++i) {
            r.rows[i].setVisible(false);
            r.clip.add(r.rows[i]);
        }
        add(r.clip);
    }
    mEnd.setPosition(0, 0, kWidth, kHeight);
    add(mEnd);

    setMode(Mode::kNamesOverBox);
    setVisible(false);
}

void GlanceOrbitFx::setMode(Mode mode)
{
    mMode = mode;
    // The centre's black box hides the wheel's new content; variants 5 and
    // 6 draw cubes there instead, under this container.
    mRegions[kCentre].background.setVisible(mode == Mode::kNamesOverBox);
}

void GlanceOrbitFx::setSteps(uint8_t steps)
{
    mSteps = (steps < 2) ? 2 : (steps > kMaxSteps ? kMaxSteps : steps);
}

void GlanceOrbitFx::setName(int8_t offset, const touchgfx::Unicode::UnicodeChar* name)
{
    if (offset < -kReach || offset > kReach) {
        return;
    }
    static const touchgfx::Unicode::UnicodeChar kEmpty[1] = { 0 };
    touchgfx::Unicode::strncpy(mNames[offset + kReach], name ? name : kEmpty, kNameLen - 1);
    mNames[offset + kReach][kNameLen - 1] = 0;
}

void GlanceOrbitFx::setName(int8_t offset, const char* utf8)
{
    if (offset < -kReach || offset > kReach) {
        return;
    }
    touchgfx::Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(utf8 ? utf8 : ""),
                                mNames[offset + kReach], kNameLen);
}

void GlanceOrbitFx::start(int8_t direction)
{
    mDirection = (direction < 0) ? -1 : 1;
    mStep = 1;
    mDrawnStep = 0;
    std::memset(mStepUs, 0, sizeof(mStepUs));
    setVisible(true);
    layout();
    invalidate();
}

void GlanceOrbitFx::cancel()
{
    if (mStep == 0) {
        return;
    }
    LOG_DEBUG("GlanceFx: orbit cut: press during a transition, next item drawn clean\n");
    mStep = 0;
    invalidate();       // before hiding: a hidden drawable invalidates nothing
    setVisible(false);
}

void GlanceOrbitFx::tick()
{
    // Only once the current step has been on screen.
    if (mStep == 0 || mDrawnStep != mStep) {
        return;
    }
    if (mStep + 1 < mSteps) {
        mStep++;
        layout();
        invalidate();
        return;
    }
    // The settled frame is the wheel's own.
    mStep = 0;
    logCost();
    invalidate();
    setVisible(false);
}

// OrbitMenu::layout(), for names only: every row placed whole from one
// fractional position, its font and colour from its distance to the centre.
void GlanceOrbitFx::layout()
{
    const float scroll = mDirection * static_cast<float>(mStep) / static_cast<float>(mSteps);
    int used[kRegions] = {};
    for (int8_t k = -kReach; k <= kReach; ++k) {
        const float d = static_cast<float>(k) - scroll;
        const float ad = std::fabs(d);
        if (ad >= 2.0f) {
            continue;   // past the wheel's area
        }
        const OrbitTiers::LabelTier& tier = OrbitTiers::pickTier(ad);

        // Centred on the row exactly as MenuGlancesItem::setOrbitStyle()
        // centres a resting name, so the hand-over at d = +/-1 is seamless.
        mMeasure.setWildcard(mNames[k + kReach]);
        mMeasure.setTypedText(touchgfx::TypedText(tier.fontCentred));
        mMeasure.setPosition(0, 0, kNameW, kRowPitch);
        mMeasure.resizeHeightToCurrentText();
        const int16_t h = mMeasure.getHeight();
        const int16_t rowTop = static_cast<int16_t>(std::lround(kCentreRowTop + d * kRowPitch));
        const Rect text(kNameX, static_cast<int16_t>(rowTop + (kRowPitch - h) / 2), kNameW, h);

        // The name in every region it reaches, each copy clipped by its
        // region: cut at a separator line, never drawn over it.
        for (int r = 0; r < kRegions; ++r) {
            if (r == kCentre && mMode == Mode::kNamesInBands) {
                continue;
            }
            Region& region = mRegions[r];
            if (!text.intersect(region.screen) || used[r] >= kRows) {
                continue;
            }
            touchgfx::TextAreaWithOneWildcard& row = region.rows[used[r]++];
            row.setWildcard(mNames[k + kReach]);
            row.setTypedText(touchgfx::TypedText(tier.fontCentred));
            row.setColor(tier.color);
            row.setPosition(text.x - region.screen.x, text.y - region.screen.y, text.width, text.height);
            row.setVisible(true);
        }
    }
    for (int r = 0; r < kRegions; ++r) {
        for (int i = used[r]; i < kRows; ++i) {
            mRegions[r].rows[i].setVisible(false);
        }
    }
}

void GlanceOrbitFx::Mark::draw(const Rect&) const
{
    if (!mEnd) {
        mOwner.mMarkTicks = GlanceScrollFx::ticksNow();
        return;
    }
    if (mOwner.mStep == 0) {
        return;
    }
    mOwner.mStepUs[mOwner.mStep] += GlanceScrollFx::ticksToUs(GlanceScrollFx::ticksNow() - mOwner.mMarkTicks);
    mOwner.mDrawnStep = mOwner.mStep;
}

void GlanceOrbitFx::logCost() const
{
    if (mSteps >= 4) {
        LOG_DEBUG("GlanceFx: orbit, %u steps: frame 1 drew in %lu us, frame 2 in %lu us, frame 3 in %lu us\n",
                  static_cast<unsigned>(mSteps), static_cast<unsigned long>(mStepUs[1]),
                  static_cast<unsigned long>(mStepUs[2]), static_cast<unsigned long>(mStepUs[3]));
    } else {
        LOG_DEBUG("GlanceFx: orbit, %u steps: frame 1 drew in %lu us, frame 2 in %lu us\n",
                  static_cast<unsigned>(mSteps), static_cast<unsigned long>(mStepUs[1]),
                  static_cast<unsigned long>(mStepUs[2]));
    }
}
