// Edited by: OpenAI-Codex_Responsible: Source_Code
#ifndef ETS2AP_PHASE0_SCRIPT_H
#define ETS2AP_PHASE0_SCRIPT_H

namespace ets2ap_spike {

enum class Mode { off, steer, throttle, brake, buttons };

struct Values {
    float steering = 0.0f;
    float throttle = 0.0f;
    float brake = 0.0f;
    bool test_button = false;
};

constexpr double script_duration(Mode mode) noexcept {
    switch (mode) {
    case Mode::steer: return 18.0;
    case Mode::throttle: case Mode::brake: return 12.0;
    case Mode::buttons: return 10.0;
    default: return 0.0;
    }
}

constexpr Values evaluate_script(Mode mode, double script_t) noexcept {
    Values result{};
    if (script_t < 0.0 || script_t >= script_duration(mode)) return result;
    if (mode == Mode::steer) {
        if (script_t >= 2.0 && script_t < 5.0) result.steering = 0.5f;
        else if (script_t >= 7.0 && script_t < 10.0) result.steering = -0.5f;
        else if (script_t >= 12.0 && script_t < 16.0)
            result.steering = static_cast<float>((script_t - 12.0) / 4.0);
    } else if (mode == Mode::throttle) {
        if (script_t >= 2.0 && script_t < 5.0) result.throttle = 0.3f;
        else if (script_t >= 7.0 && script_t < 10.0) result.throttle = 0.6f;
    } else if (mode == Mode::brake) {
        if (script_t >= 2.0 && script_t < 5.0) result.brake = 0.3f;
        else if (script_t >= 7.0 && script_t < 10.0) result.brake = 0.6f;
    } else if (mode == Mode::buttons) {
        result.test_button = (script_t >= 1.0 && script_t < 2.0) ||
            (script_t >= 4.0 && script_t < 5.0) || (script_t >= 7.0 && script_t < 8.0);
    }
    return result;
}

} // namespace ets2ap_spike

#endif
