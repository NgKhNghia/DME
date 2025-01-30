// #include "naimiTrehel_v1.h"
#include "naimiTrehel_v2.h"
// #include "naimiTrehel_v3.h"
#include <random>

Config config;
Logger* logger = nullptr; 
ErrorSimulator error;

void simulateNode(int id) {
    logger = new Logger(id, true, false, false);
    std::string ip = config.getAddress(id);
    int port = config.getPort(id);
    std::shared_ptr<Comm> comm = std::make_shared<Comm>(id, port);
    // NaimiTrehelV1 node(id, ip, port, comm);
    NaimiTrehelV2 node(id, ip, port, comm);
    // NaimiTrehelV3 node(id, ip, port, 2, comm);
    
    // if (id == 3) 
    //     error.setErrorProbability(NETWORK_ERROR, 0.3);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(3, 5);

    // auto startTime = std::chrono::steady_clock::now(); 

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(distrib(gen)));
        // error.simulateNetworkError();
        node.requestToken(); 
        {
            json note;
            note["status"] = "ok";
            logger->log("notice", id, std::to_string(id) + " enter critical section", note);
            std::this_thread::sleep_for(std::chrono::seconds(distrib(gen)));
            logger->log("notice", id, std::to_string(id) + " exit critical section", note);
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
