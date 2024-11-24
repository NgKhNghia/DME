#include "naimiTrehel.h"
#include <random>

Config config;
Logger logger(true, true); 
ErrorSimulator error;

void simulateNode(int nodeId) {
    std::string ip = config.getNodeIp(nodeId);
    int port = config.getNodePort(nodeId);
    std::shared_ptr<Comm<std::string>> comm = std::make_shared<Comm<std::string>>(nodeId, port);
    NaimiTrehel node(nodeId, ip, port, comm);
    node.initialize();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, 5);

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(distrib(gen)));
        node.requestToken(); 
        {
            logger.log("NOTI", nodeId, "enter cs");
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
