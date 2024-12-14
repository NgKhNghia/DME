// g++ Publisher.cpp -o Publisher -lpaho-mqttpp3 -lpaho-mqtt3a

#include <iostream>
#include <string>
#include "mqtt/async_client.h"

const std::string SERVER_ADDRESS{"tcp://test.mosquitto.org:1883"};
const std::string CLIENT_ID{"mqtt_cpp_publisher"};
const std::string TOPIC{"test_dme"};
const int QOS = 1;

int main()
{
    mqtt::async_client client(SERVER_ADDRESS, CLIENT_ID);

    client.connect()->wait();
    std::cout << "Connected to the broker!" << std::endl;

    std::string message = "Hello from the computer!";
    mqtt::message_ptr msg = mqtt::make_message(TOPIC, message, QOS, false);
    client.publish(msg)->wait_for(std::chrono::seconds(10));
    std::cout << "Message sent: " << message << std::endl;

    // client.disconnect()->wait();
    // std::cout << "Disconnected from the broker." << std::endl;

    return 0;
}
