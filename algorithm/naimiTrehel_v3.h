// naimiTrehel_v3.h
#ifndef NAIMITREHELV3_H
#define NAIMITREHELV3_H

#include "node.h"
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <algorithm>

class NaimiTrehelV3 : public TokenBasedNode {
private:   
    int k;               // tham so bieu thi so loi ma thuat toan co the khoi phuc theo co che M1
    int last;            // id cua node cuoi cung yeu cau CS tai thoi diem yeu cau token
    int next;            // id cua node tiep theo nhan token
    int predecessor;     // nut tien nhiem lien truoc
    int position;        // vi tri cua node trong hang doi next
    int cnt;             // so lan truy cap CS
    int otherCnt;        // so lan truy cap CS cua nut khac
    int otherId;         // id nut khac khi dang bau cu
    int predecessorAlive;// nut tien nhiem con song, dung trong co che M1

    std::vector<int> predecessors;
    // std::vector<int> aliveM1;
    std::map<int, int> aliveM2;     // id, position
    std::map<int, std::pair<int, int>> aliveM3; // id, position, next

    bool inCS;           // co the truy cap hoac dang truy cap CS       -> khong can
    bool nextUpdate;     // kiem tra xem co node nao moi request hay khong
    bool hasRequest;     // da gui yeu cau
    bool hasCommit;      // da duoc ket noi vao queue
    bool hasAlive;       // ton tai mot trong cac nut tien nhiem con song 
    bool hasPong;        // nhan duoc phan hoi con song tu nut tien nhiem
    bool hasElection;    // phat hien ra loi theo co che M3 va dang trong qua trinh bau cu
    bool hasFailure;     // phat hien ra loi

    std::mutex mtx;
    std::mutex mutexCheckFailure;
    // std::mutex mutexPing;
    std::condition_variable cv;
    std::thread listenerThread;
    std::thread pingPong;

    const std::chrono::milliseconds T_msg{500};
    const std::chrono::seconds T_ping{5};

public:
    NaimiTrehelV3(int id, const std::string& ip, int port, int k, std::shared_ptr<Comm> comm) 
        : TokenBasedNode(id, ip, port, comm), k(k), last(1), next(-1), predecessor(-1), cnt(0), inCS(false), nextUpdate(false) {
        hasToken = (id == 1);
        position = (id == 1 ? 0 : -1);
        hasElection = false;
        for (int i = 0; i < k; i++) {
            predecessors.push_back(-1);
        }
        logger->log("notice", "token", id, -1, "", hasToken, "init", "node " + std::to_string(id) + " init");
    }   

    ~NaimiTrehelV3() {
        if (listenerThread.joinable()) {
            listenerThread.join();
        }
        if (pingPong.joinable()) {
            pingPong.join();
        }
        logger->log("notice", "token", id, -1, "", hasToken, "destroy", "node " + std::to_string(id) + " destroy");
    }

    void initialize() override {
        listenerThread = std::thread(&NaimiTrehelV3::listenForMessages, this);
        pingPong = std::thread(&NaimiTrehelV3::sendPing, this);
    }

    void requestToken() override {
        std::unique_lock<std::mutex> lock(mtx);
        if (hasRequest || hasToken) {
            return;
        }
        hasCommit = false;
        sendRequest(last, id);
        // logger->log("send", "token", id, last, std::to_string(id) + " to  " + std::to_string(last), hasToken ? "yes" : "no", "waiting", std::to_string(id) + " send request to " + std::to_string(last));
        last = id;
        if (!cv.wait_for(lock, 2 * T_msg, [this]() { return hasCommit; })) {
            lock.unlock();
            mechanism3();
            lock.lock();
        }

        cv.wait(lock, [this]() { return hasToken; });
    }   

    void releaseToken() override {
        std::unique_lock<std::mutex> lock(mtx);
        cnt++;
        predecessor = -1;
        position = -1;

        inCS = false;
        hasRequest = false;
        hasCommit = false;
        hasAlive = false;
        hasElection = false;
        hasFailure = false;

        if (nextUpdate) {
            sendToken(next);
            hasToken = false;
            nextUpdate = false;
        }
        logger->log("notice", "token", id, -1, "", hasToken, "", std::to_string(id) + " released token");
    }

private:
    void sendCommit(int requesterId) {
        // std::unique_lock<std::mutex> lock(mtx);
        std::string msg;
        if (position != -1) {
            msg = "COMMIT " + std::to_string(id);
            for (int i = 1; i < k; i++) {
                // if (predecessors[i] != -1) {
                    msg += " " + std::to_string(predecessors[i]);
                // } else {
                    // msg += " -1";
                // }
            }
            msg += " " + std::to_string(id) + " " + std::to_string(position + 1);
        } else {
            msg = "OK " + std::to_string(id);
        }
        // std::cout << msg << "\n";
        comm->send(requesterId, msg);
        logger->log("send", "token", id, requesterId, std::to_string(id) + " to " + std::to_string(requesterId), hasToken, "sent", std::to_string(id) + " sent commit message to " + std::to_string(requesterId));
    }

