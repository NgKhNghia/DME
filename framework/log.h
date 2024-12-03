// log.h
#ifndef LOG_H
#define LOG_H

#include <iostream>
#include <list>
// #include <memory>
// #include <string>
#include <mutex>
#include <condition_variable>
// #include <thread>
// #include <sstream>
// #include <iomanip>
#include <nlohmann/json.hpp>
// #include <sys/file.h>
// #include <unistd.h>
// #include <chrono>
#include "MQTTClient.h"

typedef nlohmann::ordered_json json;

extern Config config;

class LoggingMethod {
protected:
    std::chrono::steady_clock::time_point startTime;

public:
    virtual ~LoggingMethod() {}
    virtual void init() {
        startTime = std::chrono::steady_clock::now();
    }
    virtual void clean() {}
    virtual void log(int id, int receiver = -1, int timestamp = -1, const std::string &message = "") = 0;

    std::string getTime() {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
        std::ostringstream oss;
        oss << duration;
        return oss.str();
    }

};

class ConsoleLoggingMethod : public LoggingMethod {
protected:
    std::mutex consoleMutex;

public:
    void log(int id, int receiver = -1, int timestamp = -1, const std::string &message = "") override {
        std::unique_lock lock(consoleMutex);
        json data;
        if (receiver == -1) {
            data["message_type"] = "notice";
        } else if (receiver == id) {
            data["message_type"] = "receive";
        } else if (receiver != -1) {
            data["message_type"] = "send";
        } 
        data["time_ms"] = getTime();
        data["id"] = id;
        if (receiver != -1) {
            data["receiver"] = receiver; 
        }
        if (timestamp != -1) {
            data["timestamp"] = timestamp;
        }
        data["message"] = message;
        std::cout << data.dump() << "\n\n";
    }
};

class FileLoggingMethod : public LoggingMethod {
protected:
    std::ofstream file;
    std::list<json> queue;
    bool closed = false;
    std::mutex fileMutex;
    std::condition_variable cond;
    std::thread m_logFileThread;

public:
    void init() override {
        LoggingMethod::init();
        if (file.is_open()) file.close();
        file.open("log.txt", std::ios::out | std::ios::app);
        if (!file) {
            std::cout << "Failed to create file log\n" << std::endl;
            return;
        }
        m_logFileThread = std::thread(&FileLoggingMethod::logFileThread, this);
    }

    void logFileThread() {
        do {
            std::unique_lock lock(fileMutex);
            cond.wait(lock, [this]() { return !queue.empty() || closed; });
            while (!queue.empty()) {
                auto& s = queue.front();
                file.seekp(0, std::ios::end);
                file << s.dump() << "\n\n";
                queue.pop_front();
            }
        } while (!closed);
    }

    ~FileLoggingMethod() override {
        closed = true;
        cond.notify_all();
        if (m_logFileThread.joinable()) {
            m_logFileThread.join();
        }
        cond.notify_one();
    }

    void clean() override {
        if (file.is_open()) {
            file.close();
        }
    }

    void log(int id, int receiver = -1, int timestamp = -1, const std::string &message = "") override {
        if (!file.is_open()) return;
        std::unique_lock lock(fileMutex);
        json data;
        if (receiver == -1) {
            data["message_type"] = "notice";
        } else if (receiver == id) {
            data["message_type"] = "receive";
        } else if (receiver != -1) {
            data["message_type"] = "send";
        } 
        data["time_ms"] = getTime();
        data["id"] = id;
        if (receiver != -1) {
            data["receiver"] = receiver;
        }
        if (timestamp != -1) {
            data["timestamp"] = timestamp;
        }
        data["message"] = message;

        queue.push_back(data);
        cond.notify_one();
    }

};

class MqttLoggingMethod : public LoggingMethod {
protected:
    inline static const char* server_address = config.getServerAddressMqtt().c_str();	// 
	inline static const char* client_id = "id";
	inline static const char* topic = "log";
	inline static long timeout = 10000L;

