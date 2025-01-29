// naimiTrehel_v2.h
#ifndef NAIMITREHELV2_H
#define NAIMITREHELV2_H

#include "node.h"
// #include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>

class NaimiTrehelV2 : public TokenBasedNode {
private:   
    int last;                           // node cuoi cung se truy cap CS
    int next;                           // node tiep theo se truy cap CS
    int totalNodes;
    bool freetime;
    // bool nextUpdate;                    // kiem tra xem co node nao moi request hay khong
    // bool hasRequest;                    // da gui yeu cau hay chua
    bool hasAckConsult;                    // da nhan duoc phan hoi sau khi gui CONSULT
    bool hasAckFailure;                      // da nhan duoc phan hoi sau khi gui ACK_FAILURE
    bool voted;                 // dung gui CONSULT...
    std::map<int, bool> listCandidate;

    std::mutex mtx;
    std::mutex mtxMsg;
    std::mutex mtxErr;
    std::condition_variable cv;

    std::thread receiveThread;

    const std::chrono::seconds T_wait{5};
    const std::chrono::seconds T_elec{5};

public:
    NaimiTrehelV2(int id, const std::string& ip, int port, std::shared_ptr<Comm> comm) 
        : TokenBasedNode(id, ip, port, comm), last(1), next(-1), freetime(true) {
        hasToken = (id == 1);
        totalNodes = config.getTotalNodes();
        // logger->log("notice", id, -1, "", hasToken, "init", "node " + std::to_string(id) + " init");
    }   

    ~NaimiTrehelV2() {
        if (receiveThread.joinable()) {
            receiveThread.join();
        }
    }

    void initialize() override {
        receiveThread = std::thread(&NaimiTrehelV2::receiveMsg, this);
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

        sendRequest(id, last);
        while (true) {
            if (hasToken) {
                break;
            } else if (voted) {
                sendRequest(id, last);
            } else {
                if (!cv.wait_for(lock, std::chrono::seconds(T_wait), [this]() { return hasToken; })) {
                    json note;
                    note["status"] = "null";
                    note["error"] = "suspect";
                    note["source"] = "null";
                    note["dest"] = "null";
                    note["last"] = last;
                    note["next"] = next;
                    logger->log("notice", id, std::to_string(id) + " suspect that a failure occurred", note);

                    sendConsult();
                }
            }
        }
    }

    void releaseToken() override {
        std::unique_lock<std::mutex> lock(mtx); 
        freetime = false;

        if (next != -1) {
            sendToken(next);
            next = -1;

            json note;
            note["status"] = "null";
            note["error"] = "null";
            note["source"] = "null";
            note["dest"] = "null";
            note["last"] = last;
            note["next"] = "null";
            logger->log("notice", id, std::to_string(id) + " release", note);
        }
    }

private:
    void receiveToken(int source) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        hasToken = true;

        json note;
        note["status"] = "ok";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = next;

