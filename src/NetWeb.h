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

#ifndef MVP3000_NETWEB
#define MVP3000_NETWEB

#include <Arduino.h>

#include <ESPAsyncWebServer.h>

#include "ESPX.h"
#ifdef ESP8266
    extern EspClass ESPX;
#else
    extern EspClassX ESPX;
#endif

#include "Config_JsonInterface.h"


class NetWeb {
    public:
        void setup();
        void loop();

        /**
         * @brief Register a new page for the web interface.
         * 
         * @param uri The URI of the page.
         * @param html The HTML content of the page, used together with the processor to fill the template.
         * @param processor The processor function for the page.
         * @param responseFiller The response filler function for the page, typically used for large pages or datasets.
         * @param contentType The content type of the page, optional, Default is "text/html".
         */
        void registerPage(String uri, const char* html, AwsTemplateProcessor processor, String contentType = "text/html");
        void registerPage(String uri, AwsResponseFiller responseFiller, String contentType = "text/html");

        /**
         * @brief Register a configuration interface to make its settings editable using a form on the web interface.
         * 
         * @param Cfg The configuration to add.
         */
        void registerCfg(CfgJsonInterface* Cfg);

        /**
         * @brief Register an action to be executed using a form on the web interface.
         * 
         * @param action The action to add.
         * @param actionFkt The function to execute.
         * @param successMessage (optional) The message to display on success. Leave empty to not display a message. If omitted, the device will restart itself after the action is completed
         */
        void registerAction(String action, std::function<bool(int, std::function<String(int)>, std::function<String(int)>)> actionFkt);
        void registerAction(String action, std::function<bool(int, std::function<String(int)>, std::function<String(int)>)> actionFkt, String successMessage);

        /**
         * @brief Register a websocket to be used with the web interface.
         * 
         * @param uri The URI of the websocket.
         * @param dataCallback The function to execute when data is received.
         * @return Returns the function to write data to the websocket.
         */
        std::function<void(const String &message)> registerWebSocket(String uri) { return registerWebSocket(uri, nullptr); };
        std::function<void(const String &message)> registerWebSocket(String uri, std::function<void(char*)> dataCallback);        // should return a function to write data to the websocket

    private:

        // Linked list for action callbacks
        struct WebActionList {
            enum ResponseType {
                NONE = 0,
                MESSAGE = 1,
                RESTART = 2
            };

            struct Node {
                String action;
                ResponseType successResponse;
                String successMessage;
                std::function<bool(int, std::function<String(int)>, std::function<String(int)>)> actionFkt;
                
                Node* next;

                Node() { }
                Node(String _action, ResponseType _successResponse, std::function<bool(int, std::function<String(int)>, std::function<String(int)>)> _actionFkt, String _successMessage) : action(_action), successResponse(_successResponse), actionFkt(_actionFkt), successMessage(_successMessage) { }
            };

            Node* head = nullptr;


            void add(String action, ResponseType successResponse, std::function<bool(int, std::function<String(int)>, std::function<String(int)>)> actionFkt, String successMessage) {
                Node* newNode = new Node(action, successResponse, actionFkt, successMessage);
                newNode->next = head;
                head = newNode;
            }

            // Loops through all cfgs and updates the value if found
            Node* loopActions(int args, std::function<String(int)> argKey, std::function<String(int)> argValue) {
                Node* current = head;
                // Loop through all nodes
                while (current != nullptr) {
                    // Check if key matches and execute action
                    if (argKey(0) == current->action) {
                        if (current->actionFkt(args, argKey, argValue)) {
                            return current;
                        } else {
                            return nullptr;
                        }
                    }
                    current = current->next;
                }
                return nullptr;
            }
        };

        // Linked list for configuration objects
        struct WebCfgList {
            struct Node {
                CfgJsonInterface* Cfg;
                Node* next;
            };
            Node* head = nullptr;

            std::function<void(CfgJsonInterface&)> saveCfgFkt; // Function to save the configuration

