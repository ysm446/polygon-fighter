#pragma once
#include <algorithm>

namespace pf {
class FixedClock {
public:
    static constexpr double Step = 1.0 / 60.0;
    template<class Update>
    int Advance(double elapsed, bool paused, bool singleStep, Update&& update) {
        if (paused) {
            accumulator_ = 0;
            if (singleStep) { update(); return 1; }
            return 0;
        }
        // 長時間停止からの復帰時に大量の物理更新が連鎖するのを防ぐ。
        accumulator_ += std::clamp(elapsed, 0.0, 0.25);
        int count = 0;
        while (accumulator_ + 1e-12 >= Step) {
            update();
            accumulator_ = std::max(0.0, accumulator_ - Step);
            ++count;
        }
        return count;
    }
    void Reset() { accumulator_ = 0; }
private:
    double accumulator_ = 0;
};
}
