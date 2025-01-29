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
    bool freetime;

    std::mutex mtx;
    std::mutex mtxMsg;
    std::condition_variable cv;

    std::thread receiveThread;

public:
    NaimiTrehelV1(int id, const std::string& ip, int port, std::shared_ptr<Comm> comm) 
        : TokenBasedNode(id, ip, port, comm), last(1), next(-1), freetime(true) {
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
        if (receiveThread.joinable()) {
            receiveThread.join();
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
            cv.wait(lock, [this] { return hasToken; });
            {
                json note;
                note["status"] = "ok";
                note["error"] = "null";
                note["source"] = "null";
                note["dest"] = "null";
                note["last"] = last;
                note["next"] = next;
                logger->log("notice", id, std::to_string(id) + " enter critical section", note);
            }     
        }
    }

    void releaseToken() override {
        std::unique_lock<std::mutex> lock(mtx);
        {
            json note;
            note["status"] = "ok";
            note["error"] = "null";
            note["source"] = "null";
            note["dest"] = "null";
            note["last"] = last;
            note["next"] = next;
            logger->log("notice", id, std::to_string(id) + " exit critical section", note);
        }    
        freetime = true;
        
        if (next != -1) {
            // hasToken = false;
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
    void initialize() override {
        receiveThread = std::thread(&NaimiTrehelV1::receiveMsg, this);
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
            next = source;
            last = source;
            if (freetime) {
                releaseToken();
            }
        }
    }

    void receivedToken(int source) {
        std::unique_lock<std::mutex> lock(mtxMsg);
        hasToken = true;

        json note;
        note["status"] = "ok";
        note["error"] = "null";
        note["source"] = source;
        note["dest"] = id;
        note["last"] = last;
        note["next"] = next;
        logger->log("receive", id, std::to_string(id) + " received token", note);

        cv.notify_one();
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

    void sendToken(int dest) {
        std::string message = std::to_string(id) + " TOKEN";
        hasToken = false;

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

    void processMessage(const std::string& message) {
        std::istringstream iss(message);
        std::string msg;
        int source;
        iss >> source >> msg;

        if (msg == "REQUEST") {
            receivedRequest(source);
        } else if (msg == "TOKEN") {
            receivedToken(source);
        }
    }

    void receiveMsg() {
        std::string message;
        while (true) {
            message = "";
            if (comm->getMessage(message)) {
                processMessage(message);
            }
        }
    }


};

#endif // NAIMITREHELV1_H