            WebCfgList() { }
            WebCfgList(std::function<void(CfgJsonInterface&)> saveCfgFkt) {
                this->saveCfgFkt = saveCfgFkt;
            }

            void add(CfgJsonInterface* Cfg) {
                Node* newNode = new Node;
                newNode->Cfg = Cfg;
                newNode->next = head;
                head = newNode;
            }

            // Loops through all cfgs and updates the value if found
            bool loopUpdateSingleValue(String key, String value) {
                Node* current = head;
                // Loop through all nodes
                while (current != nullptr) {
                    // Try to update value, if successful save Cfg and return
                    if (current->Cfg->updateSingleValue(key, value)) {
                        saveCfgFkt(*current->Cfg);
                        return true;
                    }
                    current = current->next;
                }
                return false;
            }
        };

        // Collection of web pages
        struct WebPageColl {
            struct Node {
                String uri;
                const char* html;
                String contentType;
                AwsResponseFiller responseFiller;
                AwsTemplateProcessor processor;

                Node () { }
                Node(String _uri, const char* _html, String _contentType, AwsTemplateProcessor _processor, AsyncWebServer *server) : uri(_uri), html(_html), contentType(_contentType), processor(_processor) { 
                    responseFiller = std::bind(&Node::htmlTemplateResponseFiller, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
                    attach(server);
                }
                Node(String _uri, String _contentType, AwsResponseFiller _responseFiller, AsyncWebServer *server) : uri(_uri), html(""), contentType(_contentType), responseFiller(_responseFiller), processor(nullptr) {
                    attach(server);
                }

                void attach(AsyncWebServer *server) {
                    server->on(uri.c_str(), HTTP_GET, [&](AsyncWebServerRequest *request) {
                        request->sendChunked(contentType, responseFiller, processor);
                    });
                }

                size_t htmlTemplateResponseFiller(uint8_t *buffer, size_t maxLen, size_t index) {
                    // Chunked response filler for the html template
                    size_t len = strlen(html);
                    if (index + maxLen > len) {
                        maxLen = len - index;
                    }
                    memcpy(buffer, html + index, maxLen);
                    return maxLen;
                }
            };

            static const uint8_t nodesSize = 10;
            uint8_t nodeCount = 0;

            Node* nodes[nodesSize];

            AsyncWebServer *server;


            WebPageColl(AsyncWebServer *_server) : server(_server) { }


            bool add(String uri, const char* _html, AwsTemplateProcessor processor, String contentType) {
                if (nodeCount >= nodesSize) {
                    return false;
                }
                nodes[nodeCount++] = new Node(uri, _html, contentType, processor, server);
                return true;
            }
            bool add(String uri, AwsResponseFiller responseFiller, String contentType) {
                if (nodeCount >= nodesSize) {
                    return false;
                }
                nodes[nodeCount++] = new Node(uri, contentType, responseFiller, server);
                return true;
            }
        };

        // Collection of websockets
        struct WebSocketColl {
            struct Node {
                AsyncWebSocket* websocket;

                std::function<void(char*)> dataCallback;
                std::function<void(AsyncWebSocketClient *, AwsEventType)> webSocketEventLog;

                Node() { }
                Node(String uri, std::function<void(char*)> _dataCallback, std::function<void(AsyncWebSocketClient *, AwsEventType)> _webSocketEventLog, AsyncWebServer *server) : dataCallback(_dataCallback), webSocketEventLog(_webSocketEventLog) {
                    websocket = new AsyncWebSocket(uri);
                    // Event log and custom handerl
                    websocket->onEvent([&](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
                        // General event log
                        webSocketEventLog(client, type);
                        // Custom callback for data input from websocket
                        if ((type == WS_EVT_DATA) && (dataCallback != nullptr)) { // Only parse data if there is something to do
                            AwsFrameInfo *info = (AwsFrameInfo*)arg;
                            if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                                data[len] = 0; // Terminate string
                                // Execute callback
                                dataCallback((char*)data);
                            }
                        }
                    });
                    // Attach
                    server->addHandler(websocket);
                }

