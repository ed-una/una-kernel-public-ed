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
    // Everything in this container's coordinates: the wheel's area from
    // (kX, kY). The bands stop short of the separator lines (y 79 to 81 and
    // 159 to 161 on screen), which stay visible under the moving names, and
    // start at x 37: at those heights the side bar's rail curves in as far as
    // x 34 and its handle's tip to x 35. A centred name up to 166 px wide is
    // still covered (the longest installed one is about 130 px at this
    // size). Beside the centre item the side bar stays left of x 22.
    const touchgfx::colortype black = touchgfx::Color::getColorFromRGB(0, 0, 0);
    const int16_t bandX = 37 - kX;
    mStart.setPosition(0, 0, kWidth, kHeight);
    mTopBand.setPosition(bandX, 0, kWidth - bandX, 79 - kY);
    mCentreBox.setPosition(0, 82 - kY, kWidth, 159 - 82);
    mBottomBand.setPosition(bandX, 162 - kY, kWidth - bandX, kHeight - (162 - kY));
    mTopBand.setColor(black);
    mCentreBox.setColor(black);
    mBottomBand.setColor(black);
    mEnd.setPosition(0, 0, kWidth, kHeight);

    add(mStart);
    add(mTopBand);
    add(mCentreBox);
    add(mBottomBand);
    for (int i = 0; i < kRows; ++i) {
        mRows[i].setVisible(false);
        add(mRows[i]);
    }
    add(mEnd);

    setVisible(false);
}

void GlanceOrbitFx::setCentreBox(bool on)
{
    mCentreBox.setVisible(on);
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
    int used = 0;
    for (int8_t k = -kReach; k <= kReach && used < kRows; ++k) {
        const float d = static_cast<float>(k) - scroll;
        const float ad = std::fabs(d);
        if (ad >= 2.0f) {
            continue;   // past the wheel's area
        }
        const OrbitTiers::LabelTier& tier = OrbitTiers::pickTier(ad);
        touchgfx::TextAreaWithOneWildcard& row = mRows[used++];
        row.setWildcard(mNames[k + kReach]);
        row.setTypedText(touchgfx::TypedText(tier.fontCentred));
        row.setColor(tier.color);
        // Centred on the row exactly as MenuGlancesItem::setOrbitStyle()
        // centres a resting name, so the hand-over at d = +/-1 is seamless.
        const int16_t rowTop = static_cast<int16_t>(std::lround(kCentreRowTop + d * kRowPitch));
        row.setPosition(kNameX - kX, 0, kNameW, kRowPitch);
        row.resizeHeightToCurrentText();
        row.setY(static_cast<int16_t>(rowTop + (kRowPitch - row.getHeight()) / 2 - kY));
        row.setVisible(true);
    }
    for (int i = used; i < kRows; ++i) {
        mRows[i].setVisible(false);
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
