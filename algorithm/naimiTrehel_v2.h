// naimiTrehel_v2.h
#ifndef NAIMITREHELV2_H
#define NAIMITREHELV2_H

#include "node.h"
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>

class NaimiTrehel : public TokenBasedNode {
private:   
    int last;
    int next;
    bool nextUpdate;                    // kiem tra xem co node nao moi request hay khong
    bool hasRequest;                    // da gui yeu cau hay chua
    bool inCS;                          // co the vao hoac dang trong CS 
    bool hasRespond;                    // da nhan duoc phan hoi sau khi gui CONSULT
    bool hasExsit;                      // da nhan duoc phan hoi sau khi gui EXSIT
    bool stopExtension;                 // dung luong gui CONSULT...
    std::map<int, bool> electedId;      // luu tru nhung node yeu cau tai tao token
    std::mutex mtx;
    std::condition_variable cv;
    std::thread listenerThread;

    std::chrono::seconds T_wait{5};
    std::chrono::seconds T_elec{5};

public:
    NaimiTrehel(int id, const std::string& ip, int port, std::shared_ptr<Comm> comm) 
        : TokenBasedNode(id, ip, port, comm), last(1), next(-1), inCS(false), nextUpdate(false) {
        hasToken = (id == 1);
        logger->log(id, -1, "init");
    }   

    ~NaimiTrehel() {
        if (listenerThread.joinable()) {
            listenerThread.join();
        }
        logger->log(id, -1, "destroy");
    }

    void initialize() override {
        listenerThread = std::thread(&NaimiTrehel::listenForMessages, this);
    }

    void requestToken() override {
        {
            std::unique_lock<std::mutex> lock(mtx);
            hasRequest = false;
            stopExtension = false;
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
                    break;
                }
            } 
            sendConsult();
        }

        std::unique_lock<std::mutex> lock(mtx);
        inCS = true;
    }

    void releaseToken() override {
        std::unique_lock<std::mutex> lock(mtx);
        inCS = false;
        hasRequest = false;
        if (nextUpdate) {
            sendToken(next);
            hasToken = false;
            nextUpdate = false;
        }
    }

private:
    void receiveRequest(int requesterId) {
        std::unique_lock<std::mutex> lock(mtx);
        if (id != last) {
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
        cv.notify_all();
    }

    void sendRequest(int destId, int requesterId) {
        std::string message = "REQUEST " + std::to_string(requesterId);
        comm->send(destId, message);
        if (id == requesterId) {
            logger->log(id, destId, "request token");
        } else {
            logger->log(id, destId, "send token request for requester " + std::to_string(requesterId));
        }
    }

    void sendToken(int destId) {
        std::string message = "TOKEN " + std::to_string(id);
        comm->send(destId, message);
        logger->log(id, destId, "send token to " + std::to_string(destId));
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
        } else if (msgType == "CONSULT") {
            receiveConsult(senderId);
        } else if (msgType == "RESPOND") { // respond for consult -> nut lien truoc van ton tai
            receiveRespond();
        } else if (msgType == "FAILURE") { 
            receiveFailure(senderId);
        } else if (msgType == "EXSIT") { // respond for failure -> token van ton tai
            receiveExsit();
        } else if (msgType == "ELECTION") {
            receiveElection(senderId);
        } else if (msgType == "ELECTED") {
            receiveElected(senderId);
        }
    }

    void receiveConsult(int senderId) {
        if (next == senderId) {
            comm->send(senderId, "RESPOND " + std::to_string(id));
            logger->log(id, senderId, "RESPOND from " + std::to_string(id));
        }
    }

    void receiveRespond() {
        hasRespond = true;
        cv.notify_all();
    }

    void receiveFailure(int senderId) {
        if (hasToken) {
            comm->send(senderId, "EXSIT " + std::to_string(id));
            logger->log(id, senderId, "token still EXSIT from " + std::to_string(id));
        }
    }

    void receiveExsit() {
        hasExsit = true;
        cv.notify_all();
    }

    void receiveElection(int senderId) {
        electedId[senderId] = true;
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
            logger->log(id, -1, "broadcast CONSULT");

            if (cv.wait_for(lock, T_elec, [this]() { return hasRespond || stopExtension; })) {
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
            logger->log(id, -1, "broadcast FAILURE");

            if (cv.wait_for(lock, T_elec, [this]() { return hasExsit || stopExtension; })) {
                return; 
            }
        } 
        sendElection();
    }

    // phat ban tin tao lai token
    void sendElection() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            for (int i = 1; i <= config.getTotalNodes(); i++) {
                if (i != id) {
                    comm->send(i, "ELECTION " + std::to_string(id));
                }
            }
            logger->log(id, -1, "broadcast ELECTION");
            if (cv.wait_for(lock, T_elec, [this]() { return stopExtension; })) {
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
                last = id;
                next = -1;
                electedId.clear();
                logger->log(id, -1, "regenerated token");
            }

            for (int i = 1; i <= config.getTotalNodes(); i++) {
                if (i != id) {
                    comm->send(i, "ELECTED " + std::to_string(id));
                }
            }
            logger->log(id, -1, "ELECTED token");
        }
    }

    void receiveElected(int senderId) {
        bool tmpRequest = hasRequest;
        {
            std::unique_lock<std::mutex> lock(mtx);
            next = -1;
            last = senderId;
            hasRequest = false;
            hasToken = false;
            inCS = false;
            hasRespond = false;
            hasExsit = false;
            stopExtension = true;
            cv.notify_all();
        }
        if (tmpRequest) requestToken();
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
