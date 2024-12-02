// naimiTrehel.h
#ifndef NAIMITREHEL_H
#define NAIMITREHEL_H

#include "node.h"
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>

class NaimiTrehel : public TokenBasedNode {
private:   
    int last;
    int next;
    bool inCS;
    std::mutex mtx;
    std::condition_variable cv;
    std::thread listenerThread;

public:
    NaimiTrehel(int id, const std::string& ip, int port, std::shared_ptr<Comm> comm) 
        : TokenBasedNode(id, ip, port, comm), last(1), next(-1), inCS(false) {
        token = (id == 1);
        logger.log(id, -1, -1, -1, "init");
    }   

    ~NaimiTrehel() {
        if (listenerThread.joinable()) {
            listenerThread.join();
        }
        logger.log(id, -1, -1, -1, "destroy");
    }

    void initialize() override {
        listenerThread = std::thread(&NaimiTrehel::listenForMessages, this);
    }

    void requestToken() override {
        std::unique_lock<std::mutex> lock(mtx);
        if (!token) {
            sendRequest(last, id);
            last = id;
            cv.wait(lock, [this] { return token; });
        }
        inCS = true;
    }

    void releaseToken() override {
        inCS = false;
        if (next != -1) {
            sendToken(next);
            token = false;
            next = -1;
        }
    }

    void receiveRequest(int requesterId) {
        std::unique_lock<std::mutex> lock(mtx);
        if (last != id) {
            sendRequest(last, requesterId);
        } else if (token && !inCS) {
            sendToken(requesterId);
            token = false;
        } else {
            next = requesterId;
        }
        last = requesterId;
    }

    void receiveToken() {
        std::unique_lock<std::mutex> lock(mtx);
        token = true;
        cv.notify_one();
    }

private:
    void sendRequest(int destId, int requesterId) {
        std::string message = "REQUEST " + std::to_string(requesterId);
        comm->send(destId, message);
        if (id == requesterId) {
            logger.log(id, -1, destId, -1, "token request");
        } else {
            logger.log(id, -1, destId, -1, "send token request for requester " + std::to_string(requesterId));
        }
    }

    void sendToken(int destId) {
        std::string message = "TOKEN " + std::to_string(id);
        comm->send(destId, message);
        logger.log(id, -1, destId, -1, "send token to " + std::to_string(destId));
    }

    void processMessage(const std::string& message) {
        std::istringstream iss(message);
        std::string msgType;
        int senderId;
        iss >> msgType >> senderId;

        if (msgType == "REQUEST") {
            receiveRequest(senderId);
        } else if (msgType == "TOKEN") {
            receiveToken();
        }
    }

public:
    void listenForMessages() {
        while (true) {
            std::string message;
            if (comm->getMessage(message)) {
                processMessage(message);
            }
        }
    }


};

#endif // NAIMITREHEL_H