        cv.notify_one();
    }

    void receiveRequest(int source) {
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
            next = source;
            last = source;
            if (freetime) {
                releaseToken();
            }
        }
    }

    void sendToken(int dest) {
        std::string message = std::to_string(id) + " TOKEN";

        json note;
        note["status"] = "null";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = dest;
        note["last"] = last;
        note["next"] = next;
        logger->log("send", id, std::to_string(id) + " sent token to " + std::to_string(dest), note);
        
        comm->send(dest, message);
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

    void sendConsult() { 
        {
            std::unique_lock<std::mutex> lock(mtxErr);
            hasAckConsult = false;

            json note;
            note["status"] = "null";
            note["error"] = "null";
            note["source"] = id;
            note["dest"] = "broadcast";
            note["last"] = last;
            note["next"] = next;
            logger->log("send", id, std::to_string(id) + " broadcast consult message", note);

            for (int i = 1; i <= totalNodes; i++) {
                if (i != id) {
                    comm->send(i, std::to_string(id) + " CONSULT");
                }
            }

            if (cv.wait_for(lock, T_elec, [this]() { return hasAckConsult || hasToken || voted; })) {
                return; 
            }
        } 
        sendFailure();
    }

    void sendAckConsult(int dest) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = dest;
        note["last"] = last;
        note["next"] = next;
        logger->log("send", id, std::to_string(id) + " sent ack consult message to " + std::to_string(dest), note);

        comm->send(dest, std::to_string(id) + " ACK_CONSULT");
    }

    void sendFailure() {
        {
            std::unique_lock<std::mutex> lock(mtxErr);
            hasAckFailure = false;

            json note;
            note["status"] = "null";
            note["error"] = "null";
            note["source"] = id;
            note["dest"] = "broadcast";
            note["last"] = last;
            note["next"] = next;
            logger->log("send", id, std::to_string(id) + " broadcast failure message", note);

            for (int i = 1; i <= totalNodes; i++) {
                if (i != id) {
                    comm->send(i, std::to_string(id) + " FAILURE");
                }
            }

            if (cv.wait_for(lock, T_elec, [this]() { return hasAckFailure || hasToken || voted; })) {
                return; 
            }
        } 
        sendElection();
    }

    void sendAckFailure(int dest) {
        std::unique_lock<std::mutex> lock(mtxMsg);

        json note;
        note["status"] = "ok";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = dest;
        note["last"] = last;
        note["next"] = next;
        logger->log("send", id, std::to_string(id) + " sent ack failure message to " + std::to_string(dest), note);

        comm->send(dest, std::to_string(id) + " ACK_FAILURE");       
    }

    void sendElection() {
        {
            std::unique_lock<std::mutex> lock(mtxErr);
            listCandidate[id] = true;

            json note;
            note["status"] = "null";
            note["error"] = "null";
            note["source"] = id;
            note["dest"] = "broadcast";
            note["last"] = last;
            note["next"] = next;
            logger->log("send", id, std::to_string(id) + " broadcast election message", note);

            for (int i = 1; i <= totalNodes; i++) {
                if (i != id) {
                    comm->send(i, std::to_string(id) + " ELECTION");
                }
            }
            
            if (cv.wait_for(lock, T_elec, [this]() { return hasToken || voted; })) {
                return;
            }
        }
        sendElected();
    }

    void sendElected() {
        std::unique_lock<std::mutex>(mtxErr);
        int minCandidate = id;
        for (const auto &it : listCandidate) {
            if (it.first < minCandidate) minCandidate = it.first;
        }
        if (id == minCandidate) {
            json note;
            note["status"] = "ok";
            note["error"] = "null";
            note["source"] = "null";
            note["dest"] = "null";
            note["last"] = id;
            note["next"] = -1;

            hasToken = true;
            last = id;
            next = -1;
            listCandidate.clear();

            note["source"] = id;
            note["dest"] = "broadcast";
            logger->log("send", id, std::to_string(id) + " broadcast elected message", note);
            for (int i = 1; i <= totalNodes; i++) {
                if (i != id) {
                    comm->send(i, std::to_string(id) + " ELECTED");
                }
            }
        }
    }

    void receiveConsult(int source) {
        std::unique_lock<std::mutex> lock(mtxMsg);

        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = next;
        logger->log("receive", id, std::to_string(id) + " received consult message from " + std::to_string(source), note);

        if (next == source) {
            sendAckConsult(source);
        }
    }

    void receiveAckConsult(int source) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        hasAckConsult = true;

        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = next;
        logger->log("receive", id, std::to_string(id) + " received ack consult message from " + std::to_string(source), note);

        cv.notify_one();
    }

    void receiveFailure(int source) {
        std::unique_lock<std::mutex> lock(mtxMsg);

        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = next;
        logger->log("receive", id, std::to_string(id) + " received failure message from " + std::to_string(source), note);

        if (hasToken) {
            sendAckFailure(source);
        }
    }

    void receiveAckFailure(int source) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        hasAckFailure = true;

        json note;
        note["status"] = hasToken ? "ok" : "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = next;
        logger->log("receive", id, std::to_string(id) + " received ack failure message from " + std::to_string(source), note);

        cv.notify_one();
    }

    void receiveElection(int source) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        json note;
        note["status"] = "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = next;
        logger->log("receive", id, std::to_string(id) + " received eletion message from " + std::to_string(source), note);     

        listCandidate[source] = true;
    }

    void receiveElected(int source) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        json note;
        note["status"] = "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = next;
        logger->log("receive", id, std::to_string(id) + " received eleted message from " + std::to_string(source), note);        

        next = -1;
        last = source;
        hasToken = false;
        hasAckConsult = false;
        hasAckFailure = false;
        voted = true;
        listCandidate.clear();
        cv.notify_one();
    }

    void processMessage(const std::string& message) {
        std::istringstream iss(message);
        std::string msgType;
        int source;
        iss >> source >> msgType;

        if (msgType == "REQUEST") {
            receiveRequest(source);
        } else if (msgType == "TOKEN") {
            receiveToken(source);
        } else if (msgType == "CONSULT") {
            receiveConsult(source);
        } else if (msgType == "ACK_CONSULT") { 
            receiveAckConsult(source);
        } else if (msgType == "FAILURE") { 
            receiveFailure(source);
        } else if (msgType == "ACK_FAILURE") {
            receiveAckFailure(source);
        } else if (msgType == "ELECTION") {
            receiveElection(source);
        } else if (msgType == "ELECTED") {
            receiveElected(source);
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

#endif // NAIMITREHELV2_H
