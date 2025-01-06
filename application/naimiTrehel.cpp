// #include "naimiTrehel_v1.h"
// #include "naimiTrehel_v2.h"
#include "naimiTrehel_v3.h"
#include <random>

Config config;
Logger* logger = nullptr; 
ErrorSimulator error;

void simulateNode(int id) {
    logger = new Logger(id, true, true, false);
    std::string ip = config.getAddress(id);
    int port = config.getPort(id);
    std::shared_ptr<Comm> comm = std::make_shared<Comm>(id, port);
    // NaimiTrehelV1 node(id, ip, port, comm);
    // NaimiTrehelV2 node(id, ip, port, comm);
    NaimiTrehelV3 node(id, ip, port, 2, comm);
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
            logger->log("notice", "token", id, -1, "", "yes", "enter cs", "node " + std::to_string(id) + " enter cs");
            std::this_thread::sleep_for(std::chrono::seconds(2));
            logger->log("notice", "token", id, -1, "", "yes", "exit cs", "node " + std::to_string(id) + " exit cs");
        }
        node.releaseToken();
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <id>" << std::endl;
        return EXIT_FAILURE;
    }
    int id = std::stoi(argv[1]);
    
    simulateNode(id);

    return 0;
}
