// config.h
#ifndef CONFIG_H
#define CONFIG_H

#include "dotenv.h"
#include <map>

class Config {
private:
    int totalNodes;
    std::string serverAddressMqtt;
    std::map<int, std::pair<std::string, int>> nodeConfigs; // cau hinh cho tung nut: id - ip - port

public:
    Config() {
        dotenv::init("config.env");
        loadConfigurations();
    }

    int getTotalNodes() const {
        return totalNodes;
    }

    std::string getServerAddressMqtt() {
        return serverAddressMqtt;
    }

    std::string getAddress(int nodeId) const {
        auto it = nodeConfigs.find(nodeId);
        if (it != nodeConfigs.end()) {
            return it->second.first;
        }
        throw std::runtime_error("Node " + std::to_string(nodeId) + " not found");
    }

    int getPort(int nodeId) const {
        auto it = nodeConfigs.find(nodeId);
        if (it != nodeConfigs.end()) {
            return it->second.second;
        }
        throw std::runtime_error("Node " + std::to_string(nodeId) + " not found");
    }

    std::map<int, std::pair<std::string, int>> getNodeConfigs() { 
        return nodeConfigs;
    }
    
private:
    void loadConfigurations() { // load file config.env
        try {
            totalNodes = std::stoi(dotenv::getenv("TOTAL_NODES"));
            if (totalNodes <= 0) {
                throw std::runtime_error("TOTAL_NODES must be greater than 0\n");
            }
            serverAddressMqtt = dotenv::getenv("SERVER_ADDRESS_MQTT", "");
            for (int i = 1; i <= totalNodes; i++) {
                std::string ip = dotenv::getenv(("NODE_" + std::to_string(i) + "_IP").c_str());
                int port = std::stoi(dotenv::getenv(("NODE_" + std::to_string(i) + "_PORT").c_str()));
                if (ip.empty() || port <= 0 || port > 65535) {
                    throw std::runtime_error("Invalid address or port for node " + std::to_string(i) + "\n");
                }

                nodeConfigs[i] = std::make_pair(ip, port);
            }
        }
        catch (const std::exception &e) {
            std::cerr << "Configuration error: " << e.what() << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    
};

extern Config config;


#endif // CONFIG_H