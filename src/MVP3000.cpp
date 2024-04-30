/* 
Copyright Production 3000

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. 
*/

#include "MVP3000.h"

MVP3000 mvp;


void MVP3000::setup() {
    // Start logging first obviously
    logger.setup();
    // Prepare flash to allow loading of saved configs
    config.setup();
    led.setup();
    net.setup();
    // Modules
    for (uint8_t i = 0; i < moduleCount; i++) {
        xmodules[i]->setup();
    }
}

void MVP3000::loop() {
    updateLoopDuration();
    checkStatus();

    led.loop();
    net.loop();
    // Modules
    for (uint8_t i = 0; i < moduleCount; i++) {
        xmodules[i]->loop();
    }
}

void MVP3000::addXmodule(Xmodule *xmodule) {
    if (moduleCount >= MAX_MODULES) {
        return;
    }

    xmodules[moduleCount] = xmodule;
    // modules[moduleCount]->setup();
    moduleCount++;
}


void MVP3000::checkStatus() {
    // Never leave error state
    if (status == Status::ERROR)
        return;

    // Error was logged
    if (mvp.logger.errorReported) {
        status = Status::ERROR;
        return;
    }

    // Other cases???
}

void MVP3000::updateLoopDuration() {
    // Skip first loop iteration, nothing to calculate
    if (loopLast_ms > 0) {
        if (loopDuration_ms == 0)
            // Second loop iteration, kickstart averaging
            loopDuration_ms = millis() - loopLast_ms;
        else
            // Third and higher loop iteration, rolling average latest five values
            loopDuration_ms = round((float_t)4/5 * loopDuration_ms + (float_t)1/5 * (millis() - loopLast_ms));
    }
    loopLast_ms = millis();
}
