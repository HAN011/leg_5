#pragma once
#include "app/observer/observer.hpp"
#include "app/system_parameters.hpp"
#include "bsp/dwt/dwt.h"
#include "device/Dji_motor/DJI_motor.hpp"
#include "device/RC/remote_control_data.hpp"
#include "device/super_cap/super_cap.hpp"
#include "module/IMU_EKF/IMU_EKF.hpp"
#include "module/referee/status.hpp"
#include <Eigen/Dense>
#include <cmath>
#include <limits>
#include <numbers>
double max_speed_watch[2] = {};
double head_leg_angle     = 0;
namespace app::controller {
struct desire {
    Eigen::Matrix<double, 10, 1> xd = Eigen::Matrix<double, 10, 1>::Zero();
    double roll                     = 0;
    double leg_length               = 0.12;
};
struct AngleInterval {
    double lower;
    double upper;
    double value;
    bool isReverseCheck; // 用于处理跨0点的区间
};
class DesireSet {
public:
    static DesireSet* GetInstance() {
        static auto instance = new DesireSet();
        return instance;
    }
    static constexpr double spinning_velocity = 18.0;
    static constexpr double x_velocity_scale  = 2.5;
    static constexpr double TWO_PI            = 2 * std::numbers::pi;
    static constexpr double PI_7_4            = 7 * std::numbers::pi / 4;
    static constexpr double PI_5_4            = 5 * std::numbers::pi / 4;
    static constexpr double PI_3_4            = 3 * std::numbers::pi / 4;
    static constexpr double PI_1_4            = std::numbers::pi / 4;
    static constexpr double PI_1_6            = std::numbers::pi / 6;
    static constexpr double PI_2_6            = std::numbers::pi / 6;
    static constexpr double PI_4_6            = std::numbers::pi * 4 / 6;
    static constexpr double PI_5_6            = std::numbers::pi * 5 / 6;
    static constexpr double PI_7_6            = std::numbers::pi * 7 / 6;
    static constexpr double PI_8_6            = std::numbers::pi * 8 / 6;
    static constexpr double PI_10_6           = std::numbers::pi * 10 / 6;
    static constexpr double PI_11_6           = std::numbers::pi * 11 / 6;

    void update() {
        using namespace device;

        do {
            motor_alive_detect();
            // 双下/未知状态/底盘云台非全通电
            if ((RC_->switch_left == RC_Switch::UNKNOWN || RC_->switch_right == RC_Switch::UNKNOWN)
                || ((RC_->switch_left == RC_Switch::DOWN) && (RC_->switch_right == RC_Switch::DOWN))
                || !(referee_->chassis_power_status && referee_->gimbal_power_status)
                || !motor_alive_) {
                reset_all_controls();
                SuperCap_ON_  = false;
                chassis_mode_ = chassis_mode::stop;
                break;
            }

            if (RC_->switch_right == RC_Switch::MIDDLE) {
                if (!last_keyboard_.c && RC_->keyboard.c) {
                    chassis_mode_ = chassis_mode_ == chassis_mode::spin ? chassis_mode::follow
                                                                        : chassis_mode::spin;
                } else if (!last_keyboard_.r && RC_->keyboard.r) {
                    balanceless_mode_ = !balanceless_mode_;
                    if (!balanceless_mode_)
                        chassis_mode_ = chassis_mode::follow;
                } else if (
                    RC_->keyboard.w || RC_->keyboard.s
                    || (last_switch_right != RC_Switch::MIDDLE)) {
                    chassis_mode_ = chassis_mode::follow;
                }
            }
            if ((((RC_->switch_left == RC_Switch::MIDDLE)
                  && (RC_->switch_right == RC_Switch::DOWN)))
                || balanceless_mode_) {
                reset_all_controls();
                chassis_mode_ = chassis_mode::balanceless;
            }
            if (RC_->switch_right == RC_Switch::UP) {
                chassis_mode_ = chassis_mode::spin_control;
            }

            auto lerp = [](double start, double end, double t) {
                return start + t * (end - start);
            };
            static double current_xmove       = 0.0;
            static double current_ymove       = 0.0;
            static double current_zmove       = 0.0;
            double target_xmove               = 1.0 * RC_->keyboard.w - RC_->keyboard.s;
            double target_ymove               = 0.5 * RC_->keyboard.a - RC_->keyboard.d;
            double target_zmove               = 0.5 * RC_->keyboard.q - RC_->keyboard.e;
            constexpr double smoothing_factor = 1.0 / 1000; // 1s
            current_xmove                     = lerp(current_xmove, target_xmove, smoothing_factor);
            current_ymove                     = lerp(current_ymove, target_ymove, smoothing_factor);
            current_zmove                     = lerp(current_zmove, target_zmove, smoothing_factor);

            current_xmove       = target_xmove == 0 ? target_xmove : current_xmove;
            current_ymove       = target_ymove == 0 ? target_ymove : current_ymove;
            current_zmove       = target_zmove == 0 ? target_zmove : current_zmove;
            auto keyboard_xmove = current_xmove;
            auto keyboard_ymove = current_ymove;
            auto keyboard_zmove = current_zmove;

            switch (chassis_mode_) {
            case chassis_mode::follow:
                if (RC_->keyboard.ctrl && (RC_->keyboard.w || RC_->keyboard.s)) {
                    set_states_desire(RC_->joystick_right.x() + keyboard_xmove * 0.35, 0);
                } else {
                    set_states_desire(RC_->joystick_right.x() + keyboard_xmove, 0);
                }
                set_length_desire(RC_->joystick_right.y() + keyboard_zmove);
                break;
            case chassis_mode::spin: set_states_desire(0, spinning_velocity); break;
            case chassis_mode::balanceless:
                if (RC_->keyboard.ctrl && (RC_->keyboard.w || RC_->keyboard.s)) {
                    set_states_desire(
                        RC_->joystick_right.x() + keyboard_xmove * 0.35,
                        RC_->joystick_right.y() + keyboard_ymove * 0.35);
                } else {
                    set_states_desire(
                        RC_->joystick_right.x() + keyboard_xmove,
                        RC_->joystick_right.y() + keyboard_ymove);
                }
                break;
            case chassis_mode::spin_control:
                // set_states_desire(0, RC_->joystick_right.x() * spinning_velocity);
                set_states_desire(RC_->joystick_right.y() * 0.4, 13, RC_->joystick_right.x() * 0.4);
                // set_length_desire(RC_->joystick_right.y() + keyboard_zmove);
                break;
            default: break;
            }

            if (RC_->keyboard.f) {
                desires.leg_length = 0.18;
            } else if (RC_->keyboard.g) {
                desires.leg_length = 0.25;
            }
            SuperCap_ON_ = RC_->keyboard.shift;

            status_flag.set_to_climb = false;
        } while (false);
        last_switch_right = RC_->switch_right;
        if (chassis_mode_ == chassis_mode::stop)
            last_switch_right = RC_Switch::UNKNOWN;
        last_keyboard_ = RC_->keyboard;
    }
    void Init(
        module::IMU_output_vector* IMU_output, device::RC_status* RC, device::DjiMotor* GM6020_yaw,
        std::array<module::DM8009*, 4> DM8009, std::array<device::DjiMotor*, 2> M3508,
        module::referee::Status* referee, device::SuperCap* supercap_instance) {
        IMU_data_   = IMU_output;
        RC_         = RC;
        GM6020_yaw_ = GM6020_yaw;
        DM8009_     = DM8009;
        M3508_      = M3508;
        referee_    = referee;
        supercap_   = supercap_instance;
    }

