#include <iostream>
#include <thread>

void t1() {
    std::cout << "t1" << std::endl;
}

int main() {
    std::thread(t1).join();
    std::cout << "hello" << std::endl;
    return 0;
}
