// comm.h
#ifndef COMM_H
#define COMM_H

#include "config.h"
#include "log.h"
#include "node.h"
#include "error.h"
#include <string>
#include <cstring>
#include <queue>
#include <mutex>
#include <map>
#include <vector>
#include <memory>
#include <condition_variable>
#include <netinet/in.h>
#include <thread>
#include <unistd.h>
#include <arpa/inet.h>

extern Logger *logger;
extern ErrorSimulator error;

class Comm {
private:
    int id;
    int serverSocket;
    int opt = 1;
    struct sockaddr_in servaddr;
    std::mutex socketMutex;                      
    std::condition_variable messageAvailable;
    std::queue<std::string> messageQueue; 
    std::thread m_receiveThread;

public:
    Comm(int id, int port) : id(id) {
        if ((serverSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            throw std::runtime_error("Creating socket failed");
        }

        struct sockaddr_in servaddr;
        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_addr.s_addr = INADDR_ANY;
        servaddr.sin_port = htons(port);

        if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
            throw std::runtime_error("Error setting socket options");
        }

        if (bind(serverSocket, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
            close(serverSocket);
            throw std::runtime_error("Bind failed");
        }

        if (::listen(serverSocket, 5) < 0) {
            throw std::runtime_error("Listen failed");
            close(serverSocket);
        }

        m_receiveThread = std::thread(&Comm::receiveThread, this);
    }

    ~Comm() {
        if (m_receiveThread.joinable()) {
            m_receiveThread.join();
        }
        close(serverSocket);
    }

    void send(int destId, const std::string& message) {       
        if (error.simulateNetworkError()) {
            time_t now = time(0);
            tm *ltm = localtime(&now);
            char buffer[20];
            strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", ltm);
            std::cout << "node " << id << " died at " + std::string(buffer) << "\n";
            exit(EXIT_FAILURE);
        }
        // else if (error.simulateMessageLoss()) {
        //     return;
        // }
        // else if (error.simulateMessageDelay()) {
        //     std::this_thread::sleep_for(std::chrono::seconds(1));
        // }

        auto it = config.getNodeConfigs().find(destId);
        // if (it == config.getNodeConfigs().end()) {
        //     std::cout << "Destination ID " << destId << " not found";
        //     // return;
        //     throw std::runtime_error("Destination ID " + std::to_string(destId) + " not found");
        // }
        int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (clientSocket < 0) {
            close(clientSocket);
            return;
        }
        sockaddr_in destIp;
        destIp.sin_family = AF_INET;
        destIp.sin_port = htons(it->second.second);
        inet_pton(AF_INET, it->second.first.c_str(), &destIp.sin_addr);
        if (connect(clientSocket, (struct sockaddr*)&destIp, sizeof(destIp)) < 0) {
            close(clientSocket);
            return;
        }
        if (::send(clientSocket, message.c_str(), message.size(), 0) < 0) {
            close(clientSocket);
            return;
        }
        close(clientSocket);
    }

    int getMessage(std::string& msg) {
        std::unique_lock<std::mutex> lock(socketMutex);
        messageAvailable.wait(lock, [this]{ return !messageQueue.empty(); });
        
        if (messageQueue.empty()) {
            return 0; 
        }
        msg = messageQueue.front();
        messageQueue.pop();
        return 1;
    }

private:
    void receiveThread() {  
        while (1) {
            int clientSocket = accept(serverSocket, nullptr, nullptr);
            if (clientSocket >= 0) {
                char buffer[1024] = {0};
                int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
                if (bytesRead < 0) {
                    close(clientSocket);
                    throw std::runtime_error("Failed to receive message");
                }
                else {
                    buffer[bytesRead] = '\0';
                    {
                        std::lock_guard<std::mutex> lock(socketMutex);
                        messageQueue.emplace(std::string(buffer));
                    }
                    messageAvailable.notify_one();
                }
                close(clientSocket);
            }
            else {
                throw std::runtime_error("Error accepting connection");
            }
        }
    }
};

#endif // COMM_H


