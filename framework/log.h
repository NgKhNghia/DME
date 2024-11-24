// log.h
#ifndef LOG_H
#define LOG_H

#include <iostream>
#include <fstream>
#include <list>
#include <memory>
#include <string>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <sstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sys/file.h>
#include <unistd.h>
#include <chrono>

typedef nlohmann::ordered_json json;

class LoggingMethod {
protected:
    std::chrono::steady_clock::time_point startTime;

public:
    virtual ~LoggingMethod() {}
    virtual void init() {
        startTime = std::chrono::steady_clock::now();
    }
    virtual void clean() {}
    virtual void logToken(const std::string &messageType, int id, int receiver, const std::string &message) = 0;
    virtual void logPermisson(const std::string &messageType, int id, int receiver, int timeStamp, const std::string &message) = 0;
    virtual void log(const std::string &messageType, int id, const std::string &message) = 0; // log error

    std::string getElapsedDuration() {
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
    void logToken(const std::string &messageType, int id, int receiver, const std::string &message) {
        std::unique_lock lock(consoleMutex);
        std::cout << "\"messageType\": \"" << messageType << "\", "
                  << "\"durationMs\": " << getElapsedDuration() << ", "
                  << "\"node\": " << id << ", "
                  << "\"receiver\": " << (receiver == 0 ? "NULL" : std::to_string(receiver)) << ", "
                  << "\"message\": " << message
                  << std::endl;
    }

    void logPermisson(const std::string &messageType, int id, int receiver, int timeStamp, const std::string &message) override {
        std::unique_lock lock(consoleMutex);
        std::cout << "\"messageType\": \"" << messageType << "\", "
                  << "\"durationMs\": " << getElapsedDuration() << ", "
                  << "\"node\": " << id << ", "
                  << "\"receiver\": " << (receiver == 0 ? "NULL" : std::to_string(receiver)) << ", "
                  << "\"timeStamp\": " << timeStamp << ", "
                  << "\"message\": " << message
                  << std::endl;
    }

    void log(const std::string &messageType, int id, const std::string &message) override {
        std::unique_lock lock(consoleMutex);
        std::cout << "\"messageType\": \"" << messageType << "\", "
                  << "\"durationMs\": " << getElapsedDuration() << ", "
                  << "\"node\": " << id << ", "
                  << "\"message\": " << message
                  << std::endl;
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
        // file.open("log_token_ring.txt", std::ios::out | std::ios::app);
        // file.open("log_lamport.txt", std::ios::out | std::ios::app);
        file.open("log_naimi_trehel.txt", std::ios::out | std::ios::app);
        
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
                file << s.dump() << "\n";
                queue.pop_front();
            }
        } while (!closed);
    }

    ~FileLoggingMethod() override {
        closed = true;
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

    void logToken(const std::string &messageType, int id, int receiver, const std::string &message) override {
        if (!file.is_open()) return;

        std::unique_lock lock(fileMutex);
        json logData = {
            {"messageType", messageType},
            {"durationMs", getElapsedDuration()},
            {"node", id},
            {"receiver", receiver},
            {"message", message}
        };
        queue.push_back(logData);
        cond.notify_one();
    }

    void logPermisson(const std::string &messageType, int id, int receiver, int timeStamp, const std::string &message) override {
        if (!file.is_open()) return;    
        std::unique_lock lock(fileMutex);
        json logData = {
            {"messageType", messageType},
            {"durationMs", getElapsedDuration()},
            {"node", id},
            {"receiver", receiver},
            {"timeStamp", timeStamp},
            {"message", message}
        };
        queue.push_back(logData);
        cond.notify_one();
    }

    void log(const std::string &messageType, int id, const std::string &message) override {
        if (!file.is_open()) return;
        std::unique_lock lock(fileMutex);
        json logData = {
            {"messageType", messageType},
            {"durationMs", getElapsedDuration()},
            {"node", id},
            {"message", message}
        };
        queue.push_back(logData);
        cond.notify_one();
    }

};

// class MqttLoggingMethod : public LoggingMethod {















// };

class Logger {
protected:
    bool toConsole;
    bool toFile;
    bool toMqtt;
    std::list<std::shared_ptr<LoggingMethod>> methods;

public:
    Logger(bool console, bool file) : toConsole(console), toFile(file) {
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
        // if (toMqtt) methods.push_back(std::make_shared<MqttLoggingMethod>());
        reset();
    }

    void reset() {
        for (auto& m : methods) {
            m->init();
        }
    }

    void logToken(const std::string &messageType, int id, int receiver, const std::string &message) {
        for (auto& m : methods) {
            m->logToken(messageType, id, receiver, message);
        }
    }

    void logPermisson(const std::string &messageType, int id, int receiver, int timeStamp, const std::string &message) {
        for (auto& m : methods) {
            m->logPermisson(message, id, receiver, timeStamp, message);
        }
    }

    void log(const std::string &messageType, int id, const std::string &message) {
        for (auto& m : methods) {
            m->log(messageType, id, message);
        }
    }
};

extern Logger logger;





#endif // LOG_H