    void receivedCommit(int senderId, std::vector<int> tmpPredecessors, int tmpPosition) {
        // std::unique_lock<std::mutex> lock(mtx);
        // logger->log(id, id, std::to_string(id) + " received commit message from " + std::to_string(senderId));
        predecessor = senderId;
        predecessors = tmpPredecessors;
        position = tmpPosition;
        hasCommit = true;
        logger->log("receive", "token", senderId, id, std::to_string(senderId) + " to " + std::to_string(id), hasToken, "received", std::to_string(id) + " received commit message from " + std::to_string(senderId));
        cv.notify_one();
    }

    void receivedCommitOk(int senderId) {
        // std::unique_lock<std::mutex> lock(mtx);
        // logger->log(id, id, std::to_string(id) + " received commit message from " + std::to_string(senderId));
        predecessor = senderId;
        hasCommit = true;
        logger->log("receive", "token", senderId, id, std::to_string(senderId) + " to " + std::to_string(id), hasToken, "received", std::to_string(id) + " received commit message from " + std::to_string(senderId));
        cv.notify_one();
    }

    void sendPing() {
        std::unique_lock<std::mutex> lock(mutexCheckFailure);
        while (true) {
            if (predecessor == -1) {
                continue;
            }
            hasPong = false;
            comm->send(predecessor, "ping");
            if (!cv.wait_for(lock, 2 * T_msg, [this]() { return hasPong; })) {
                lock.unlock();
                mechanism1();
                lock.lock();
            }
            std::this_thread::sleep_for(T_ping);
        }
    }

    void sendPong() {
        comm->send(next, "pong " + std::to_string(id));
    }

    void receivedPong() {
        hasPong = true;
        cv.notify_one();
    }

    void mechanism1() { 
        std::unique_lock<std::mutex> lock(mutexCheckFailure);
        logger->log("error", "token", id, -1, "", hasToken, "", std::to_string(id) + " detected " + std::to_string(predecessor) + " failure");
        predecessor = -1;
        for (int i = k - 2; i >= 0; i--) {
            if (predecessors[i] == -1) {
                continue;
            }
            hasAlive = false;
            comm->send(predecessors[i], "ARE_YOU_ALIVE " + std::to_string(id));
            if (cv.wait_for(lock, 2 * T_msg, [this]() { return hasAlive; })) {
                sendRequestFailure(predecessorAlive, id);
                if (cv.wait_for(lock, 2 * T_msg, [this]() { return hasCommit; })) {
                    return;
                }
            }
        }
        lock.unlock();
        if (!hasAlive) {
            mechanism2();
        }
    }

    void mechanism2() {
        std::unique_lock<std::mutex> lock(mutexCheckFailure);
        std::string tmp = "";
        for (int i = k - 1; i >= 0; i--) {
            if (i != 0) {
                tmp += std::to_string(predecessors[i]) + " ";
            } else {
                tmp += std::to_string(predecessors[i]);
            }
        }
        logger->log("error", "token", id, -1, "", hasToken, "", std::to_string(id) + " detected " + tmp + " failure");
        aliveM2.clear();
        for (int i = 1; i <= config.getTotalNodes(); i++) {
            if (i == id) {
                continue;
            }
            comm->send(i, "SEARCH_PREV " + std::to_string(id) + " " + std::to_string(position));
        }
        std::this_thread::sleep_for(2 * T_msg);
        if (!aliveM2.empty()) {
            while (!aliveM2.empty()) {
                auto max = *std::max_element(aliveM2.begin(), aliveM2.end(), [](const auto &a, const auto &b) {
                    return a.second < b.second;
                });
                aliveM2.erase(max.first);
                sendRequestFailure(max.first, id);
                if (cv.wait_for(lock, 2 * T_msg, [this]() { return hasCommit; })) {     // tu bia ra
                    return;
                }
            }
        } else {
            regeneratedToken();
        }
    }

