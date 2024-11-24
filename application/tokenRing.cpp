// mainTokenRing.cpp
#include "tokenRing.h"
// #include <csignal>

// bool running = true;

// void signalHandler(int signal) {
//     if (signal == SIGINT) {
//         running = false;
//         logger.~Logger();
//     }
// }

Logger logger(true, true);
Config config;

void simulate(int id) {
    std::string ip = config.getNodeIp(id);
    int port = config.getNodePort(id);
    std::shared_ptr<Comm<std::string>> comm = std::make_shared<Comm<std::string>>(id, port);  
    TokenRingNode node(id, ip, port, comm); 

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, 5);

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(distrib(gen)));
        node.requestToken();
        {
            logger.log("NOTI", id, "enter cs");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        node.releaseToken();
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <node_id>" << std::endl;
        return 1;
    }
    // std::signal(SIGINT, signalHandler);
    int id = std::stoi(argv[1]);

    simulate(id);

    return 0;
}
