/**
 * Host unit tests for App::GlanceListOrder -- which row of the Glances wheel
 * the user stays on when a new list arrives.
 *
 * The wheel used to be rebuilt by index whenever the number of glances
 * changed, so a glance appearing above the selected one moved the user onto
 * its neighbour, and a remembered position meant "row 3" whatever row 3 now
 * was. These tests pin the rule that replaces it: the same app wherever it
 * lands, and if that app has gone, the same place, clamped to the rows left.
 */

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "Components/Shared/GlanceListOrder.hpp"

namespace {

using App::GlanceData;
namespace Order = App::GlanceListOrder;

std::vector<GlanceData> List(std::initializer_list<uint32_t> pids)
{
    std::vector<GlanceData> list;
    for (uint32_t pid : pids) {
        GlanceData g {};
        g.pid = pid;
        list.push_back(g);
    }
    return list;
}

TEST(GlanceListOrder, NotificationsIsAlwaysTheFirstRow)
{
    EXPECT_EQ(Order::FindRow(List({}), Order::kNotifications), 0);
    EXPECT_EQ(Order::FindRow(List({11, 22}), Order::kNotifications), 0);
}

TEST(GlanceListOrder, GlancesFollowNotifications)
{
    const auto list = List({11, 22, 33});
    EXPECT_EQ(Order::FindRow(list, 11), 1);
    EXPECT_EQ(Order::FindRow(list, 33), 3);
    EXPECT_EQ(Order::FindRow(list, 44), -1);
}

// A glance appears above the selected one: the user stays on their app, one
// row further down, not on whatever moved into their old row.
TEST(GlanceListOrder, KeepsTheAppWhenOneIsAddedAbove)
{
    const auto before = List({11, 33});
    const int16_t row = Order::FindRow(before, 33);
    ASSERT_EQ(row, 2);
    EXPECT_EQ(Order::KeepRow(List({11, 22, 33}), 33, row), 3);
}

TEST(GlanceListOrder, KeepsTheAppWhenOneGoesAbove)
{
    EXPECT_EQ(Order::KeepRow(List({22, 33}), 33, 3), 2);
}

// The selected app itself goes: the same place, clamped to the rows left.
TEST(GlanceListOrder, AnAppThatGoesLeavesTheUserInPlace)
{
    EXPECT_EQ(Order::KeepRow(List({11, 33}), 22, 2), 2);
    EXPECT_EQ(Order::KeepRow(List({11}), 33, 3), 1);
    EXPECT_EQ(Order::KeepRow(List({}), 33, 3), 0);
}

TEST(GlanceListOrder, NotificationsStaysNotifications)
{
    EXPECT_EQ(Order::KeepRow(List({11, 22, 33}), Order::kNotifications, 0), 0);
}

TEST(GlanceListOrder, IdAtRowIsTheInverseOfFindRow)
{
    const auto list = List({11, 22, 33});
    for (int16_t row = 0; row <= 3; ++row) {
        EXPECT_EQ(Order::FindRow(list, Order::IdAtRow(list, row)), row);
    }
    EXPECT_EQ(Order::IdAtRow(list, 9), Order::kNotifications);
    EXPECT_EQ(Order::IdAtRow(List({}), 1), Order::kNotifications);
}

} // namespace
