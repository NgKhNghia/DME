#include "naimiTrehel.h"
#include <random>

Config config;
Logger logger(true, true, true); 
ErrorSimulator error;

void simulateNode(int nodeId) {
    std::string ip = config.getAddress(nodeId);
    int port = config.getPort(nodeId);
    std::shared_ptr<Comm> comm = std::make_shared<Comm>(nodeId, port);
    NaimiTrehel node(nodeId, ip, port, comm);
    node.initialize();
    error.setErrorProbability(NETWORK_DISCONNECT_RECOVERABLE, 0.9);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, 5);

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(distrib(gen)));
        node.requestToken(); 
        {
            logger.log(nodeId, -1, -1, "enter cs");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
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
