// tokenring.h
#ifndef TOKENRING_H
#define TOKENRING_H

#include "node.h"
#include "comm.h"
#include "log.h"
#include <thread>
#include <random>
#include <condition_variable>
#include <mutex>

extern Config config;
extern Logger logger;

class TokenRingNode : public TokenBasedNode {
private:
    bool token;
    bool isRequestToken;    
    int nextNode;  
    std::mutex mtx;
    std::condition_variable cv;
    std::thread receiveTokenThread;

public:
    TokenRingNode(int id, const std::string& ip, int port, std::shared_ptr<Comm<std::string>> comm)
        : TokenBasedNode(id, ip, port, comm), isRequestToken(false) {
        findNeighborNode();
        initialize();  
    }

    ~TokenRingNode() {
        if (receiveTokenThread.joinable()) {
            receiveTokenThread.join();
        }
        logger.log("NOTI", id, "destroy");
    }

    void initialize() override {
        logger.log("NOTI", id, "init");
        if (id == 1) {
            acquireToken();  
        }
        std::thread(&TokenRingNode::receiveTokenLoop, this);
    }

    void findNeighborNode() {
        nextNode = id % config.getTotalNodes() + 1;  
    }

    void requestToken() override {
        std::unique_lock<std::mutex> lock(mtx);
        if (!isRequestToken) {
            logger.logToken("SEND", id, nextNode, "request token");
            isRequestToken = true;
            cv.wait(lock, [this] { return token; });
        }
    }

    void releaseToken() override {
        std::unique_lock<std::mutex> lock(mtx);
        token = false;
        isRequestToken = false;
        comm->send(nextNode, "TOKEN");  
        logger.logToken("SEND", id, nextNode, "send token");
    }

    void receiveToken() {
        std::string message;
        if (comm->getMessage(message)) {
            if (message == "TOKEN") {
                acquireToken();  
            }
        }
    }

private:
    void acquireToken() {
        token = true;
        logger.log("NOTI", id, "has token");
        cv.notify_one();
    }

    void receiveTokenLoop() {
        while (true) {
            receiveToken();  
        }
    }
};

#endif // TOKENRING_H

