// naimiTrehel_v3.h
#ifndef NAIMITREHELV3_H
#define NAIMITREHELV3_H

#include "node.h"
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <algorithm>

class NaimiTrehel : public TokenBasedNode {
private:   
    int k;               // tham so bieu thi so loi ma thuat toan co the khoi phuc theo co che M1
    int last;            // id cua node cuoi cung yeu cau CS tai thoi diem yeu cau token
    int next;            // id cua node tiep theo nhan token
    int predecessor;
    std::vector<int> predecessors;
    std::vector<int> aliveM1;
    std::map<int, int> aliveM2;
    int position;        // vi tri cua node trong hang doi next
    bool inCS;           // co the truy cap hoac dang truy cap CS
    bool nextUpdate;     // kiem tra xem co node nao moi request hay khong
    bool hasRequest;
    bool hasCommit;
    bool hasPong;


    std::mutex mtx;
    std::mutex mutexCheckFailure;
    std::condition_variable cv;
    std::thread listenerThread;

    const std::chrono::seconds T_msg{10};
    const std::chrono::seconds T_ping{5};

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
        std::unique_lock<std::mutex> lock(mtx);
        if (hasRequest) {
            return;
        }
        if (hasToken) {
            return;
        }

        sendRequest(last, id);
        last = id;
        if (!cv.wait_for(lock, std::chrono::seconds(T_msg), [this]() { return hasCommit; })) {
            mechanism3();
        }
        sendPing(); // tao luong moi
    }   

    void releaseToken() override {
        
    }

private:
    void sendCommit(int requesterId) {
        std::unique_lock<std::mutex> lock(mtx);
        std::string msg = "COMMIT " + std::to_string(id);
        for (int i = 0; i < k; i++) {
            if (predecessors[i] != -1) {
                msg += " " + std::to_string(predecessors[i]);
            } else {
                msg += " -1";
            }
        }
        msg += " " + std::to_string(position + 1);
    }

    void receivedCommit(int senderId, std::vector<int> prede, int pos) {
        std::unique_lock<std::mutex> lock(mtx);
        logger->log(id, id, std::to_string(senderId) + " send commit message to " + std::to_string(id));
        predecessor = senderId;
        predecessors = prede;
        position = pos;
        hasCommit = true;
        cv.notify_all();
    }

    void sendPing() {
        std::unique_lock<std::mutex> lock(mutexCheckFailure);
        if (predecessor == -1) {
            return;
        }
        while (true) {
            hasPong = false;
            comm->send(predecessor, "ping");
            if (!cv.wait_for(lock, std::chrono::seconds(T_msg), [this]() { return hasPong; })) {
                mechanism1();
            }
            std::this_thread::sleep_for(std::chrono::seconds(T_ping));
        }
    }

    void sendPong() {
        comm->send(next, "pong " + std::to_string(id));
    }

    void receivedPong() {
        std::unique_lock<std::mutex> lock(mutexCheckFailure);
        hasPong = true;
        cv.notify_one();
    }

    void mechanism1() {
        std::unique_lock<std::mutex> lock(mutexCheckFailure);
        for (int i = k - 1; i >= 0; i--) {
            comm->send(predecessors[i], "ARE_YOU_ALIVE");
        }
        cv.wait_for(lock, std::chrono::seconds(T_msg), [this]() {});
        if (aliveM1.empty()) {
            mechanism2();
        } else {
            int max = *std::max_element(aliveM1.begin(), aliveM1.end());
            sendRequest(max, id);
            if (!cv.wait_for(lock, std::chrono::seconds(T_msg), [this]() { return hasCommit; })) {
                mechanism3();
            }
        }
    }

    void mechanism2() {
        std::unique_lock<std::mutex> lock(mutexCheckFailure);
        
    }

    void mechanism3() {

    }

    void receivedAlive(int senderId) {
        std::unique_lock<std::mutex> lock(mutexCheckFailure);
        for (int i = 1; i <= config.getTotalNodes(); i++) {
            comm->send(i, "SEARCH_PRE " + std::to_string(id) + " " + std::to_string(position));
        }
        std::this_thread::sleep_for(T_msg);
        if (!aliveM2.empty()) {
            auto max = std::max_element(aliveM2.begin(), aliveM2.end(), [](const std::pair<int, int> &a, const std::pair<int, int> &b) {
                return a.second < b.second;
            });
            sendRequest(max->first, id);
            if (!cv.wait_for(lock, std::chrono::seconds(T_msg), [this]() { return hasCommit; })) {
                mechanism3();
            }
        } else {
            hasToken = true;
            position = 0;
            cv.notify_all();
        }
    }

    void receiveRequest(int requesterId) {
        std::unique_lock<std::mutex> lock(mtx);
        
        if (last == id) {
            next = requesterId;
            sendCommit(requesterId);
        } else {
            sendRequest(last, requesterId);
        }
        last = requesterId;
    }

    void receiveToken() {
        std::unique_lock<std::mutex> lock(mtx);
        hasToken = true;
        cv.notify_one();
    }

    void sendRequest(int destId, int requesterId) {
        std::string message = "REQUEST " + std::to_string(requesterId);
        comm->send(destId, message);
        if (id == requesterId) {
            logger->log(id, destId, std::to_string(id) + " request token");
        } else {
            logger->log(id, destId, std::to_string(id) + " send token request to " + std::to_string(destId) + " for " + std::to_string(requesterId));
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
        } else if (msgType == "COMMIT") {
            std::vector<int> prede;
            for (int i = 0; i < k; i++) {
                iss >> prede[i]; 
            }
            int pos;
            iss >> pos;
            receivedCommit(senderId, prede, pos);
        } else if (msgType == "ping") {
            sendPong();
        } else if (msgType == "pong") {
            receivedPong();
        } else if (msgType == "I_AM_STILL_ALIVE") {
            receivedAlive(senderId);
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

#endif // NAIMITREHELV3_H
