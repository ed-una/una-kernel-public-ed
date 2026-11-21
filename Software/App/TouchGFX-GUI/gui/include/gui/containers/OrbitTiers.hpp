#ifndef ORBITTIERS_HPP
#define ORBITTIERS_HPP

#include <touchgfx/TypedText.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <SDK/GUI/Color.hpp>

#include <cstdint>

/**
 * @brief Label font and colour by distance from the centre, for menus that
 *        move like the Track menu (OrbitMenu) and the Glances scroll that
 *        copies it (GlanceOrbitFx).
 *
 * Depth is conveyed by discrete palette colours only (no alpha) -- the panel
 * is 64-colour, so an alpha gradient would land off-palette and band.
 * Rest positions land on 30 / 22 / 18; the intermediate sizes (27, 25, 20)
 * cover the in-between animation frames.
 */
namespace OrbitTiers
{

struct LabelTier
{
    float       maxAbsD;     ///< Applies while |d| < this.
    touchgfx::TypedTextId font;        ///< Left-aligned wildcard typed text (OrbitMenu).
    touchgfx::TypedTextId fontCentred; ///< The same font and size, centred (Glances names).
    uint32_t    color;       ///< Exact palette entry.
};

inline const LabelTier kTiers[] = {
    { 0.25f,  T_TMP_SEMIBOLD_26_L, T_TMP_SEMIBOLD_26, SDK::GUI::Color::WHITE },
    { 0.60f,  T_TMP_SEMIBOLD_22_L, T_TMP_SEMIBOLD_22, SDK::GUI::Color::WHITE },
    { 0.90f,  T_TMP_SEMIBOLD_20_L, T_TMP_SEMIBOLD_20, SDK::GUI::Color::WHITE },
    { 1.40f,  T_TMP_SEMIBOLD_18_L, T_TMP_SEMIBOLD_18, SDK::GUI::Color::GRAY },
    { 1.90f,  T_TMP_REGULAR_18_L,  T_TMP_REGULAR_18,  SDK::GUI::Color::GRAY }, // currently not in use. Select appropriate fonts
    { 1.0e9f, T_TMP_REGULAR_18_L,  T_TMP_REGULAR_18,  SDK::GUI::Color::GRAY }, // currently not in use. Select appropriate fonts
};

inline const LabelTier &pickTier(float absD)
{
    for (const LabelTier &t : kTiers) {
        if (absD < t.maxAbsD) {
            return t;
        }
    }
    return kTiers[sizeof(kTiers) / sizeof(kTiers[0]) - 1];
}

} // namespace OrbitTiers

#endif // ORBITTIERS_HPP
