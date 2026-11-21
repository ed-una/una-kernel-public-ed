/**
 ******************************************************************************
 * @file    GlanceListOrder.hpp
 * @brief   Which row of the Glances wheel to stay on when the list changes
 ******************************************************************************
 */

#ifndef GLANCE_LIST_ORDER_HPP
#define GLANCE_LIST_ORDER_HPP

#include <cstdint>
#include <vector>

#include "AppTypes.hpp"

namespace App {
namespace GlanceListOrder {

/// The identity of the wheel's first row, Notifications, which is the GUI's
/// own and has no app. No glance app has PID 0.
constexpr uint32_t kNotifications = 0;

/// The wheel row of the item with identity @p id: 0 for Notifications, glance
/// i of @p list at row i + 1. -1 if it is not in the list.
inline int16_t FindRow(const std::vector<GlanceData> &list, uint32_t id)
{
    if (id == kNotifications) {
        return 0;
    }
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].pid == id) {
            return static_cast<int16_t>(i + 1);
        }
    }
    return -1;
}

/**
 * @brief   The row to show after a new list arrives.
 * @details The same item, wherever the new list puts it: matched by app, not
 *          by place, so a glance appearing or going above the selected one
 *          does not move the user onto its neighbour. If the item itself has
 *          gone, the same place, clamped to the rows there now.
 * @param   list: the new list.
 * @param   id: the identity of the item shown until now.
 * @param   oldRow: the row it was on.
 */
inline int16_t KeepRow(const std::vector<GlanceData> &list, uint32_t id, int16_t oldRow)
{
    const int16_t row = FindRow(list, id);
    if (row >= 0) {
        return row;
    }
    const int16_t last = static_cast<int16_t>(list.size());   // rows are 0..size
    if (oldRow < 0) {
        return 0;
    }
    return oldRow > last ? last : oldRow;
}

/// The identity of the item on wheel row @p row of @p list.
inline uint32_t IdAtRow(const std::vector<GlanceData> &list, int16_t row)
{
    if (row <= 0 || static_cast<size_t>(row) > list.size()) {
        return kNotifications;
    }
    return list[static_cast<size_t>(row) - 1].pid;
}

} // namespace GlanceListOrder
} // namespace App

#endif // GLANCE_LIST_ORDER_HPP
