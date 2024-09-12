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
    // Register home page
    net.netWeb.registerPage("/", webPage, std::bind(&MVP3000::webPageProcessor, this, std::placeholders::_1));

    // Modules
    for (uint8_t i = 0; i < moduleCount; i++) {
        xmodules[i]->setup();
    }
}

void MVP3000::loop() {
    updateLoopDuration();
    checkStatus();

    config.loop();
    led.loop();
    net.loop();
    // Modules
    for (uint8_t i = 0; i < moduleCount; i++) {
        xmodules[i]->loop();
    }

    // Check if delayed restart was set
    if (delayedRestart_ms > 0) {
        if (millis() > delayedRestart_ms) {
            // delayedRestart_ms = 0; // Not needed as we reset the ESP
            ESPX.reset();
        }
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
    if (state == STATE_TYPE::ERROR)
        return;

    // Error was logged
    if (logger.errorReported) {
        state = STATE_TYPE::ERROR;
        return;
    }

    if ((net.netState == Net::NET_STATE_TYPE::CLIENT) || (net.netState == Net::NET_STATE_TYPE::AP))
        state = STATE_TYPE::GOOD;
    else
        state = STATE_TYPE::INIT;
}

void MVP3000::updateLoopDuration() {
    // Only start measuring loop duration after wifi is up, as this adds a single long duration and messes with max value
    if (state != STATE_TYPE::GOOD)
        return;

    // Skip first loop iteration, nothing to calculate
    if (loopLast_ms > 0) {
        // Current loop duration
        uint16_t loopDuration = millis() - loopLast_ms;

        // Update min and max loop duration
        loopDurationMax_ms = max(loopDurationMax_ms, loopDuration);
        loopDurationMin_ms = min(loopDurationMin_ms, loopDuration); // Is often 0, there are lots of loops where nothing happens

        // Calculate mean loop duration
        if (loopDurationMean_ms == 0)
            // Second loop iteration, kickstart averaging
            loopDurationMean_ms = loopDuration;
        else
            // Third and higher loop iteration, rolling average latest ten values
            loopDurationMean_ms = round((float_t)9/10 * loopDurationMean_ms + (float_t)1/10 * loopDuration);
    }

    // Remember this loop time
    loopLast_ms = millis();
}


///////////////////////////////////////////////////////////////////////////////////

String MVP3000::webPageProcessor(uint8_t var) {
    String str = ""; // Needs to be defined outside of switch
    switch (var) {
        case 11:
            return String(__DATE__) + " " + String(__TIME__);
        case 12:
            return String(ESP.getFreeHeap()) + " / " + String(ESPX.getHeapFragmentation());
        case 13:
            return String(helper.millisToTime(millis()));
        case 14:
            return String(ESPX.getResetReason().c_str());
        case 15:
            return String(ESP.getCpuFreqMHz());
        case 16:
            return String(loopDurationMean_ms) + " / " + String(loopDurationMin_ms) + " / " + String(loopDurationMax_ms);
        case 17:
            return logger.getRecentLog();
        case 18:
            return (net.netCom.isHardDisabled()) ? "UDP discovery (disabled)" : "<a href='/netcom'>UDP discovery</a>";
        case 20:
            if (moduleCount == 0)
                return "<li>None</li>";
            for (uint8_t i = 0; i < moduleCount; i++) {
                char message[128];
                if ((xmodules[i]->uri).length() > 0) {
                    snprintf(message, sizeof(message), "<li><a href='%s'>%s</a></li>", xmodules[i]->uri.c_str(), xmodules[i]->description.c_str());
                } else {
                    snprintf(message, sizeof(message), "<li>%s</li>", xmodules[i]->description.c_str());
                }
                str += message;
            }
            return str;

        default:
            return str;
    }
}
