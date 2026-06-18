#pragma once

#include "Interfaces.hpp"
#include "Types.hpp"

#include <cstdint>

namespace rv1126b {

struct AiScheduleDecision {
    bool run_gesture{false};
    bool run_pose{false};
    bool run_cup{false};
};

class AiScheduler {
public:
    void configure(const AppConfig& config);
    AiScheduleDecision next(int64_t timestamp_ms, bool running_mode);
    void reset();

private:
    static bool due(int64_t now_ms, int64_t last_ms, int interval_ms);

    int pose_interval_ms_{150};
    int gesture_interval_ms_{300};
    int cup_interval_ms_{500};
    int64_t last_pose_ms_{-1};
    int64_t last_gesture_ms_{-1};
    int64_t last_cup_ms_{-1};
};

}  // namespace rv1126b
