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
    int totalNodes;
    int k;               // tham so bieu thi so loi ma thuat toan co the khoi phuc theo co che M1
    int last;            // id cua node cuoi cung yeu cau CS tai thoi diem yeu cau token
    int next;            // id cua node tiep theo nhan token
    int predecessor;     // nut tien nhiem lien truoc
    int position;        // vi tri cua node trong hang doi next
    int cnt;             // so lan truy cap CS
    int otherCnt;        // so lan truy cap CS cua nut khac
    int otherId;         // id nut khac khi dang bau cu

    std::vector<int> listPredecesers;
    std::map<int, int> aliveM2;     // id, position
    std::map<int, std::pair<int, int>> aliveM3; // id, position, next

    bool freetime;
    bool hasCommit;      // da duoc ket noi vao queue
    bool hasAlive;       // ton tai mot trong cac nut tien nhiem con song 
    bool hasPong;        // nhan duoc phan hoi con song tu nut tien nhiem
    bool electionDetected;    // phat hien ra loi theo co che M3 va dang trong qua trinh bau cu
    bool failureDetected;     // phat hien ra loi

    std::mutex mtx;
    std::mutex mtxMsg;
    std::mutex mtxPingPong;

    std::condition_variable cv;

    std::thread receiveThread;
    std::thread pingPong;

    const std::chrono::milliseconds T_msg{500};
    const std::chrono::seconds T_ping{5};

public:
    NaimiTrehelV3(int id, const std::string& ip, int port, int k, std::shared_ptr<Comm> comm) 
        : TokenBasedNode(id, ip, port, comm), k(k), last(1), next(-1), predecessor(-1), cnt(0), freetime(true) {
        totalNodes = config.getTotalNodes();
        hasToken = (id == 1);
        position = (id == 1 ? 0 : -1);
        for (int i = 0; i < k; i++) {
            listPredecesers.push_back(-1);
        }

        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["init"] = "ok";
        // note["error"] = "null";
        // note["source"] = "null";
        // note["dest"] = "null";
        note["last"] = last;
        note["next"] = next;
        logger->log("notice", id, std::to_string(id) + " init", note);

        initialize();
    }   

    ~NaimiTrehelV3() {
        if (receiveThread.joinable()) {
            receiveThread.join();
        }
        if (pingPong.joinable()) {
            pingPong.join();
        }
    }

    void requestToken() override {
        std::unique_lock<std::mutex> lock(mtx);
        freetime = false;
        
        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = "null";
        note["dest"] = "null";
        note["last"] = last;
        note["next"] = next;
        logger->log("notice", id, std::to_string(id) + " request token", note);
        
        if (!hasToken) {
            sendRequest(id, last);
            if (!cv.wait_for(lock, 2 * T_msg, [this]() { return hasCommit; })) {
                lock.unlock();
                mechanism3();
                lock.lock();
            }
            cv.wait(lock, [this]() { return hasToken; });
        }
        
    }   

    void releaseToken() override {
        std::unique_lock<std::mutex> lock(mtx);
        cnt++;
        predecessor = -1;
        position = -1;
        freetime = true;
        otherId = -1;
        otherCnt = -1;

        if (next != -1) {
            sendToken(next);
            next = -1;

            json note;
            note["status"] = "null";
            note["error"] = "null";
            note["source"] = "null";
            note["dest"] = "null";
            note["last"] = last;
            note["next"] = -1;
            logger->log("notice", id, std::to_string(id) + " release", note);
        }
    }

