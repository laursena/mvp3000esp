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

#ifndef MVP3000_NETMQTT
#define MVP3000_NETMQTT

#include <Arduino.h>

#ifdef ESP8266
    #include <ESP8266WiFi.h>
#else
    #include <WiFi.h>
    #include <IPAddress.h>
#endif
#include <WiFiUdp.h>
#include <ArduinoMqttClient.h>
#include <millisDelay.h> // https://github.com/PowerBroker2/SafeString

#include "Config.h"
#include "ESPX.h"
#ifdef ESP8266
    extern EspClass ESPX;
#else
    extern EspClassX ESPX;
#endif


struct CfgNetMqtt : public CfgJsonInterface {

    // Fixed settings

    boolean mqttEnabled = true;

    // Modifiable settings saved to SPIFF

    uint16_t mqttPort = 1883; // 1883: unencrypted, unauthenticated
    String mqttForcedBroker = ""; // test.mosquitto.org
    String mqttTopicSuffix = "myesp";

    CfgNetMqtt() : CfgJsonInterface("cfgNetMqtt") {
        addSetting<uint16_t>("mqttPort", &mqttPort, [](uint16_t x) { return (x < 1024) ? false : true; }); // port above 1024
        addSetting<String>("mqttForcedBroker", &mqttForcedBroker, [](String x) { return ((x.length() > 0) && (x.length() < 6)) ? false : true; } ); // allow empty to remove
        addSetting<String>("mqttTopicSuffix", &mqttTopicSuffix, [](String x) { return (x.length() < 5) ? false : true; }); // min 5 chars
    }
};


class NetMqtt {

    public:

        void setup();
        void loop();

        /**
         * @brief Register a topic for MQTT communication.
         * 
         * @param topic The topic to register. It is prefixed with the device ID and suffixed with _data and _ctrl.
         * @param dataCallback The function to call when data is received on the topic suffixed with _ctrl. Omit to not subscribe to the topic.
         * @return Returns the function to write data to MQTT.
         */
        std::function<void(const String &message)> registerMqtt(String topic, std::function<void(char*)> dataCallback = nullptr);

    private:

        enum class MQTT_STATE: uint8_t {
            CONNECTED = 0,
            DISCONNECTED = 1
        };

        struct MqttTopicList {
            struct Node {
                String topic;
                std::function<void(char*)> dataCallback;

                Node* next;

            MqttClient* mqttClient;

                Node() { }
                Node(String topic, std::function<void(char*)> dataCallback, MqttClient* mqttClient) : topic(topic), dataCallback(dataCallback), mqttClient(mqttClient) { }

                String getCtrlTopic() { return topic + "_ctrl"; }
                String getDataTopic() { return topic + "_data"; }

                std::function<void(const String &message)> getMqttPrint() { return std::bind(&Node::mqttPrint, this, std::placeholders::_1); }
                void mqttPrint(const String &message) {
                    // Only write if connected
                    if (mqttClient->connected()) {
                        mqttClient->beginMessage(getDataTopic());
                        mqttClient->print(message);
                        mqttClient->endMessage();
                    }
                }
            };

            Node* head = nullptr;

            MqttClient* mqttClient;


            MqttTopicList(MqttClient* mqttClient) : mqttClient(mqttClient) { }


            std::function<void(const String &message)> add(String topic, std::function<void(char*)> dataCallback) {
                Node* newNode = new Node(topic , dataCallback, mqttClient);
                newNode->next = head;
                head = newNode;

                return newNode->getMqttPrint();
            }

            boolean findAndExecute(String topic, char* data) {
                Node* current = head;
                while (current != nullptr) {
                    if (current->topic.compareTo(topic)) {
                        current->dataCallback(data);
                        return true;
                    }
                    current = current->next;
                }
                return false;
            }

            boolean hasTopics() { return head != nullptr; }

            void subscribeAll() {
                Node* current = head;
                while (current != nullptr) {
                    // Only subscribe if there is a callback
                    if (current->dataCallback != nullptr)
                        mqttClient->subscribe(current->getCtrlTopic().c_str());
                    current = current->next;
                }
            }
        };


        CfgNetMqtt cfgNetMqtt;

        WiFiClient wifiClient;

        MqttClient mqttClient = NULL;

        MQTT_STATE mqttState = MQTT_STATE::DISCONNECTED;
        MqttTopicList mqttTopicList = MqttTopicList(&mqttClient);

        IPAddress localBrokerIp = INADDR_NONE; // compare with == operator, there is

        uint16_t connectInterval = 5000;
        millisDelay connectDelay;
        void mqttConnect();

        void handleMessage(int messageSize);


        String webPageProcessor(const String& var);
        char* webPage = R"===(
<!DOCTYPE html> <html lang='en'>
<head> <title>MVP3000 - Device ID %0%</title>
<script>function promptId(f) { f.elements['deviceId'].value = prompt('WARNING! Confirm with device ID.'); return (f.elements['deviceId'].value == '') ? false : true ; }</script>
<style>table { border-collapse: collapse; border-style: hidden; } table td { border: 1px solid black; ; padding:5px; } input:invalid { background-color: #eeccdd; }</style> </head>
<body> <h2>MVP3000 - Device ID %0%</h2>
<p><a href='/'>Home</a></p>
<h3>MQTT Communication</h3> <ul>
    <li>Status: %51% </li>
    <li>Forced external broker:<br> <form action='/save' method='post'> <input name='mqttForcedBroker' value='%53%'> <input type='submit' value='Save'> </form> </li>
    <li>MQTT port: default is 1883 (unsecure) <br> <form action='/save' method='post'> <input name='mqttPort' value='%54%' type='number' min='1024' max='65535'> <input type='submit' value='Save'> </form> </li>
    <li>Topic: <br> <form action='/save' method='post'> %55% <input name='mqttTopicSuffix' value='%56%' minlength='5'> <input type='submit' value='Save'> </form> </li> </ul>
<p>&nbsp;</body></html>
)===";
};

#endif