	MQTTClient client;
public:
    MqttLoggingMethod() {
        MQTTClient_global_init(NULL);

		int rc = MQTTClient_create(&client, server_address, client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);
		if (rc != MQTTCLIENT_SUCCESS) {
			std::cout << "Failed to connect to MQTT server, return code " << rc << std::endl;
			return;
		}
    }

    ~MqttLoggingMethod() {
		MQTTClient_destroy(&client);
	}

    void init() override {
		if (MQTTClient_isConnected(client)) return;
		MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
		conn_opts.keepAliveInterval = 20;
		conn_opts.cleansession = 1;
		int rc = MQTTClient_connect(client, &conn_opts);
		if (rc != MQTTCLIENT_SUCCESS) {
			std::cout << "Failed to connect to MQTT server, return code " << rc << std::endl;
			return;
		} 

        // special message
        MQTTClient_message pubmsg = MQTTClient_message_initializer;
		MQTTClient_deliveryToken token;

        json data = json::array();
        int totalNodes = config.getTotalNodes();
        std::map<int, std::pair<std::string, int>> table = config.getNodeConfigs();
        data["total_nodes"] = totalNodes;
        for (int i = 1; i <= totalNodes; i++) {
            json obj;
            obj["id"] = i;
            obj["address"] = table[i].first;
            obj["port"] = table[i].second;
            data.push_back(obj);
        }
		pubmsg.payload = (void*)data.dump().c_str();
		pubmsg.payloadlen = data.dump().length();
		pubmsg.qos = 1; // QOS
		pubmsg.retained = 0;
		MQTTClient_publishMessage(client, topic, &pubmsg, &token);
		MQTTClient_waitForCompletion(client, token, timeout);
	}

    void clean() override {
		if (MQTTClient_isConnected(client))
			MQTTClient_disconnect(client, 10000);
	}

	void log(int id, int receiver = -1, int timestamp = -1, const std::string &message = "") override {
		if (!MQTTClient_isConnected(client)) return;

		MQTTClient_message pubmsg = MQTTClient_message_initializer;
		MQTTClient_deliveryToken token;

        json data;
        if (receiver == -1) {
            data["message_type"] = "notice";
        } else if (receiver == id) {
            data["message_type"] = "receive";
        } else if (receiver != -1) {
            data["message_type"] = "send";
        } 
        data["time_ms"] = getTime();
        data["id"] = id;
        if (receiver != -1) {
            data["receiver"] = receiver;
        }
        if (timestamp != -1) {
            data["timestamp"] = timestamp;
        }
        data["message"] = message;

		pubmsg.payload = (void*)data.dump().c_str();
		pubmsg.payloadlen = data.dump().length();
		pubmsg.qos = 1; // QOS
		pubmsg.retained = 0;
		MQTTClient_publishMessage(client, topic, &pubmsg, &token);
		MQTTClient_waitForCompletion(client, token, timeout);
    }
};

class Logger {
protected:
    bool toConsole;
    bool toFile;
    bool toMqtt;
    std::list<std::shared_ptr<LoggingMethod>> methods;

public:
    Logger(bool console, bool file, bool mqtt) : toConsole(console), toFile(file), toMqtt(mqtt) {
        init();
    }

    ~Logger() {
        for (auto& m : methods) {
            m->clean();
        }
    }

    void init() {
        if (toConsole) methods.push_back(std::make_shared<ConsoleLoggingMethod>());
        if (toFile) methods.push_back(std::make_shared<FileLoggingMethod>());
        if (toMqtt) methods.push_back(std::make_shared<MqttLoggingMethod>());
        reset();
    }

    void reset() {
        for (auto& m : methods) {
            m->init();
        }
    }

    void log(int id, int receiver = -1, int timestamp = -1, const std::string &message = "") {
        for (auto& m : methods) {
            m->log(id, receiver, timestamp, message);
        }
    }
};

extern Logger logger;





#endif // LOG_H
