// naimiTrehel_v2.h
#ifndef NAIMITREHELV2_H
#define NAIMITREHELV2_H

#include "node.h"
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>

class NaimiTrehelV2 : public TokenBasedNode {
private:   
    int last;                           // node cuoi cung se truy cap CS
    int next;                           // node tiep theo se truy cap CS
    bool nextUpdate;                    // kiem tra xem co node nao moi request hay khong
    bool hasRequest;                    // da gui yeu cau hay chua
    bool intoCS;                        // co the vao hoac dang trong CS 
    bool hasRespond;                    // da nhan duoc phan hoi sau khi gui CONSULT
    bool hasExsit;                      // da nhan duoc phan hoi sau khi gui EXSIT
    bool stopExtension;                 // dung gui CONSULT...
    std::map<int, bool> electedId;      // luu tru nhung node yeu cau tai tao token
    std::mutex mtx;
    std::condition_variable cv;
    std::thread listenerThread;

    const std::chrono::seconds T_wait{5};
    const std::chrono::seconds T_elec{5};

public:
    NaimiTrehelV2(int id, const std::string& ip, int port, std::shared_ptr<Comm> comm) 
        : TokenBasedNode(id, ip, port, comm), last(1), next(-1), intoCS(false), nextUpdate(false), stopExtension(false) {
        hasToken = (id == 1);
        logger->log("notice", "token", id, -1, "", hasToken, "init", "node " + std::to_string(id) + " init");
    }   

    ~NaimiTrehelV2() {
        if (listenerThread.joinable()) {
            listenerThread.join();
        }
        logger->log("notice", "token", id, -1, "", hasToken, "destroy", "node " + std::to_string(id) + " destroy");
    }

    void initialize() override {
        listenerThread = std::thread(&NaimiTrehelV2::listenForMessages, this);
    }

    void requestToken() override {
        {
            std::unique_lock<std::mutex> lock(mtx);
            hasRequest = false;
        }
        while (!hasToken) {
            {
                std::unique_lock<std::mutex> lock(mtx);
                if (!hasRequest) {
                    sendRequest(last, id);
                    last = id;
                    hasRequest = true;
                }
                if (cv.wait_for(lock, std::chrono::seconds(T_wait), [this]() { return hasToken || stopExtension; })){
                    if (hasToken) break;
                    else if (stopExtension) continue;
                }
            }
            stopExtension = false; 
            sendConsult();
            if (hasToken) break;
        }

        std::unique_lock<std::mutex> lock(mtx);
        intoCS = true;
    }

    void releaseToken() override {
        std::unique_lock<std::mutex> lock(mtx);
        intoCS = false;
        hasRequest = false;
        if (nextUpdate) {
            sendToken(next);
            hasToken = false;
            nextUpdate = false;
        }
    }

private:
    void processMessage(const std::string& message) {
        std::istringstream iss(message);
        std::string msgType;
        int senderId;
        iss >> msgType >> senderId;

        if (msgType == "REQUEST") {
            receiveRequest(senderId);
        } else if (msgType == "TOKEN") {
            receiveToken(senderId);
        } else if (msgType == "CONSULT") {
            receiveConsult(senderId);
        } else if (msgType == "RESPOND") { // respond for consult -> nut lien truoc van ton tai
            receiveRespond(senderId);
        } else if (msgType == "FAILURE") { 
            receiveFailure(senderId);
        } else if (msgType == "EXSIT") { // respond for failure -> token van ton tai
            receiveExsit(senderId);
        } else if (msgType == "ELECTION") {
            receiveElection(senderId);
        } else if (msgType == "ELECTED") {
            receiveElected(senderId);
        }
    }

