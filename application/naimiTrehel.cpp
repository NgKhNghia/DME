#include "naimiTrehel_v1.h"
#include "naimiTrehel_v2.h"
#include "naimiTrehel_v3.h"
#include <random>

Config config;
Logger* logger = nullptr; 
ErrorSimulator error;

void simulateNode(int nodeId) {
    logger = new Logger(nodeId, true, false, false);
    std::string ip = config.getAddress(nodeId);
    int port = config.getPort(nodeId);
    std::shared_ptr<Comm> comm = std::make_shared<Comm>(nodeId, port);
    // NaimiTrehelV1 node(nodeId, ip, port, comm);
    // NaimiTrehelV2 node(nodeId, ip, port, comm);
    NaimiTrehelV3 node(nodeId, ip, port, 2, comm);
    node.initialize();
    // error.setErrorProbability(NETWORK_ERROR, 0.9);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(3, 5);

    auto startTime = std::chrono::steady_clock::now(); 

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(distrib(gen)));
        error.simulateNetworkError();
        node.requestToken(); 
        {
            logger->log(nodeId, -1, "enter cs");
            std::this_thread::sleep_for(std::chrono::seconds(2));
            logger->log(nodeId, -1, "exit cs");
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
