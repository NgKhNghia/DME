// log.h
#ifndef LOG_H
#define LOG_H

#include <iostream>
#include <list>
#include <mutex>
#include <condition_variable>
#include <nlohmann/json.hpp>
#include "mqtt/async_client.h"

typedef nlohmann::ordered_json json;


class LoggingMethod {
public:
    ~LoggingMethod() = default;
    virtual void init() {}
    virtual void clean() {}
    virtual void log(int id, int receiver = -1, const std::string &message = "") = 0;
};

class ConsoleLoggingMethod : public LoggingMethod {
protected:
    std::mutex consoleMutex;

public:
    void log(int id, int receiver = -1, const std::string &message = "") override {
        std::unique_lock lock(consoleMutex);
        std::cout << message << "\n";
    }
};

class FileLoggingMethod : public LoggingMethod {
protected:
    std::ofstream file;
    std::queue<std::string> queue;
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
                file << s << "\n";
                queue.pop();
            }
        } while (!closed);
    }

    ~FileLoggingMethod() {
        clean();
    }

    void clean() override {
        {
            closed = true;
            std::unique_lock<std::mutex> lock(fileMutex);
        }
        cond.notify_one();
        if (m_logFileThread.joinable()) {
            m_logFileThread.join();
        }
        if (file.is_open()) {
            file.close();
        }
    }

    void log(int id, int receiver = -1, const std::string &message = "") override {
        if (!file.is_open()) return;
        std::unique_lock lock(fileMutex);
        queue.push(message);
        cond.notify_one();
    }
};



class MqttLoggingMethod : public LoggingMethod {
private:
    mqtt::async_client mqttClient;
    mqtt::connect_options connOpts;
    std::string topic = "test_dme";
    std::queue<std::string> queue;
    std::mutex mtx;
    std::condition_variable cond;
    std::thread mqttThread;
    bool stop = false;

public:
    // note: client_id.length >= 1 -> else: error
    MqttLoggingMethod(int id) : mqttClient(config.getBrokerAddressMqtt(), "mqtt_publisher_" + std::to_string(id)) {}


    ~MqttLoggingMethod() {
        clean();
    }

    void init() override {
        LoggingMethod::init();
        connOpts.set_keep_alive_interval(60);
        connOpts.set_clean_session(true);
        mqttClient.connect(connOpts)->wait();

        mqttThread = std::thread(&MqttLoggingMethod::processLogs, this);
    }

    void log(int id, int receiver = -1, const std::string &message = "") override {
        std::unique_lock<std::mutex> lock(mtx);
        queue.push(message);
        cond.notify_one();
    }

    void processLogs() {
        while (true) {
            std::string logMessage;
            {
                std::unique_lock<std::mutex> lock(mtx);
                cond.wait(lock, [this]() { return !queue.empty() || stop; });
                if (stop && queue.empty()) {
                    break;
                }
                logMessage = queue.front();
                queue.pop();
            }
            mqtt::message_ptr pubmsg = mqtt::make_message(topic, logMessage);
            pubmsg->set_qos(1);
            mqttClient.publish(pubmsg)->wait();
        }
    }

    void clean() override {
        {
            std::unique_lock<std::mutex> lock(mtx);
            stop = true;
        }
        cond.notify_one();
        if (mqttThread.joinable()) {
            mqttThread.join();
        }
        mqttClient.disconnect()->wait();
    }
};

class Logger {
protected:
    int id;
    bool toConsole;
    bool toFile;
    bool toMqtt;
    std::list<std::shared_ptr<LoggingMethod>> methods;
    std::chrono::steady_clock::time_point startTime;

public:
    Logger(int id, bool console, bool file, bool mqtt) : id(id), toConsole(console), toFile(file), toMqtt(mqtt) {
        init();
    }

    ~Logger() {
        for (auto& m : methods) {
            m->clean();
        }
    }

    void init() {
        startTime = std::chrono::steady_clock::now();
        
        if (toConsole) methods.push_back(std::make_shared<ConsoleLoggingMethod>());
        if (toFile) methods.push_back(std::make_shared<FileLoggingMethod>());
        if (toMqtt) methods.push_back(std::make_shared<MqttLoggingMethod>(id));
        reset();
    }

    void reset() {
        for (auto& m : methods) {
            m->init();
        }
    }

    int getTime() {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
        return static_cast<int>(duration);
    }

    void log(int id, int receiver = -1, const std::string &message = "") {
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
        data["message"] = message;
        std::string logMessage = data.dump();

        for (auto& m : methods) {
            m->log(id, receiver, logMessage);
        }
    }
};






#endif // LOG_H
