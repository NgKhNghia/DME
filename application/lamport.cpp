// g++ application/mainLamport.cpp -o application/mainLamport -lpthread -Iframework -Ialgorithm

#include "lamport.h"

Logger logger(true, true);
Config config;

void simulate(int id) {
    std::string ip = config.getNodeIp(id);
    int port = config.getNodePort(id);
    std::shared_ptr<Comm<std::string>> comm = std::make_shared<Comm<std::string>>(id, port);
    LamportNode node(id, ip, port, comm);

    std::random_device rd; 
    std::mt19937 gen(rd()); 
    std::uniform_int_distribution<> dis(1, 9); 
    std::this_thread::sleep_for(std::chrono::seconds(dis(gen))); 

    while (true) {
        node.requestCS();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (node.canEnterCS()) {
            {
                logger.log("NOTI", id, "enter cs");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            node.releaseCS();
        }
    }
}



int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <node_id>" << std::endl;
        return 1;
    }

    int id = std::stoi(argv[1]);
    simulate(id);    
    while (1);

    return 0;
}
