// mosquitto_pub -h test.mosquitto.org -p 1883 -t "test/topic" -m "Hello, MQTT"
// mosquitto_sub -h test.mosquitto.org -p 1883 -t "test/topic"

// g++ Subscriber.cpp -o Subscriber -lpaho-mqttpp3 -lpaho-mqtt3a

// mosquitto_sub -h <broker_address> -t test/topic
// BROKER_ADDRESS_MQTT=tcp://localhost:1883       


// tcp://test.mosquitto.org:1883
// tcp://mqtt.eclipse.org:1883
// tcp::localhost:1883

#include <iostream>
#include <string>
#include "mqtt/async_client.h"

const std::string SERVER_ADDRESS{"tcp://localhost:1883"};
const std::string CLIENT_ID{"mqtt_cpp_subscriber"};
const std::string TOPIC{"test_dme"};
const int QOS = 1;

class callback : public virtual mqtt::callback
{
public:
    void connected(const std::string& cause) override
    {
        std::cout << "Connected to the broker!" << std::endl;
    }

    void connection_lost(const std::string& cause) override
    {
        std::cout << "Connection lost: " << cause << std::endl;
    }

    void message_arrived(mqtt::const_message_ptr msg) override
    {
        std::cout << "Message arrived on topic '" << TOPIC << "': " << msg->to_string() << std::endl;
    }

    void delivery_complete(mqtt::delivery_token_ptr token) override
    {
        std::cout << "Message delivery completed." << std::endl;
    }
};

int main()
{
    mqtt::async_client client(SERVER_ADDRESS, CLIENT_ID);
    callback cb;

    client.set_callback(cb);

    // try
    // {
        std::cout << "Connecting to the MQTT broker..." << std::endl;
        client.connect()->wait();
        std::cout << "Connected to the broker!" << std::endl;

        // Đăng ký nhận tin nhắn từ topic
        client.subscribe(TOPIC, QOS);

        std::cout << "Waiting for messages..." << std::endl;

        // Vòng lặp vô hạn để chương trình chạy mãi
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        // // Ngắt kết nối
        // client.disconnect()->wait();
        // std::cout << "Disconnected from the broker." << std::endl;
    // }
    // catch (const mqtt::exception& exc)
    // {
    //     std::cerr << "Error: " << exc.what() << std::endl;
    // }

    return 0;
}
