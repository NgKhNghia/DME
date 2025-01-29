// tokenring.h
#ifndef TOKENRING_H
#define TOKENRING_H

#include "node.h"
// #include "comm.h"
#include "log.h"
#include <thread>
#include <random>
#include <condition_variable>
#include <mutex>

// extern Config config;
// extern Logger* logger;

class TokenRing : public TokenBasedNode {
private:
    int next;
    int totalNodes;
    bool needToken;

    std::mutex mtx;
    std::mutex mtxMsg;
    std::condition_variable cv;

    std::thread receiveThread;

public:
    TokenRing(int id, const std::string& ip, int port, std::shared_ptr<Comm> comm) : TokenBasedNode(id, ip, port, comm), needToken(false) {
        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["init"] = "ok";
        note["error"] = "null";
        note["source"] = "null";
        note["dest"] = "null";
        logger->log("notice", id, std::to_string(id) + " init", note);
        initialize();  
    }

    ~TokenRing() {
        if (receiveThread.joinable()) {
            receiveThread.join();
        }
    }

    void requestToken() override {
        std::unique_lock<std::mutex> lock(mtx);
        needToken = true;

        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = "null";
        note["dest"] = "null";
        logger->log("notice", id, std::to_string(id) + " request token", note);

        cv.wait(lock, [this] { return hasToken; });
    }

    void releaseToken() override {
        std::unique_lock<std::mutex> lock(mtx);
        needToken = false;
        json note;
        note["status"] = "ok";
        note["error"] = "null";
        note["source"] = "null";
        note["dest"] = "null";
        logger->log("notice", id, std::to_string(id) + " release token", note);

        sendToken();
    }

private:
    void initialize() override {
        totalNodes = config.getTotalNodes();
        next = id % totalNodes + 1;
        hasToken = (id == 1) ? true : false;

        receiveThread = std::thread(&TokenRing::receiveMsg, this);
    }

    void sendToken() {
        std::unique_lock<std::mutex> lock(mtxMsg);
        hasToken = false;

        json note;
        note["status"] = "null";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = next;
        logger->log("send", id, std::to_string(id) + " send token to " + std::to_string(next), note);

        comm->send(next, std::to_string(id) + " TOKEN");
    }

    void receivedToken(int source) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        
        json note;
        note["status"] = "ok";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        logger->log("recieve", id, std::to_string(id) + " received token from " + std::to_string(source), note);

        hasToken = true;
        if (needToken) {
            cv.notify_one();
        } else {
            sendToken();
        }
    }

    void processMsg(const std::string &msg) {
        std::istringstream iss(msg);
        int id;
        std::string content;
        iss >> id >> content;
        if (content == "TOKEN") {
            receivedToken(id);
        }
    }

    void receiveMsg() {
        std::string msg;
        while (true) {
            if (comm->getMessage(msg)) {
                processMsg(msg);
            }
        }
    }
};

#endif // TOKENRING_H

