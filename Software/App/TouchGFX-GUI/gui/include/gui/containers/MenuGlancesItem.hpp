#ifndef MENUGLANCESITEM_HPP
#define MENUGLANCESITEM_HPP

#include <gui_generated/containers/MenuGlancesItemBase.hpp>

class MenuGlancesItem : public MenuGlancesItemBase
{
public:
    MenuGlancesItem();
    virtual ~MenuGlancesItem() {}

    virtual void initialize();

    void setName(TypedTextId id);
    void setName(const char* name);

    /**
     * @brief Draw the name as the Track menu draws a row one step from the
     *        centre (OrbitTiers, distance 1), or as designed.
     * @details The Track-menu Glances scroll (GlanceOrbitFx) hands over to
     *          these rows when it settles, so they must match its last
     *          positions exactly: the same font and colour, centred on the
     *          row the same way.
     */
    void setOrbitStyle(bool orbit);

protected:
};

#endif // MENUGLANCESITEM_HPP