    void mechanism3() { 
        std::unique_lock<std::mutex> lock(mtx);
        logger->log("error", "token", id, -1, "", hasToken, "", std::to_string(id) + " sent request but did not receive commit");
        hasFailure = true;
        aliveM3.clear();
        for (int i = 1; i <= config.getTotalNodes(); i++) {
            if (i == id) {
                continue;
            }
            comm->send(i, "SEARCH_QUEUE " + std::to_string(id) + " " + std::to_string(cnt));
        }
        std::this_thread::sleep_for(2 * T_msg);
        if (aliveM3.empty()) {
            if (hasElection) {     // M3b
                if (cnt > otherCnt || (cnt == otherCnt) && (id > otherId)) {
                    sendRequest(otherId, id);
                    return;
                }
            }
        } else {
            // M3a
            while (!aliveM3.empty()) {      // tu bia ra
                auto maxElement = *std::max_element(aliveM3.begin(), aliveM3.end(), [this](const auto &a, const auto &b) {
                    return a.second.first < b.second.first;
                });
                aliveM3.erase(maxElement.first);
                if (maxElement.second.second == -1) {
                    sendRequestDirectly(maxElement.first);  
                    if (cv.wait_for(lock, 2 * T_msg, [this]() { return hasCommit; })) {
                        return;
                    }
                } else {
                    sendConnection(maxElement.first);       
                    if (cv.wait_for(lock, 2 * T_msg, [this]() { return hasCommit; })) {
                        return;
                    }
                } 
            }
        } 
        if (aliveM3.empty()) {
            regeneratedToken();
        }
    }

    void sendRequestDirectly(int dest) {
        comm->send(dest, "DIRECTLY " + std::to_string(id));
        logger->log("send", "token", id, dest, std::to_string(id) + " to " + std::to_string(dest), hasToken, "sent", std::to_string(id) + " sent request to " + std::to_string(dest));
    }

    void receivedRequestDirectly(int source) {
        next = source;
        last = source;
        logger->log("receive", "token", source, id, std::to_string(source) + " to " + std::to_string(id), hasToken, "recieved", std::to_string(id) + " received request from " + std::to_string(source));
        sendCommit(source);
    }

    void sendConnection(int dest) {
        comm->send(dest, "CONNECTION " + std::to_string(id));
        logger->log("send", "token", id, dest, std::to_string(id) + " to " + std::to_string(dest), hasToken, "sent", std::to_string(id) + " sent connection to " + std::to_string(dest));
    }

    void receivedConnection(int source) {
        next = source;
        last = source;
        logger->log("receive", "token", source, id, std::to_string(source) + " to " + std::to_string(id), hasToken, "received", std::to_string(id) + " received connection from " + std::to_string(source));
        sendCommit(source);
    }

    void receivedAckSearchQueue(int requesterId, int tmpPost, int tmpNext) {
        logger->log("receive", "token", requesterId, id, std::to_string(requesterId) + " to " + std::to_string(id), hasToken, "received", std::to_string(id) + " received ack search queue from " + std::to_string(requesterId));
        aliveM3.insert(std::pair(requesterId, std::pair(tmpPost, tmpNext)));
    }   

    void receivedSearchQueue(int requesterId) {
        logger->log("receive", "token", requesterId, id, std::to_string(requesterId) + " to " + std::to_string(id), hasToken, "received", std::to_string(id) + "received search queue from " + std::to_string(requesterId));
        if (!hasFailure) {
            comm->send(requesterId, "ACK_SEARCH_QUEUE " + std::to_string(id) + " " + std::to_string(position) + " " + std::to_string(next));
        } else {
            // std::cout << "hasElection: " << hasElection << "\n";
            hasElection = true;
        }
    }

    void regeneratedToken() {
        hasToken = true;
        position = 0;
        for (int i = 1; i <= config.getTotalNodes(); i++) {
            if (i == id) {
                continue;
            }
            comm->send(i, "REGENERATED_TOKEN " + std::to_string(id));
        }
        logger->log("send", "token", id, -1, "", hasToken, "broadcast", std::to_string(id) + " regenerated token");
        cv.notify_one();
    }

    void receivedRegeneratedToken(int requesterId) {
        logger->log("receive", "token", requesterId, id, std::to_string(requesterId) + " to " + std::to_string(id), hasToken, "received", std::to_string(requesterId) + " regenerated token");
        if (!hasRequest) {
            last = requesterId;
        } else if (position == -1) {
            next = last;
        } else {
            last = requesterId;
        }
    }

    void receivedSearchQuestion(int requesterId, int pos) {
        logger->log("received", "token", requesterId, id, std::to_string(requesterId) + " to " + std::to_string(id), hasToken, "received", std::to_string(id) + " received search question from " + std::to_string(requesterId));
        if (position < pos) {
            comm->send(requesterId, "ACK_SEARCH_PREV " + std::to_string(id) + " " + std::to_string(position));
        }
    }