    void CanLost() { reset_all_controls(); }

    desire desires    = {};
    bool SuperCap_ON_ = false;
    double power_limit_velocity;

private:
    DesireSet()                                            = default; // 禁止外部构造
    ~DesireSet()                                           = default; // 禁止外部析构
    DesireSet(const DesireSet& DesireSet)                  = delete;  // 禁止外部拷贝构造
    const DesireSet& operator=(const DesireSet& DesireSet) = delete;  // 禁止外部赋值操作

    static constexpr double inf = std::numeric_limits<double>::infinity();
    static constexpr double nan = std::numeric_limits<double>::quiet_NaN();

    device::DjiMotor* GM6020_yaw_           = nullptr;
    std::array<module::DM8009*, 4> DM8009_  = {};
    std::array<device::DjiMotor*, 2> M3508_ = {};

    const device::RC_status* RC_               = nullptr;
    const module::IMU_output_vector* IMU_data_ = nullptr;
    module::referee::Status* referee_          = nullptr;
    observer::observer* observer_              = observer::observer::GetInstance();
    device::SuperCap* supercap_;

    device::RC_Keyboard last_keyboard_  = {};
    device::RC_Switch last_switch_right = {};
    bool balanceless_mode_              = false;
    bool motor_alive_                   = false;

    double yaw_set = 0.0;
    // 平动
    void set_states_desire(double x_velocity, double rotation_velocity) {
        set_states_desire(x_velocity, rotation_velocity, 0);
    }
    // 小陀螺
    void set_states_desire(double x_velocity, double rotation_velocity, double y_velocity) {
        auto x_d_ref              = x_velocity * x_velocity_scale;
        auto y_d_ref              = y_velocity * x_velocity_scale;
        auto gimbal_yaw_angle     = GM6020_yaw_->get_angle() + std::numbers::pi * 3 / 4;
        gimbal_yaw_angle          = std::fmod(gimbal_yaw_angle, TWO_PI);
        constexpr double power_kp = 0.26;

        power_limit_velocity = power_kp * std::sqrt(referee_->chassis_power_limit_);
        max_speed_watch[0]   = power_limit_velocity;
        if (!supercap_->Info.enabled_)
            x_d_ref = std::clamp(x_d_ref, -power_limit_velocity, power_limit_velocity); // 功控
        x_d_ref = std::clamp(x_d_ref, -x_velocity_scale, x_velocity_scale);             // 上限
        static uint32_t last_time = 0;
        auto dt                   = DWT_GetDeltaT64_Expect(&last_time, app::dt);

        desires.xd(0, 0) = 0;       // distance :always 0 during velocity control
        desires.xd(1, 0) = x_d_ref; // velocity
        if (std::fabs(x_d_ref) > 1e-3)
            status_flag.IsControlling = true;
        else
            status_flag.IsControlling = false;

        head_leg_angle = std::fmod(gimbal_yaw_angle, TWO_PI);
        if (head_leg_angle < 0)
            head_leg_angle += TWO_PI;
        status_flag.IsSpinning                          = false;
        desires.xd(0, 0)                                = 0;
        const std::array<AngleInterval, 5> angleActions = {
            {
             // 按顺时针方向排列区间
                {0, PI_1_6, -x_d_ref, false},
             {PI_2_6, PI_4_6, -y_d_ref, false},
             {PI_5_6, PI_7_6, x_d_ref, false},
             {PI_8_6, PI_10_6, y_d_ref, false},
             {PI_11_6, TWO_PI, -x_d_ref, true} // 跨0点的特殊区间
            }
        };
        switch (chassis_mode_) {                  // yaw
        case chassis_mode::follow:
        case chassis_mode::balanceless:
            // // 无头
            // yaw_set += RC_->joystick_left.y() * 0.004;
            // desires.xd(2, 0) = yaw_set;
            // 有头
            desires.xd(2, 0) = IMU_data_->Yaw_multi_turn + (gimbal_yaw_angle - std::numbers::pi);
            break;
        case chassis_mode::spin:
        case chassis_mode::spin_control: {
            auto it = std::lower_bound(
                angleActions.begin(), angleActions.end(), head_leg_angle,
                [](const AngleInterval& a, double angle) {
                    return a.isReverseCheck ? (angle >= a.upper) : (a.upper <= angle);
                });

            if (it != angleActions.end()) {
                bool condition = it->isReverseCheck
                                   ? (head_leg_angle > it->lower || head_leg_angle < it->upper)
                                   : (it->lower < head_leg_angle && head_leg_angle < it->upper);

                if (condition)
                    desires.xd(1, 0) = it->value;
                else {
                    desires.xd(1, 0) = 0;
                }
            }

            // if ((std::numbers::pi * 2 > head_leg_angle && head_leg_angle > std::numbers::pi * 7 /
            // 4)
            //     || (std::numbers::pi / 4 > head_leg_angle && head_leg_angle > 0)) {
            //     desires.xd(1, 0) = -x_d_ref;
            // } else if (
            //     std::numbers::pi * 5 / 4 > head_leg_angle
            //     && head_leg_angle > std::numbers::pi * 3 / 4) {
            //     desires.xd(1, 0) = x_d_ref;
            // } else if (
            //     std::numbers::pi * 3 / 4 > head_leg_angle
            //     && head_leg_angle > std::numbers::pi / 4) {
            //     desires.xd(1, 0) = -y_d_ref;
            // } else if (
            //     std::numbers::pi * 7 / 4 > head_leg_angle
            //     && head_leg_angle > std::numbers::pi * 5 / 4) {
            //     desires.xd(1, 0) = y_d_ref;
            // }
            desires.xd(2, 0) += rotation_velocity * dt;
            desires.xd(3, 0)       = rotation_velocity;
            status_flag.IsSpinning = true;
            break;
        }
        default: break;
        }
        if (chassis_mode_ != chassis_mode::spin_control) {
            for (uint8_t i = 3; i < 10; i++) {
                desires.xd(i, 0) = 0;
            }
        } else {
            for (uint8_t i = 4; i < 10; i++) {
                desires.xd(i, 0) = 0;
            }
        }
    }
    void set_length_desire(double length_speed) {
        constexpr double length_speed_scale = 0.0003;
        desires.leg_length += length_speed * length_speed_scale;
        desires.leg_length = std::clamp(desires.leg_length, 0.12, 0.27);
    }
    void reset_all_controls() {
        desires.xd.setZero();
        desires.roll                = 0;
        desires.leg_length          = 0.12;
        chassis_mode_               = chassis_mode::stop;
        status_flag.moving_jump_cmd = false;
        status_flag.stand_jump_cmd  = false;
        SuperCap_ON_                = false;
    }
    void motor_alive_detect() {
        motor_alive_ = false;
        for (auto motor : M3508_) {
            if (!motor->get_online_states()) {
                return;
            }
        }
        for (auto motor : DM8009_) {
            if (!motor->get_online_states()) {
                return;
            }
        }
        // 无头模式，待改回
        if (!GM6020_yaw_->get_online_states()) {
            return;
        }
        motor_alive_ = true;
    }
};

} // namespace app::controller