private:
    void initialize() override {
        receiveThread = std::thread(&NaimiTrehelV3::receiveMsg, this);
        pingPong = std::thread(&NaimiTrehelV3::sendPing, this);
    }

    void sendRequest(int source, int dest) {
        std::string message = std::to_string(source) + " REQUEST";
        last = source;

        json note;
        note["status"] = "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = dest;
        note["last"] = last;
        note["next"] = next;
        
        if (id == source) {
            logger->log("send", id, std::to_string(id) + " sent request to " + std::to_string(dest), note);
        } else {
            logger->log("send", id, std::to_string(id) + " sent request to " + std::to_string(dest) + " for " + std::to_string(source), note);
        }
        
        comm->send(dest, message);
    }

    void receivedRequest(int source) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = source;
        note["next"] = (next != -1) ? next : (freetime) ? -1 : source;
        logger->log("receive", id, std::to_string(id) + " received request from " + std::to_string(source), note);
        
        if (id != last) {
            sendRequest(source, last);
        } else {
            sendCommit(source);
            if (freetime) {
                releaseToken();
            }
        }
    }

    void sendCommit(int dest) {
        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = dest;
        note["last"] = dest;
        note["next"] = dest;
        logger->log("send", id, std::to_string(id) + " sent commit to " + std::to_string(dest), note);
        
        next = dest;
        last = dest;
        std::string msg = std::to_string(id) + " COMMIT ";
        for (int i = 1; i < k; i++) {
            msg += " " + std::to_string(listPredecesers[i]);
        }
        msg += " " + std::to_string(id) + " " + std::to_string(position + 1);
        comm->send(dest, msg);
    }

    void receivedCommit(int source, std::vector<int> predes, int pos) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        json note;
        note["status"] = "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = next;
        logger->log("receive", id, std::to_string(id) + " received commit message from " + std::to_string(source), note);

        predecessor = source;
        this->listPredecesers = predes;
        position = pos;
        hasCommit = true;
        cv.notify_one();
    }

    void sendToken(int destId) {
        json note;
        note["status"] = "null";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = next;
        note["last"] = last;
        note["next"] = next;
        logger->log("send", id, std::to_string(id) + " sent token to " + std::to_string(next), note);

        hasToken = false;
        std::string message = std::to_string(id) + " TOKEN";
        comm->send(destId, message);
    }

    void receivedToken() {
        json note;
        note["status"] = "ok";
        note["error"] = "null";
        note["source"] = predecessor;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = next;
        logger->log("receive", id, std::to_string(id) + " received token from " + std::to_string(predecessor), note);

        hasToken = true;
        cv.notify_one();
    }

    void mechanism1() { 
        std::unique_lock<std::mutex> lock(mtxPingPong);
        json note;
        note["status"] = "null";
        note["error"] = predecessor;
        note["source"] = "null";
        note["dest"] = "null";
        note["last"] = last;
        note["next"] = next;
        logger->log("notice", id, std::to_string(id) + " detected " + std::to_string(predecessor) + " failure", note);

        predecessor = -1;
        for (int i = k - 2; i >= 0; i--) {
            if (listPredecesers[i] == -1) {
                continue;
            }
            sendAreYouAlive(listPredecesers[i]);
            if (cv.wait_for(lock, 2 * T_msg, [this]() { return hasAlive; })) {
                sendRequestM1(listPredecesers[i]);
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

    void sendAreYouAlive(int dest) {
        comm->send(dest, std::to_string(id) + " ARE_YOU_ALIVE");
    }

    void receiveAreYouAlive(int source) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = next;
        logger->log("receive", id, std::to_string(id) + " received are you alive from " + std::to_string(source), note);
        
        sendIAmAlive(source);
    }

    void sendIAmAlive(int dest) {
        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = dest;
        note["last"] = last;
        note["next"] = next;
        logger->log("send", id, std::to_string(id) + " sent i am alive to " + std::to_string(dest), note);

        comm->send(dest, std::to_string(id) + " I_AM_ALIVE");
    }
    
    void receiveIAmAlive(int source) {
        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = next;
        logger->log("receive", id, std::to_string(id) + " received i am alive from " + std::to_string(source), note);
        
        hasAlive = true;
        cv.notify_one();
    }

    void sendRequestM1(int dest) { 
        json note;
        note["status"] = "null";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = dest;
        note["last"] = last;
        note["next"] = next;
        logger->log("send", id, std::to_string(id) + " sent request m1 to " + std::to_string(dest), note);

        std::string message = std::to_string(id) + " REQUEST_M1";
        comm->send(dest, message);
    }

    void receiveRequestM1(int source) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = source;
        logger->log("receive", id, std::to_string(id) + " received request m1 from " + std::to_string(source), note);

        sendCommitM1(source);
    }

    void sendCommitM1(int dest) {
        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = dest;
        note["last"] = last;
        note["next"] = dest;
        logger->log("send", id, std::to_string(id) + " sent commit m1 to " + std::to_string(dest), note);

        next = dest;
        std::string msg = std::to_string(id) + " COMMIT ";
        for (int i = 1; i < k; i++) {
            msg += " " + std::to_string(listPredecesers[i]);
        }
        msg += " " + std::to_string(id) + " " + std::to_string(position + 1);
        comm->send(dest, msg);
    }

    void mechanism2() {
        std::unique_lock<std::mutex> lock(mtxPingPong);
        std::string listFailure = "";
        for (int i = k - 1; i >= 0; i--) {
            if (i != 0) {
                listFailure += std::to_string(listPredecesers[i]) + " ";
            } else {
                listFailure += std::to_string(listPredecesers[i]);
            }
        }
        json note;
        note["status"] = "null";
        note["error"] = listFailure;
        note["source"] = "null";
        note["dest"] = "null";
        note["last"] = last;
        note["next"] = next;
        logger->log("notice", id, std::to_string(id) + " detected " + listFailure + " failure", note);

        aliveM2.clear();
        sendSearchPrev();
        std::this_thread::sleep_for(2 * T_msg);
        if (!aliveM2.empty()) {
            while (!aliveM2.empty()) {
                auto max = *std::max_element(aliveM2.begin(), aliveM2.end(), [](const auto &a, const auto &b) {
                    return a.second < b.second;
                });
                aliveM2.erase(max.first);
                sendRequestM1(max.first);
                if (cv.wait_for(lock, 2 * T_msg, [this]() { return hasCommit; })) {     // tu bia ra
                    return;
                }
            }
        } else {
            regeneratedToken();
        }
    }

    void sendSearchPrev() {
        json note;
        note["status"] = "null";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = "broadcast";
        note["last"] = last;
        note["next"] = next;
        logger->log("send", id, std::to_string(id) + " broadcast search prev", note);

        for (int i = 1; i <= totalNodes; i++) {
            if (i != id) {
                comm->send(i, std::to_string(id) + " SEARCH_PREV " +  std::to_string(position));
            }
        }
    }

    void receiveSearchPrev(int source, int pos) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = next;
        logger->log("receive", id, std::to_string(id) + " received search prev from " + std::to_string(source), note);

        if (position < pos) {
            sendAckSearchPrev(source);
        }
    }

    void sendAckSearchPrev(int dest) {
        comm->send(dest, std::to_string(id) + " ACK_SEARCH_PREV" + std::to_string(position));
    }

    void receiveAckSearchPrev(int source, int pos) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = next;
        logger->log("receive", id, std::to_string(id) + " received ack search prev from " + std::to_string(source), note);

        aliveM2.insert(std::pair<int, int>{source, pos});
    }

    void mechanism3() { 
        std::unique_lock<std::mutex> lock(mtx);
        json note;
        note["status"] = "null";
        note["error"] = "null";
        note["source"] = "null";
        note["dest"] = "null";
        note["last"] = last;
        note["next"] = next;
        logger->log("notice", id, std::to_string(id) + " sent request message but didn't receive commit", note);

        bool stop = false;
        failureDetected = true;
        aliveM3.clear();
        sendSearchQueue();
        // M3b
        std::thread t([&, this]() { 
            int time = 0;
            while (!stop && (std::chrono::milliseconds)time < T_msg) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                time += 10;
            }
            stop = true;
        });

        while (!stop) {
            if (electionDetected) {     
                if (cnt > otherCnt || (cnt == otherCnt) && (id > otherId)) {
                    stop = true;
                    t.join();
                    sendRequest(otherId, id);
                    return;
                } else {
                    electionDetected = false;
                }
            }
        }
        if (t.joinable()) {
            t.join();
        }
        // M3a
        while (!aliveM3.empty()) {     
            auto maxElement = *std::max_element(aliveM3.begin(), aliveM3.end(), [this](const auto &a, const auto &b) {
                return a.second.first < b.second.first;
            });
            aliveM3.erase(maxElement.first);
            if (maxElement.second.second == -1) {
                sendRequest(id, maxElement.first); 
                if (cv.wait_for(lock, 2 * T_msg, [this]() { return hasCommit; })) {
                    return;
                } else {
                    logger->log("notice", id, std::to_string(id) + " detected " + std::to_string(maxElement.first) + " failure", note);
                }
            } else {
                logger->log("notice", id, std::to_string(id) + " detected " + std::to_string(maxElement.second.second) + " failure", note);

                sendConnection(maxElement.first);       
                if (cv.wait_for(lock, 2 * T_msg, [this]() { return hasCommit; })) {
                    return;
                } else {
                    logger->log("notice", id, std::to_string(id) + " detected " + std::to_string(maxElement.first) + " failure", note);
                }
            } 
        }
        if (aliveM3.empty()) {
            regeneratedToken();
        }
    }

    void sendSearchQueue() {
        json note;
        note["status"] = "null";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = "broadcast";
        note["last"] = last;
        note["next"] = next;
        logger->log("send", id, std::to_string(id) + " broadcast search queue", note);

        for (int i = 1; i <= totalNodes; i++) {
            if (i != id) {
                comm->send(i, std::to_string(id) + " SEARCH_QUEUE " + std::to_string(cnt));
            }
        }
    }

    void receiveSearchQueue(int source) {
        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = next;
        logger->log("receive", id, std::to_string(id) + " received search queue from " + std::to_string(source), note);

        if (!failureDetected) {
            if (position != -1) {
                sendAckSearchQueue(source);
            }
        } else {
            electionDetected = true;
        }
    }

    void sendAckSearchQueue(int dest) {
        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = dest;
        note["last"] = last;
        note["next"] = next;
        logger->log("send", id, std::to_string(id) + " sent ack search queue to " + std::to_string(dest), note);

        comm->send(dest, std::to_string(id) + " ACK_SEARCH_QUEUE " + std::to_string(position) + " " + std::to_string(next));
    }

    void receivedAckSearchQueue(int source, int pos, int next) {
        json note;
        note["status"] = "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = this->next;
        logger->log("received", id, std::to_string(id) + " received ack search queue from " + std::to_string(source), note);

        aliveM3.insert(std::pair(source, std::pair(pos, next)));
    }   

    void sendConnection(int dest) {
        json note;
        note["status"] = "null";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = dest;
        note["last"] = last;
        note["next"] = next;
        logger->log("send", id, std::to_string(id) + " sent connection to " + std::to_string(dest), note);

        comm->send(dest, std::to_string(id) + " CONNECTION");
    }

    void receivedConnection(int source) {
        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = source;
        logger->log("receive", id, std::to_string(id) + " received connection from " + std::to_string(source), note);

        sendAckConnection(source);
    }

    void sendAckConnection(int dest) {
        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = dest;
        note["last"] = last;
        note["next"] = dest;
        logger->log("send", id, std::to_string(id) + " sent token to " + std::to_string(dest), note);

        next = dest;
        std::string msg = std::to_string(id) + " COMMIT ";
        for (int i = 1; i < k; i++) {
            msg += " " + std::to_string(listPredecesers[i]);
        }
        msg += " " + std::to_string(id) + " " + std::to_string(position + 1);
        comm->send(dest, msg);
    }

    void regeneratedToken() {
        json note;
        note["status"] = "ok";
        note["error"] = "null";
        note["source"] = "null";
        note["dest"] = "null";
        note["last"] = last;
        note["next"] = next;
        logger->log("notice", id, std::to_string(id) + " regenerated token", note);

        hasToken = true;
        position = 0;
        sendRegenerated();
        cv.notify_one();
    }

    void sendRegenerated() {
        json note;
        note["status"] = "ok";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = "broadcast";
        note["last"] = last;
        note["next"] = next;
        logger->log("send", id, std::to_string(id) + " broadcast regenerated", note);

        for (int i = 1; i <= totalNodes; i++) {
            if (i != id) {
                comm->send(i, std::to_string(id) + " REGENERATED");
            }
        }
    }

    void receiveRegenerated(int source) {
        if (freetime) {
            last = source;
        } else if (position != -1) {
            last = source;
        } else {
            next = last;
        }

        json note;
        note["status"] = "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = next;
        logger->log("receive", id, std::to_string(id) + " received regenerated from " + std::to_string(source), note);
    }

    void sendPing() {
        std::unique_lock<std::mutex> lock(mtxPingPong);
        while (true) {
            if (hasToken || predecessor == -1) {
                continue;
            }
            comm->send(predecessor, std::to_string(id) + " PING");
            if (!cv.wait_for(lock, 2 * T_msg, [this]() { return hasPong; })) {
                lock.unlock();
                mechanism1();
                lock.lock();
            }
            std::this_thread::sleep_for(T_ping);
        }
    }

    void receivePing(int source) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        sendPong(source);
    }

    void sendPong(int dest) {
        comm->send(dest, std::to_string(id) + " PONG");
    }

    void receivePong(int source) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        hasPong = true;
        cv.notify_one();
    }

    void processMessage(const std::string& message) {
        std::istringstream iss(message);
        std::string msgType;
        int source;
        iss >> source >> msgType;

        if (msgType == "REQUEST") {
            receivedRequest(source);
        } else if (msgType == "COMMIT") {
            std::vector<int> listPredecesers(k);
            int pos;
            for (int i = 0; i < k; i++) {
                iss >> listPredecesers[i];
            }
            iss >> pos;
            receivedCommit(source, listPredecesers, pos);
        } else if (msgType == "TOKEN") {
            receivedToken();
        } else if (msgType == "ARE_YOU_ALIVE") {
            receiveAreYouAlive(source);
        } else if (msgType == "I_AM_ALIVE") {
            receiveIAmAlive(source);
        } else if (msgType == "REQUEST_M1") {
            receiveRequestM1(source);
        } else if (msgType == "SEARCH_PREV") {
            int pos;
            iss >> pos;
            receiveSearchPrev(source, pos);
        } else if (msgType == "ACK_SEARCH_PREV") {
            int pos;
            iss >> pos;
            receiveAckSearchPrev(source, pos);
        } else if (msgType == "SEARCH_QUEUE") {
            otherId = source;
            iss >> otherCnt;
            receiveSearchQueue(source);
        } else if (msgType == "ACK_SEARCH_QUEUE") {
            int pos;
            int next;
            iss >> pos >> next;
            receivedAckSearchQueue(source, pos, next);
        } else if (msgType == "CONNECTION") {
            receivedConnection(source);
        } else if (msgType == "REGENERATED") {
            receiveRegenerated(source);
        } else if (msgType == "PING") {
            receivePing(source);
        } else if (msgType == "PONG") {
            receivePong(source);
        } 
    }

    void receiveMsg() {
        while (true) {
            std::string message;
            if (comm->getMessage(message)) {
                processMessage(message);
            }
        }
    }

};

#endif // NAIMITREHELV3_H
