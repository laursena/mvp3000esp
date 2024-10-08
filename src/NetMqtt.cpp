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

///////////////////////////////////////////////////////////////////////////////////

/*

    Server sends a DIscover SENSor to broadcast, ESP responds with REsponse SENSor
        DISENS RESENS
    ESP sends a DIscover SERVer to broadcast, server responds with REsponse SERVer
        DISERV RESERV

*/

///////////////////////////////////////////////////////////////////////////////////


#include "NetMqtt.h"

#include "MVP3000.h"
extern MVP3000 mvp;


void NetMqtt::setup() {
    // Read config and register with web interface
    mvp.config.readCfg(cfgNetMqtt);
    setMqttState();

    // Redefine needed with network, otherwise mqttClient.connected() crashes
    mqttClient = MqttClient(wifiClient);

    // Register config
    mvp.net.netWeb.registerCfg(&cfgNetMqtt, std::bind(&NetMqtt::saveCfgCallback, this));

    // For some reason the mqttClient.onMessage() method does not work when the function is in a class ...
    // mqttClient.onMessage(handleMessage); // argument of type "void (NetMqtt::*)(int messageSize)" is incompatible with parameter of type "void (*)(int)"
    // mqttClient.onMessage([] (int messageSize) { handleMessage; }); // invalid use of non-static member function 'void NetMqtt::handleMessage(int)'
    // mqttClient.onMessage([&] (int messageSize) { handleMessage; }); // no suitable conversion function from "lambda [](int messageSize)->void" to "void (*)(int)" exists
    // mqttClient.onMessage(std::bind(&NetMqtt::handleMessage, this, std::placeholders::_1)); // no suitable conversion function from "std::_Bind_helper<false, void (NetMqtt::*)(int messageSize), NetMqtt *, const std::_Placeholder<1> &>::type" (aka "std::_Bind<std::__remove_cv_t<void (NetMqtt::*)(int messageSize)> (std::__remove_cv_t<NetMqtt *>, std::__remove_cv_t<const std::_Placeholder<1>>)>") to "void (*)(int)" exists
};

void NetMqtt::loop() {
    // Called from net.loop() only if wifi is up and in client mode, check again

    // Check if there is actually something to do
    if ((mqttState == MQTT_STATE::DISABLEDX) || !linkedListMqttTopic.hasTopics() || !mvp.net.connectedAsClient())
        return;

    int messageSize;
    switch (mqttState) {

        case MQTT_STATE::NOBROKER:
            // No MQTT external or local broker known, query auto-discovery
            localBrokerIp = mvp.net.netCom.checkSkill("MQTT");
            setMqttState();
            break;

        case MQTT_STATE::CONNECTING:
            // Check state change
            if (mqttClient.connected()) {
                mqttState = MQTT_STATE::CONNECTED;
                mvp.logger.write(CfgLogger::Level::INFO, "Connected to MQTT broker, subscribing to topics.");
                // Subscribe to all topics with a callback
                linkedListMqttTopic.loop([&](DataStructMqttTopic* current, uint16_t i) {
                    // Only subscribe if there is a callback
                    if (current->ctrlCallback != nullptr) {
                        mqttClient.subscribe(current->getCtrlTopic());
                    }
                });
                break;
            }

            // Try to connect to broker
            connectMqtt();
            break;

        case MQTT_STATE::CONNECTED:
            // Check state change
            if (!mqttClient.connected()) {
                setMqttState();
                mvp.logger.write(CfgLogger::Level::WARNING, "Disconnected from MQTT broker.");
                break;
            }

            // When mqttClient.onMessage() would work we could just call mqttClient.poll(); here
            // Check if a MQTT message was received, parseMessage calls poll() itself
            messageSize = mqttClient.parseMessage();
            if (messageSize > 0) {
                handleMessage(messageSize);
            }
            break;

        case MQTT_STATE::DISCONNECTED: // We have given up, do we want this ??? sometimes yes, external server is down maybe? but then again, if we want MQTT we should never give up?
            break;

        case MQTT_STATE::DISABLEDX:
            break;
    }
}


///////////////////////////////////////////////////////////////////////////////////

std::function<void(const String &message)> NetMqtt::registerMqtt(const String& baseTopic, MqttCtrlCallback ctrlCallback) {
    // Store topic and callback for registering with MQTT, return the function to write to this topic
    return linkedListMqttTopic.appendUnique(&mqttClient, baseTopic, ctrlCallback);
}

void NetMqtt::setMqttState() {
    if (!cfgNetMqtt.mqttEnabled) {
        mqttState = MQTT_STATE::DISABLEDX;
    } else if ((cfgNetMqtt.mqttForcedBroker.length() > 0) || (localBrokerIp != INADDR_NONE)) {
        mqttState = MQTT_STATE::CONNECTING;
    } else {
        mqttState = MQTT_STATE::NOBROKER;
    }
    connectTimer.restart();
}

