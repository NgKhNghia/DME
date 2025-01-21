// naimiTrehel_v1.h
#ifndef NAIMITREHELV1_H
#define NAIMITREHELV1_H

#include "node.h"
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>

class NaimiTrehelV1 : public TokenBasedNode {
private:   
    int last;
    int next;
    bool inCS;
    bool nextUpdate;     // kiem tra xem co node nao moi request hay khong
    std::mutex mtx;
    std::condition_variable cv;
    std::thread listenerThread;

public:
    NaimiTrehelV1(int id, const std::string& ip, int port, std::shared_ptr<Comm> comm) 
        : TokenBasedNode(id, ip, port, comm), last(1), next(-1), inCS(false), nextUpdate(false) {
        hasToken = (id == 1);
        logger->log("notice", "token", id, -1, "", hasToken, "init", "node " + std::to_string(id) + " init");
    }   

    ~NaimiTrehelV1() {
        if (listenerThread.joinable()) {
            listenerThread.join();
        }
        logger->log("notice", "token", id, -1, "", hasToken, "destroy", "node " + std::to_string(id) + " destroy");
    }

    void initialize() override {
        listenerThread = std::thread(&NaimiTrehelV1::listenForMessages, this);
    }

    void requestToken() override {
        std::unique_lock<std::mutex> lock(mtx);
        if (!hasToken) {
            sendRequest(last, id);
            last = id;
            cv.wait(lock, [this] { return hasToken; });
        }
        inCS = true;
    }

    void releaseToken() override {
        inCS = false;
        if (nextUpdate) {
            sendToken(next);
            hasToken = false;
            nextUpdate = false;
        }
    }

private:
    void receiveRequest(int requesterId) {
        std::unique_lock<std::mutex> lock(mtx);
        if (last != id) {
            sendRequest(last, requesterId);
        } else if (hasToken && !inCS) {
            sendToken(requesterId);
            hasToken = false;
        } else {
            next = requesterId;
            nextUpdate = true;
        }
        last = requesterId;
    }

    void receiveToken() {
        std::unique_lock<std::mutex> lock(mtx);
        hasToken = true;
        logger->log("receive", "token", id, -1, "", hasToken, "", std::to_string(id) + " received token");
        cv.notify_one();
    }

    void sendRequest(int destId, int requesterId) {
        std::string message = "REQUEST " + std::to_string(requesterId);
        comm->send(destId, message);
        if (id == requesterId) {
            logger->log("send", "token", id, destId, std::to_string(id) + " to " + std::to_string(destId), hasToken, "sent", std::to_string(id) + " sent request to " + std::to_string(destId));
        } else {
            logger->log("send", "token", id, destId, std::to_string(id) + " to " + std::to_string(destId), hasToken, "sent", std::to_string(id) + " sent request to " + std::to_string(destId) + " for " + std::to_string(requesterId));
        }
    }

    void sendToken(int destId) {
        std::string message = "TOKEN " + std::to_string(id);
        comm->send(destId, message);
        logger->log("send", "token", id, next, std::to_string(id) + " to " + std::to_string(next), hasToken, "", std::to_string(id) + " sent token to " + std::to_string(next));
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

    void listenForMessages() {
        while (true) {
            std::string message;
            if (comm->getMessage(message)) {
                processMessage(message);
            }
        }
    }


};

#endif // NAIMITREHELV1_H