                std::function<void(const String &message)> getTextAll() { return std::bind(&Node::textAll, this, std::placeholders::_1); }
                void textAll(const String &message) {
                    websocket->textAll(message);
                }
            };

            static const uint8_t nodesSize = 3;
            uint8_t nodeCount = 0;

            Node* nodes[nodesSize];

            AsyncWebServer *server;
            std::function<void(AsyncWebSocketClient *, AwsEventType)> webSocketEventLog;


            WebSocketColl(AsyncWebServer *_server, std::function<void(AsyncWebSocketClient *, AwsEventType)> _webSocketEventLog) : server(_server), webSocketEventLog(_webSocketEventLog) { }


            bool add(String uri, std::function<void(char*)> dataCallback) {
                if (nodeCount >= nodesSize) {
                    return false;
                }
                nodes[nodeCount++] = new Node(uri, dataCallback, webSocketEventLog, server);
                return true;
            }

            std::function<void(const String &message)> getTextAll() {
                return (nodeCount > 0) ? nodes[nodeCount - 1]->getTextAll() : nullptr;
            }
        };


        AsyncWebServer server = AsyncWebServer(80);

        // Message to serve on next page load after form save
        const char *postMessage = "";

        WebActionList webActionList;

        WebCfgList webCfgList;

        WebPageColl webPageColl = WebPageColl(&server);

        void webSocketEventLog(AsyncWebSocketClient *client, AwsEventType type);
        WebSocketColl webSocketColl = WebSocketColl(&server, std::bind(&NetWeb::webSocketEventLog, this, std::placeholders::_1, std::placeholders::_2));

        // Handle form action (post)
        void editCfg(AsyncWebServerRequest *request);
        void startAction(AsyncWebServerRequest *request);
        bool formInputCheck(AsyncWebServerRequest *request);

        void responseRedirect(AsyncWebServerRequest *request, const char* message = "");
        void responsePrepareRestart(AsyncWebServerRequest *request);

        String webPageProcessor(const String& var);
        char* webPage = R"===(
<!DOCTYPE html> <html lang='en'>
<head> <title>MVP3000 - Device ID %0%</title>
    <script>function promptId(f) { f.elements['deviceId'].value = prompt('WARNING! Confirm with device ID.'); return (f.elements['deviceId'].value == '') ? false : true ; }</script>
    <style>table { border-collapse: collapse; border-style: hidden; } table td { border: 1px solid black; ; padding:5px; } input:invalid { background-color: #eeccdd; }</style> </head>
<body> <h2>MVP3000 - Device ID %0%</h2> <h3 style='color: red;'>%1%</h3>
    <h3>System</h3> <ul>
        <li>ID: %0%</li>
        <li>Build: %2%</li>
        <li>Memory: %3%, fragmentation %4%&percnt;</li>
        <li>Uptime: %5%</li>
        <li>Last restart reason: %6%</li>
        <li>CPU frequency: %7% MHz</li>
        <li>Main loop duration: %8% ms (mean/min/max)</li> </ul>
    <h3>Components</h3> <ul>
        <li><a href='/net'>Network</a></li>
        <li>%11% </ul>
    <h3>Modules</h3> <ul>
        %21% </ul>
    <h3>Maintenance</h3> <ul>
        <li> <form action='/start' method='post' onsubmit='return confirm(`Restart?`);'> <input name='restart' type='hidden'> <input type='submit' value='Restart' > </form> </li>
        <li> <form action='/checkstart' method='post' onsubmit='return promptId(this);'> <input name='reset' type='hidden'> <input name='deviceId' type='hidden'> <input type='submit' value='Factory reset'> <input type='checkbox' name='keepwifi' checked value='1'> keep Wifi </form> </li> </ul>
<p>&nbsp;</body></html>
)===";

};

#endif