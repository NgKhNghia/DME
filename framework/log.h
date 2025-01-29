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
    virtual void log(int id, const std::string &logData) = 0;
};

class ConsoleLoggingMethod : public LoggingMethod {
protected:
    std::mutex consoleMutex;

public:
    void log(int id, const std::string &logData) override {
        std::unique_lock lock(consoleMutex);
        std::cout << logData << "\n";
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

    void log(int id, const std::string &logData) override {
        if (!file.is_open()) return;
        std::unique_lock lock(fileMutex);
        queue.push(logData);
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

    void log(int id, const std::string &logData) override {
        std::unique_lock<std::mutex> lock(mtx);
        queue.push(logData);
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
    std::string pointTime;

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
        pointTime = getPointTime();

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

    std::string getPointTime() {
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm* tm_info = std::localtime(&now_time);
        std::ostringstream oss;
        oss << std::put_time(tm_info, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }


    int getDuration() {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
        return static_cast<int>(duration);
    }

    /*
        type: notice, send, receive, error
        algorithm: permission, token
        time: yyyy-dd-mm hh-mm-ss
        duration_ms: ...
        souce: ...
        dest: ..., null
        direcion: a to b, broadcast, multicast
        token / permissible: yes, no
        state: wait ...
        content: ..., null
    */


    // {
    //     timeInit: YYYY-MM-DD HH-MM-SS, // thời gian khởi tạo
    //     type: notice / send / receive, // loại bản tin
    //     duration_ms: 10, // thời điểm gửi bản tin tính từ lúc khởi tạo
    //     id: 1 // nút ghi log
    //     content: ..., // nội dung giải thích chi tiết cho bản tin
    //     note: { // phần dành riêng cho ui
    //         status: ok / null // dù là loại thuật toán nào cũng chỉ cần xét xem có được vào miền găng hay không -> ví dụ: ok = tô màu nút đó, null = nút đó mất màu
    //         init: ok // chỉ dùng 1 lần duy nhất khi khời tạo, sau này sẽ không có trường init nữa
    //         error: 4 5 6 7 ... // phát hiện ra nút nào đó bị lỗi, -> có giá trị là có lỗi, nếu không có trường error thì tức là không phát hiện ra lỗi
    //         source: 1, null // nút gửi
    //         dest: 2 3 4 ..., broadcast, null // nút nhận
    //         // direction: -> bỏ, hướng luôn là từ source tới dest
    //         // những trường thông tin phía dưới sẽ tùy thuật toán mà có trường thông tin khác nhau nên có thuật toán có trường thông tin có key này, có thuật toán lại không có nên cái nào không có key thì bỏ qua
    //         // mạng hình cây (ví dụ: naimi-trehel)
    //         last: 2 // có giá trị = khởi tạo 1 mũi tên, thay đổi giá trị = thay đổi mũi tên trỏ sang nút khác, nếu last = id luôn thì bỏ mũi tên last đi
    //         next: 2 / -1 // nếu = -1 thì không có mũi tên, có giá trị thì mũi tên hướng đến nút có giá trị đó, giá trị thay đổi thì mũi tên thay đổi
    //         // permission-based
    //         // agreed: 1 2 3 4 ... / null // những nút đã đồng ý
    //     }
    // }

    void log(const std::string &type, int id, const std::string &content, json note) {
        json data;
        data["timeInit"] = pointTime;
        data["duration_ms"] = getDuration();
        data["type"] = type;
        data["id"] = id;
        data["content"] = content;
        data["note"] = note;
        std::string logData = data.dump();

        for (auto &m : methods) {
            m->log(id, logData);
        }
    }

    // void log(const std::string &type, const std::string &algorithm, int source, int dest, const std::string &direction, bool permissionOrToken, const std::string &state, const std::string &content) {
    //     json data;
    //     data["time"] = pointTime;
    //     data["duration_ms"] = getDuration();
    //     data["type"] = type;
    //     data["algorithm"] = algorithm;
    //     data["source"] = source;
    //     if (dest != -1) {
    //         data["dest"] = dest;
    //     }
    //     data["direction"] = direction;
    //     if (algorithm == "permission") {
    //         data["permissible"] = permissionOrToken ? "yes" : "no";
    //     } else if (algorithm == "token") {
    //         data["token"] = permissionOrToken ? "yes" : "no";
    //     }
    //     data["state"] = state;
    //     data["content"] = content;
    //     std::string logData = data.dump();

    //     for (auto &m : methods) {
    //         m->log(source, logData);
    //     }
    // }


    // void log(int id, int receiver = -1, const std::string &message = "") {
    //     json data;
    //     if (receiver == -1) {
    //         data["message_type"] = "notice";
    //     } else if (receiver == id) {
    //         data["message_type"] = "receive";
    //     } else if (receiver != -1) {
    //         data["message_type"] = "send";
    //     }

    //     data["time_ms"] = getTime();
    //     data["id"] = id;
    //     if (receiver != -1) {
    //         data["receiver"] = receiver;
    //     }
    //     data["message"] = message;
    //     std::string logMessage = data.dump();

    //     for (auto& m : methods) {
    //         m->log(id, receiver, logMessage);
    //     }
    // }
};






#endif // LOG_H
