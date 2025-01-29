// lamport.h
#ifndef LAMPORT_H
#define LAMPORT_H

#include "node.h"
#include "log.h"
#include <mutex>
#include <thread>
#include <vector>
#include <set>
#include <queue>
#include <condition_variable>

class Lamport : public PermissonBasedNode {
private: 
    typedef struct Request {
        int id;
        int timestamp;
        bool operator<(const Request &other) const { // min-heap
            return (timestamp > other.timestamp) || (timestamp == other.timestamp && id > other.id);
        }
        bool operator>(const Request &other) const { // map-heap
            return (timestamp < other.timestamp) || (timestamp == other.timestamp && id < other.id);
        }
    } REQUEST;

    int totalNodes;
    int globalTimestamp;  
    int localTimestamp;
    std::priority_queue<REQUEST, std::vector<REQUEST>, std::less<REQUEST>> listRqt;
    std::set<int> listReply;

    std::thread receiveThread;

    std::mutex mtx;
    std::mutex mtxMsg;
    std::condition_variable cv;
public:
    Lamport(int id, const std::string& ip, int port, std::shared_ptr<Comm> comm)
        : PermissonBasedNode(id, ip, port, comm), globalTimestamp(0), localTimestamp(0) {
            json note;
            note["status"] = "null";
            note["init"] = "ok";
            note["error"] = "null";
            note["source"] = "null";
            note["dest"] = "null";
            logger->log("notice", id, std::to_string(id) + " init", note);
            totalNodes = config.getTotalNodes();
            initialize();
    }

    ~Lamport() {
        if (receiveThread.joinable()) {
            receiveThread.join();
        }
    }

    void requestPermission() override {
        std::unique_lock<std::mutex> lock(mtx);
        globalTimestamp++;
        localTimestamp = globalTimestamp;
        REQUEST rqt = {id, localTimestamp};
        listRqt.push(rqt);
        for (int i = 1; i <= totalNodes; i++) {
            if (i == id) {
                continue;
            }
            comm->send(i, std::to_string(rqt.id) + " REQUEST " + std::to_string(rqt.timestamp)); // id type timestamp
        }
        json note;
        note["status"] = "null";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = "broadcast";
        logger->log("send", id, std::to_string(id) + " sent request broadcast", note);
        cv.wait(lock, [this]() { 
            return (listReply.size() == totalNodes - 1) && (listRqt.top().id == id); 
        });
    }

    void releasePermission() override {
        std::unique_lock<std::mutex> lock(mtx);
        listRqt.pop();
        listReply.clear();
        for (int i = 1; i <= totalNodes; i++) {
            if (i == id) {
                continue;
            }
            comm->send(i, std::to_string(id) + " RELEASE " + std::to_string(localTimestamp));
        }
        json note;
        note["status"] = "null";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = "broadcast";
        logger->log("send", id, std::to_string(id) + " sent release broadcast", note);
    }

private:
    void initialize() override {
        receiveThread = std::thread(&Lamport::receiveMsg, this);
    }

    void sendAgree(int dest, int timestamp) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        comm->send(dest, std::to_string(id) + " OK " + std::to_string(timestamp));
        json note;
        note["status"] = (listReply.size() == totalNodes - 1) && (listRqt.top().id == id) ? "ok" : "null";
        note["error"] = "null";
        note["source"] = id;
        note["dest"] = dest;
        logger->log("send", id, std::to_string(id) + " sent ok to " + std::to_string(dest), note);
    }

    void receivedRqt(int source, int timestamp) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        json note;
        note["status"] = (listReply.size() == totalNodes - 1) && (listRqt.top().id == id) ? "ok" : "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        logger->log("receive", id, std::to_string(id) + " received request from " + std::to_string(source), note);
    
        globalTimestamp = std::max(globalTimestamp, timestamp) + 1;
        REQUEST rqt = {source, timestamp};
        listRqt.push(rqt);
    }

    void receivedAgree(int source, int timestamp) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        json note;
        note["status"] = (listReply.size() == totalNodes - 1) && (listRqt.top().id == id) ? "ok" : "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        logger->log("receive", id, std::to_string(id) + " received ok from " + std::to_string(source), note);

        listReply.insert(source);
        cv.notify_one();
    }

    void receivedRls(int source, int timestamp) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        json note;
        note["status"] = (listReply.size() == totalNodes - 1) && (listRqt.top().id == id) ? "ok" : "null";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        logger->log("receive", id, std::to_string(id) + " received release from " + std::to_string(source), note);

        listRqt.pop();
        cv.notify_one();
    }

    void processMsg(const std::string msg) {
        std::istringstream iss(msg);
        int id;
        std::string type;
        int timestamp;
        iss >> id >> type >> timestamp;
        if (type == "REQUEST") {
            receivedRqt(id, timestamp);
            sendAgree(id, timestamp);
        } else if (type == "OK") {
            receivedAgree(id, timestamp);
        } else if (type == "RELEASE") {
            receivedRls(id, timestamp);
        }
    }

    void receiveMsg() {
        std::string msg;
        while (1) {
            msg = "";
            if (comm->getMessage(msg)) {
                processMsg(msg);
            }
        }
    }
};


#endif // LAMPORT_H
