// g++ application/mainLamport.cpp -o application/mainLamport -lpthread -Iframework -Ialgorithm

#include "lamport.h"

Logger *logger = nullptr;
Config config;
ErrorSimulator error;

void simulate(int id) {
    logger = new Logger(id, true, true, false);
    std::string ip = config.getAddress(id);
    int port = config.getPort(id);
    std::shared_ptr<Comm> comm = std::make_shared<Comm>(id, port);
    Lamport node(id, ip, port, comm);

    std::random_device rd; 
    std::mt19937 gen(rd()); 
    std::uniform_int_distribution<> dis(1, 9); 
    // std::this_thread::sleep_for(std::chrono::seconds(dis(gen))); 

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(dis(gen)));
        node.requestPermission();
        {
            json note;
            note["status"] = "ok";
            note["error"] = "null";
            note["source"] = "null";
            note["dest"] = "null";
            logger->log("notice", id, std::to_string(id) + " enter critical section", note);
            std::this_thread::sleep_for(std::chrono::seconds(dis(gen)));
            logger->log("notice", id, std::to_string(id) + " exit critical section", note);
        }
        node.releasePermission();
    }
}



int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <id>" << std::endl;
        return EXIT_FAILURE;
    }

    int id = std::stoi(argv[1]);
    simulate(id);    

    return 0;
}
