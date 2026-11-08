// SPDX-License-Identifier: GPL-3.0-or-later
// Benchmark of the display tree edits: move, move to a group, wrap and ungroup on a large list.
// Usage: bench_display_edit [rows]   (default 5000)
#include <lvdwave/display_edit.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace lvd::wave;
using Clock = std::chrono::steady_clock;

static double msSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

int main(int argc, char** argv) {
    const int rows = argc > 1 ? std::atoi(argv[1]) : 5000;
    DisplayModel m;
    for (int i = 0; i < rows; ++i)
        m.root().children.push_back(DisplayModel::makeSignal("top.s" + std::to_string(i)));
    const ItemId mid = m.root().children[static_cast<std::size_t>(rows / 2)].id;

    constexpr int kRepeat = 50;
    auto t = Clock::now();
    for (int i = 0; i < kRepeat; ++i)
        moveItems(m, {mid}, i % 2 == 0 ? -1 : 1);
    std::printf("moveItems, one row of %d:           %8.3f ms\n", rows, msSince(t) / kRepeat);

    std::vector<ItemId> hundred;
    for (int i = 0; i < 100; ++i)
        hundred.push_back(m.root().children[static_cast<std::size_t>(i * rows / 100)].id);
    t = Clock::now();
    for (int i = 0; i < kRepeat; ++i)
        moveItems(m, hundred, i % 2 == 0 ? -1 : 1);
    std::printf("moveItems, 100 rows of %d:          %8.3f ms\n", rows, msSince(t) / kRepeat);

    t = Clock::now();
    auto w = wrapInGroup(m, hundred, "G");
    std::printf("wrapInGroup, 100 rows of %d:        %8.3f ms\n", rows, msSince(t));
    const ItemId g = w.value().created.front();
    t = Clock::now();
    moveItemsToGroup(m, {mid}, g, 0);
    std::printf("moveItemsToGroup, one row:           %8.3f ms\n", msSince(t));
    t = Clock::now();
    ungroup(m, {g});
    std::printf("ungroup, 101 rows of %d:            %8.3f ms\n", rows, msSince(t));
    return 0;
}
