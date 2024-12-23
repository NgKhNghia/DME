#include "naimiTrehel_v2.h"
#include <random>

Config config;
Logger* logger = nullptr; 
ErrorSimulator error;

void simulateNode(int nodeId) {
    logger = new Logger(nodeId, true, true, true);
    std::string ip = config.getAddress(nodeId);
    int port = config.getPort(nodeId);
    std::shared_ptr<Comm> comm = std::make_shared<Comm>(nodeId, port);
    NaimiTrehel node(nodeId, ip, port, comm);
    node.initialize();
    // error.setErrorProbability(NETWORK_DISCONNECT_RECOVERABLE, 0.1);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(3, 5);

    auto startTime = std::chrono::steady_clock::now(); 

    while (true) {
        // if (nodeId == 3) {
        //     auto currentTime = std::chrono::steady_clock::now();
        //     if (std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime).count() >= 30) {
        //         logger->log(nodeId, -1, "Node 3 simulation stopped after 30 seconds");
        //         break;
        //     }
        // }

        std::this_thread::sleep_for(std::chrono::seconds(distrib(gen)));
        node.requestToken(); 
        {
            logger->log(nodeId, -1, "enter cs");
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        node.releaseToken();
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <nodeId>" << std::endl;
        return EXIT_FAILURE;
    }
    int nodeId = std::stoi(argv[1]);
    
    simulateNode(nodeId);

    return 0;
}