void NetMqtt::connectMqtt() {
    // Reconnect tries are gone, remove local broker as it did not work
    if (connectTimer.plusOne()) {
        mqttState = MQTT_STATE::DISCONNECTED;
        mvp.logger.writeFormatted(CfgLogger::Level::INFO, "Connecting to MQTT broker failed, giving up.");
        return;
    }

    // Only work to do if interval not yet started or just finished
    if (!connectTimer.justFinished())
        return;

    if (cfgNetMqtt.mqttForcedBroker.length() > 0) {
        // Connect to forced broker
        mqttClient.connect(cfgNetMqtt.mqttForcedBroker.c_str(), cfgNetMqtt.mqttPort);
        mvp.logger.writeFormatted(CfgLogger::Level::INFO, "Connecting to remote MQTT broker: %s", cfgNetMqtt.mqttForcedBroker.c_str());
    } else {
        // Update local broker, it could have changed since it was originally queried
        localBrokerIp = mvp.net.netCom.checkSkill("MQTT");
        if (localBrokerIp == INADDR_NONE) {
            setMqttState();
            return;
        }
        // The library is broken for ESP8266, it does not accept the IPAddress-type when a port is given
        mqttClient.connect(localBrokerIp.toString().c_str(), cfgNetMqtt.mqttPort);
        mvp.logger.writeFormatted(CfgLogger::Level::INFO, "Connecting to local MQTT broker: %s", localBrokerIp.toString().c_str());
    }
}

void NetMqtt::handleMessage(int messageSize) {
    // Check if message is a duplicate, requires QoS 1+ and needs to be implemented by the sender and the receiver
    if (mqttClient.messageDup())
        return; // Handling of duplicates not implemented

    String topic = mqttClient.messageTopic();
    // Topic is prefixed with the device ID and suffixed with _ctrl, we only need the base topic
    topic = topic.substring(topic.indexOf('_') + 1, topic.lastIndexOf('_'));

    // Copy message to buffer, needs to be done after reading the topic as it clears the message-ready flag
    uint8_t buf[messageSize + 1];
    mqttClient.read(buf, messageSize + 1);
    buf[messageSize] = '\0';

    // Find the topic in the list and execute callback
    DataStructMqttTopic *mqttTopic = linkedListMqttTopic.findTopic(topic);
    if ((mqttTopic != nullptr) && (mqttTopic->ctrlCallback != nullptr)) {
        mqttTopic->ctrlCallback((char *)buf);
    } else {
        mvp.logger.writeFormatted(CfgLogger::Level::CONTROL, "MQTT control with unknown topic '%s'", topic.c_str());
    }
}


///////////////////////////////////////////////////////////////////////////////////

void NetMqtt::saveCfgCallback() {
    mvp.logger.write(CfgLogger::Level::INFO, "MQTT configuration changed, restarting MQTT client.");
    setMqttState();
    mqttClient.stop();
}

String NetMqtt::templateProcessor(uint8_t var) {
    switch (var) {
        case 61:
            return (cfgNetMqtt.mqttEnabled) ? "checked" : "";
        case 62:
            switch (mqttState) {
                case MQTT_STATE::CONNECTED:
                    return "connected";
                case MQTT_STATE::DISCONNECTED:
                case MQTT_STATE::DISABLEDX:
                    return "disconnected";
                case MQTT_STATE::CONNECTING:
                    return "connecting";
                case MQTT_STATE::NOBROKER:
                    return "no broker";
            }
        case 63:
            return (localBrokerIp != INADDR_NONE) ? localBrokerIp.toString() : "-";
        case 64:
            return cfgNetMqtt.mqttForcedBroker;
        case 65:
            return String(cfgNetMqtt.mqttPort);

        // Filling of the MQTT topics is better be split, long strings are never good during runtime
        case 70:
            // Check if list is empty
            if (linkedListMqttTopic.getSize() == 0) {
                return "<li>None</li>";
            }
            // Set initial bookmark
            linkedListMqttTopic.bookmarkByIndex(0, true);
        case 71:
            return _helper.printFormatted("<li>%s %s %s</li> %s", linkedListMqttTopic.getBookmarkData()->getDataTopic().c_str(),
                (linkedListMqttTopic.getBookmarkData()->ctrlCallback != nullptr) ? " | " : "",
                (linkedListMqttTopic.getBookmarkData()->ctrlCallback != nullptr) ? linkedListMqttTopic.getBookmarkData()->getCtrlTopic().c_str() : "",
                (linkedListMqttTopic.moveBookmark()) ? "%61%" : "");

        default:
            return "";
    }
}