    void receivedSearchPrevAck(int requesterId, int pos) {
        logger->log("received", "token", requesterId, id, std::to_string(requesterId) + " to " + std::to_string(id), hasToken, "received", std::to_string(id) + " received search preack from " + std::to_string(requesterId));
        aliveM2.insert(std::pair<int, int>{requesterId, pos});
    }

    void sendAlive(int requesterId) {
        // std::unique_lock<std::mutex> lock(mtx);
        comm->send(requesterId, "I_AM_ALIVE " + std::to_string(id));
        logger->log("send", "token", id, requesterId, std::to_string(id) + " to " + std::to_string(requesterId), hasToken, "sent", std::to_string(id) + " sent i am alive to " + std::to_string(requesterId));
    }

    void receivedAlive(int senderId) {
        logger->log("receive", "token", senderId, id, std::to_string(senderId) + " to " + std::to_string(id), hasToken, "received", std::to_string(id) + " received i am alive from " + std::to_string(senderId));
        hasAlive = true;
        predecessorAlive = senderId;
        cv.notify_one();
    }

    void receivedRequest(int requesterId) {
        logger->log("receive", "token", requesterId, id, std::to_string(requesterId) + " to " + std::to_string(id), hasToken, "received", std::to_string(id) + " received request from " + std::to_string(requesterId));
        if (last == id) {
            next = requesterId;
            nextUpdate = true;
            sendCommit(requesterId);
        } else {
            sendRequest(last, requesterId);
        }
        last = requesterId;
    }

    void receivedToken() {
        logger->log("receive", "token", predecessor, id, std::to_string(predecessor) + " to " + std::to_string(id), hasToken, "received", std::to_string(id) + " received token from " + std::to_string(predecessor));
        hasToken = true;
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

    // ham gui request token khi co loi  
    void sendRequestFailure(int destId, int requesterId) { 
        // std::unique_lock<std::mutex> lock(mtx);
        std::string message = "REQUEST_FAILURE " + std::to_string(requesterId);
        comm->send(destId, message);
        logger->log("send", "token", id, destId, std::to_string(id) + " to " + std::to_string(destId), hasToken, "sent", std::to_string(id) + " sent request to " + std::to_string(destId));
    }

    // ham nhan request khi co loi
    void receivedRequestFailure(int requesterId) {
        // std::unique_lock<std::mutex> lock(mtx);
        next = requesterId;
        // last = requesterId;
        logger->log("receive", "token", requesterId, id, std::to_string(requesterId) + " to " + std::to_string(id), hasToken, "received", std::to_string(id) + " received request from " + std::to_string(requesterId));
        sendCommit(requesterId);
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
            receivedRequest(senderId);
        } else if (msgType == "REQUEST_FAILURE") {
            receivedRequestFailure(senderId);
        } else if (msgType == "TOKEN") {
            receivedToken();
        } else if (msgType == "COMMIT") {
            std::vector<int> tmpPredecessors(k);
            for (int i = 0; i < k; i++) {
                int tmp;
                iss >> tmp;
                tmpPredecessors[i] = tmp; 
            }
            int tmpPosition;
            iss >> tmpPosition;
            receivedCommit(senderId, tmpPredecessors, tmpPosition);
        } else if (msgType == "OK") {
            receivedCommitOk(senderId);
        } else if (msgType == "ping") {
            sendPong();
        } else if (msgType == "pong") {
            receivedPong();
        } else if (msgType == "ARE_YOU_ALIVE") {
            sendAlive(senderId);
        } else if (msgType == "I_AM_ALIVE") {
            receivedAlive(senderId);
        } else if (msgType == "SEARCH_PREV") {
            int tmpPos;
            iss >> tmpPos;
            receivedSearchQuestion(senderId, tmpPos);
        } else if (msgType == "ACK_SEARCH_PREV") {
            int tmpPos;
            iss >> tmpPos;
            receivedSearchPrevAck(senderId, tmpPos);
        } else if (msgType == "REGENERATED_TOKEN") {
            receivedRegeneratedToken(senderId);
        } else if (msgType == "SEARCH_QUEUE") {
            otherId = senderId;
            iss >> otherCnt;
            receivedSearchQueue(senderId);
        } else if (msgType == "ACK_SEARCH_QUEUE") {
            int tmpPos;
            int tmpNext;
            iss >> tmpPos >> tmpNext;
            receivedAckSearchQueue(senderId, tmpPos, tmpNext);
        } else if (msgType == "DIRECTLY") {
            receivedRequestDirectly(senderId);
        } else if (msgType == "CONNECTION") {
            receivedConnection(senderId);
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
