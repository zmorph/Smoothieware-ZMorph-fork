/*
      This file is part of Smoothie (http://smoothieware.org/). The motion control part is heavily based on Grbl (https://github.com/simen/grbl).
      Smoothie is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
      Smoothie is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
      You should have received a copy of the GNU General Public License along with Smoothie. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SPINDLE_MODULE_H
#define SPINDLE_MODULE_H

#include "libs/Module.h"
#include <stdint.h>

namespace mbed {
    class PwmOut;
    class InterruptIn;
}

// This module implements closed loop PID control for spindle RPM.
class Spindle: public Module {
    public:
        Spindle();
        virtual ~Spindle() {};
        void on_module_loaded();
        float get_current_rpm();
        
    private:
        void on_pin_rise();
        void on_gcode_received(void *argument);
        void on_gcode_execute(void *argument);
        uint32_t on_update_speed(uint32_t dummy);
        
        mbed::PwmOut *spindle_pin; // PWM output for spindle speed control
        mbed::InterruptIn *feedback_pin; // Interrupt pin for measuring speed
        bool output_inverted;
        
        // Current values, updated at runtime
        bool spindle_on;
        float current_rpm;
        float target_rpm;
};

#endif

