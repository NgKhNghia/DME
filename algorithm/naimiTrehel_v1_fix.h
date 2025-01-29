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
    std::mutex mtxMsg;
    std::condition_variable cv;

    std::thread listenerThread;

public:
    NaimiTrehelV1(int id, const std::string& ip, int port, std::shared_ptr<Comm> comm) 
        : TokenBasedNode(id, ip, port, comm), last(1), next(-1), inCS(false), nextUpdate(false) {
        hasToken = (id == 1);

        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["init"] = "ok";
        note["error"] = "null";
        note["source"] = "null";
        note["dest"] = "null";
        note["last"] = last;
        note["next"] = next;
        logger->log("notice", id, std::to_string(id) + " init", note);

        initialize();
    }   

    ~NaimiTrehelV1() {
        if (listenerThread.joinable()) {
            listenerThread.join();
        }
    }

    void requestToken() override {
        std::unique_lock<std::mutex> lock(mtx);

        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = "null";
        note["dest"] = "null";
        note["last"] = last;
        note["next"] = next;
        logger->log("notice", id, std::to_string(id) + " request token", note);

        if (!hasToken) {
            sendRequest(last, id);
            last = id;
            cv.wait(lock, [this] { return hasToken; });
        }
        // inCS = true;
    }

    void releaseToken() override {
        std::unique_lock<std::mutex> lock(mtx);
        inCS = false;
        if (nextUpdate) {
            sendToken(next);
            hasToken = false;
            nextUpdate = false;
        }
    }

private:
    void initialize() override {
        listenerThread = std::thread(&NaimiTrehelV1::listenForMessages, this);
    }

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

    void receiveToken(int source) {
        std::unique_lock<std::mutex> lock(mtx);
        hasToken = true;

        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = next;
        logger->log("receive", id, std::to_string(id) + " received token", note);

        cv.notify_one();
    }

    void sendRequest(int destId, int requesterId) {
        std::string message = "REQUEST " + std::to_string(requesterId);

        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = destId;
        note["last"] = requesterId;
        note["next"] = next;
        if (id == requesterId) {
            logger->log("send", id, std::to_string(id) + " sent request to " + std::to_string(destId), note);
        } else {
            logger->log("send", id, std::to_string(id) + " sent request to " + std::to_string(destId) + " for " + std::to_string(requesterId), note);
        }
        
        comm->send(destId, message);
    }

    void sendToken(int destId) {

        std::string message = "TOKEN " + std::to_string(id);

        json note;
        note["status"] = "null";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = destId;
        note["last"] = last;
        note["next"] = next;
        logger->log("send", id, std::to_string(id) + " sent token to " + std::to_string(destId), note);

        comm->send(destId, message);
    }

    void processMessage(const std::string& message) {
        std::istringstream iss(message);
        std::string msgType;
        int senderId;
        iss >> msgType >> senderId;

        if (msgType == "REQUEST") {
            receiveRequest(senderId);
        } else if (msgType == "TOKEN") {
            receiveToken(senderId);
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
