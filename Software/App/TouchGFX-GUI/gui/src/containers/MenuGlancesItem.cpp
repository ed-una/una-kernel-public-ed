#include <gui/containers/MenuGlancesItem.hpp>
#include <gui/containers/GlanceOrbitFx.hpp>
#include <gui/containers/OrbitTiers.hpp>
#include <touchgfx/Color.hpp>

MenuGlancesItem::MenuGlancesItem()
{

}

void MenuGlancesItem::initialize()
{
    MenuGlancesItemBase::initialize();
}

void MenuGlancesItem::setName(TypedTextId id)
{
    Unicode::snprintf(textBuffer, TEXT_SIZE, "%s", touchgfx::TypedText(id).getText());
    text.invalidate();
}

void MenuGlancesItem::setOrbitStyle(bool orbit)
{
    text.invalidate();
    if (orbit) {
        const OrbitTiers::LabelTier &tier = OrbitTiers::pickTier(1.0f);
        text.setTypedText(touchgfx::TypedText(tier.fontCentred));
        text.setColor(tier.color);
        text.setPosition(GlanceOrbitFx::kNameX, 0, GlanceOrbitFx::kNameW, getHeight());
        text.resizeHeightToCurrentText();
        text.setY(static_cast<int16_t>((getHeight() - text.getHeight()) / 2));
    } else {
        // As MenuGlancesItemBase lays it out.
        text.setTypedText(touchgfx::TypedText(T_TMP_REGULAR_18));
        text.setColor(touchgfx::Color::getColorFromRGB(192, 192, 192));
        text.setPosition(35, 19, 170, 23);
    }
    text.invalidate();
}

void MenuGlancesItem::setName(const char* name)
{
    Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(name), textBuffer, TEXT_SIZE);
    text.invalidate();
}