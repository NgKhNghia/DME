// lamport.h
#ifndef LAMPORT_H
#define LAMPORT_H

#include "node.h"
#include "log.h"
#include <atomic>
#include <mutex>
#include <queue>
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <random>

extern Logger logger;

enum MessageType { REQUEST, REPLY, RELEASE }; // Định nghĩa các loại tin nhắn

struct Message {
    int senderId;            // ID của nút gửi tin nhắn
    int timestamp;           // Dấu thời gian của tin nhắn
    MessageType type;        // Loại tin nhắn (REQUEST, REPLY, hoặc RELEASE)
    std::string content;     // Nội dung tin nhắn
};

class LamportNode : public PermissonBasedNode {
private:
    std::atomic<int> timeStamp;  
    std::priority_queue<Message, std::vector<Message>, bool(*)(const Message&, const Message&)> requestQueue; 
    std::mutex queueMutex;  
    std::map<int, bool> replyReceived;  

    static bool compareMessages(const Message &m1, const Message &m2) {
        return (m1.timestamp > m2.timestamp) || 
               (m1.timestamp == m2.timestamp && m1.senderId > m2.senderId);
    }

public:
    LamportNode(int id, const std::string& ip, int port, std::shared_ptr<Comm<std::string>> comm)
        : PermissonBasedNode(id, ip, port, comm), timeStamp(0), requestQueue(compareMessages) {
            initialize();
        }

    ~LamportNode() override {
        logger.log("NOTI", id, "destroyed");
    }

    void initialize() override {
        logger.log("NOTI", id, "init");
        startThread();  
    }

    void startThread() {
        std::thread([&] {
            while (1) {
                receiveMessage();
            }
        }).detach();
    }

    int getTimestamp() const {
        return timeStamp.load();
    }

    void incrementTimestamp() {
        timeStamp++;
    }

    void sendMessage(int receiverId, MessageType type, const std::string& content = "") {
        incrementTimestamp();
        int currentTimestamp = getTimestamp();
        
        Message msg = {id, currentTimestamp, type, content};
        std::string messageContent = formatMessage(msg);
        
        comm->send(receiverId, messageContent);
        logger.logPermisson("SEND", id, receiverId, currentTimestamp, messageContent);
    }

    void receiveMessage() {
        std::string messageContent;
        if (comm->getMessage(messageContent)) {
            Message msg = parseMessage(messageContent);
            int senderTimestamp = msg.timestamp;
            int senderId = msg.senderId;

            incrementTimestamp();
            timeStamp.store(std::max(timeStamp.load(), senderTimestamp) + 1);

            std::lock_guard<std::mutex> lock(queueMutex);
            switch (msg.type) {
                case REQUEST:
                    requestQueue.push(msg);
                    sendMessage(senderId, REPLY, "Send to " + std::to_string(msg.senderId));
                    break;
                    
                case REPLY:
                    replyReceived[senderId] = true;
                    break;

                case RELEASE:
                    removeRequest(senderId);
                    break;
            }
        }
    }

    void requestCS() {
        incrementTimestamp();
        int currentTimestamp = getTimestamp();

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            Message request = {id, currentTimestamp, REQUEST, "Request CS"};
            requestQueue.push(request);
            resetReplies();
        }

        for (const auto& node : config.getNodeConfigs()) {
            if (node.first != id) {
                sendMessage(node.first, REQUEST, "Send to " + std::to_string(node.first));
            }
        }
    }

    bool canEnterCS() {
        std::lock_guard<std::mutex> lock(queueMutex);
        
        return std::all_of(replyReceived.begin(), replyReceived.end(), 
                [](const std::pair<int, bool>& entry) { return entry.second; }) &&
                !requestQueue.empty() && requestQueue.top().senderId == id;
    }

    void releaseCS() {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (!requestQueue.empty() && requestQueue.top().senderId == id) {
                requestQueue.pop();
            }
        }

        for (const auto& node : config.getNodeConfigs()) {
            if (node.first != id) {
                sendMessage(node.first, RELEASE, "Send to " + std::to_string(node.first));
            }
        }
    }

    void resetReplies() {
        for (auto& entry : replyReceived) {
            entry.second = false;
        }
    }

    Message parseMessage(const std::string& messageContent) {
        // example: "Id: 1, Timestamp: 5, Type: REQUEST, Content: Requesting CS"
        int senderIdIndex = messageContent.find("Id: ");
        int senderTimestampIndex = messageContent.find("Timestamp: ");
        int senderTypeIndex = messageContent.find("Type: ");
        int senderContentIndex = messageContent.find("Content: ");

        int senderId = std::stoi(messageContent.substr(senderIdIndex + 4, senderTimestampIndex - (senderIdIndex + 4) - 2));
        int senderTimestamp = std::stoi(messageContent.substr(senderTimestampIndex + 11, senderTypeIndex - (senderTimestampIndex + 11) - 2));
        std::string tmp = messageContent.substr(senderTypeIndex + 6, senderContentIndex - (senderTypeIndex + 6) - 2); 
        MessageType senderType = (tmp == "REQUEST") ? REQUEST : (tmp == "REPLY") ? REPLY : RELEASE;
        std::string senderContent = messageContent.substr(senderContentIndex + 9);

        Message msg = {senderId, senderTimestamp, senderType, senderContent};
        return msg;
    }

    std::string formatMessage(const Message& msg) {
        std::string typeStr = (msg.type == REQUEST) ? "REQUEST" : (msg.type == REPLY) ? "REPLY" : "RELEASE";
        return "Id: " + std::to_string(msg.senderId) + ", Timestamp: " + std::to_string(msg.timestamp) + 
               ", Type: " + typeStr + ", Content: " + msg.content;
    }

    void removeRequest(int senderId) {
        std::priority_queue<Message, std::vector<Message>, 
            bool(*)(const Message&, const Message&)> tempQueue(compareMessages);
        
        while (!requestQueue.empty()) {
            Message msg = requestQueue.top();
            requestQueue.pop();
            if (msg.senderId != senderId) {
                tempQueue.push(msg);
            }
        }
        
        requestQueue = std::move(tempQueue);
    }

};

#endif // LAMPORT_H