    void sendToken(int destId) {
        std::string message = "TOKEN " + std::to_string(id);
        comm->send(destId, message);
        logger->log("send", "token", id, next, std::to_string(id) + " to " + std::to_string(next), hasToken, "", std::to_string(id) + " sent token to " + std::to_string(next));
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

    // kiem tra node lien truoc con song hay khong
    void sendConsult() { 
        {
            std::unique_lock<std::mutex> lock(mtx);
            hasRespond = false;
            for (int i = 1; i <= config.getTotalNodes(); i++) {
                if (i != id) {
                    comm->send(i, "CONSULT " + std::to_string(id));
                }
            }
            // logger->log(id, -1, std::to_string(id) + " send broadcast CONSULT");
            logger->log("send", "token", id, -1, "broadcast", hasToken, "sent", std::to_string(id) + " send broadcast CONSULT");

            if (cv.wait_for(lock, T_elec, [this]() { return hasRespond || hasToken || stopExtension; })) {
                return; 
            }
        } 
        sendFailure();
    }

    // kiem tra con token hay khong
    void sendFailure() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            hasExsit = false;
            for (int i = 1; i <= config.getTotalNodes(); i++) {
                if (i != id) {
                    comm->send(i, "FAILURE " + std::to_string(id));
                }
            }
            logger->log("send", "token", id, -1, "broadcast", hasToken, "sent", std::to_string(id) + " send broadcast FAILURE");


            if (cv.wait_for(lock, T_elec, [this]() { return hasExsit || hasToken || stopExtension; })) {
                return; 
            }
        } 
        sendElection();
    }

    // phat ban tin ung cu token
    void sendElection() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            electedId[id] = true;
            for (int i = 1; i <= config.getTotalNodes(); i++) {
                if (i != id) {
                    comm->send(i, "ELECTION " + std::to_string(id));
                }
            }
            logger->log("send", "token", id, -1, "broadcast", hasToken, "sent", std::to_string(id) + " send broadcast ELECTION");
            
            if (cv.wait_for(lock, T_elec, [this]() { return hasToken || stopExtension; })) {
                return;
            }
        }
        regenerateToken();
    }

    void regenerateToken() {
        int minElecterId = id;
        for (const auto &it : electedId) {
            if (it.first < minElecterId) minElecterId = it.first;
        }
        if (id == minElecterId) {
            {
                std::unique_lock<std::mutex> lock(mtx);
                hasToken = true;
                nextUpdate = false;
                last = id;
                next = -1;
                electedId.clear();
                cv.notify_one();
                logger->log("send", "token", id, -1, "", hasToken, "broadcast", std::to_string(id) + " regenerated token");

            }

            for (int i = 1; i <= config.getTotalNodes(); i++) {
                if (i != id) {
                    comm->send(i, "ELECTED " + std::to_string(id));
                }
            }
            logger->log("send", "token", id, -1, "", hasToken, "broadcast", std::to_string(id) + " broadcast ELECTED");
        }
    }

    void receiveToken(int senderID) {
        std::unique_lock<std::mutex> lock(mtx);
        hasToken = true;
        logger->log("receive", "token", senderID, id, std::to_string(senderID) + " to " + std::to_string(id), hasToken, "", std::to_string(id) + " received token from " + std::to_string(senderID));
        cv.notify_one();
    }

    void receiveRequest(int requesterId) {
        std::unique_lock<std::mutex> lock(mtx);
        logger->log("receive", "token", requesterId, id, std::to_string(requesterId) + " to " + std::to_string(id), hasToken, "", std::to_string(id) + " received request from " + std::to_string(requesterId));
        if (id != last) {
            sendRequest(last, requesterId);
        } else if (hasToken && !intoCS) {
            sendToken(requesterId);
            hasToken = false;
        } else {
            next = requesterId;
            nextUpdate = true;
        }
        last = requesterId;
    }

    void receiveConsult(int senderId) {
        if (next == senderId) {
            logger->log("receive", "token", senderId, id, std::to_string(senderId) + " to " + std::to_string(id), hasToken, "", std::to_string(id) + " received RESPONE from " + std::to_string(senderId));
            comm->send(senderId, "RESPOND " + std::to_string(id));
            logger->log("send", "token", id, senderId, std::to_string(id) + " to " + std::to_string(senderId), hasToken, "", std::to_string(id) + " sent RESPONE to " + std::to_string(senderId));
        }
    }

    void receiveRespond(int senderId) {
        std::unique_lock<std::mutex> lock(mtx);
        hasRespond = true;
        logger->log("send", "token", senderId, id, std::to_string(senderId) + " to " + std::to_string(id), hasToken, "", std::to_string(id) + " received RESPONE from " + std::to_string(senderId));
        cv.notify_one();
    }

    void receiveFailure(int senderId) {
        if (hasToken) {
            logger->log("receive", "token", senderId, id, std::to_string(senderId) + " to " + std::to_string(id), hasToken, "", std::to_string(id) + " received EXSIT from " + std::to_string(senderId));
            comm->send(senderId, "EXSIT " + std::to_string(id));
            logger->log("send", "token", id, senderId, std::to_string(id) + " to " + std::to_string(senderId), hasToken, "", std::to_string(id) + " sent EXSIT to " + std::to_string(senderId));
        }
    }

    void receiveExsit(int senderId) {
        std::unique_lock<std::mutex> lock(mtx);
        hasExsit = true;
        logger->log("receive", "token", senderId, id, std::to_string(senderId) + " to " + std::to_string(id), hasToken, "", std::to_string(id) + " received ESXIT from " + std::to_string(senderId));
        cv.notify_one();
    }

    void receiveElection(int senderId) {
        std::unique_lock<std::mutex> lock(mtx);
        electedId[senderId] = true;
        logger->log("receive", "token", senderId, id, std::to_string(senderId) + " to " + std::to_string(id), hasToken, "", std::to_string(id) + " received ELECTION from " + std::to_string(senderId));
    }

    void receiveElected(int senderId) {
        logger->log("receive", "token", senderId, id, std::to_string(senderId) + " to " + std::to_string(id), hasToken, "", std::to_string(id) + " received ELECTED from " + std::to_string(senderId));
        {
            std::unique_lock<std::mutex> lock(mtx);
            next = -1;
            last = senderId;
            hasRequest = false;
            hasToken = false;
            hasRespond = false;
            hasExsit = false;
            stopExtension = true;
            electedId.clear();
            cv.notify_all();
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

#endif // NAIMITREHELV2_H
