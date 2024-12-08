#include <iostream>
#include <mqtt/async_client.h>
#include <string>

// Các thông số kết nối
const std::string SERVER_ADDRESS = "tcp://localhost:1883";  // Địa chỉ của broker
const std::string CLIENT_ID = "testClient";                 // Client ID của bạn
const std::string TOPIC = "test/topic";                     // Topic mà bạn muốn gửi/nhận tin nhắn
const int QOS = 1;                                         // Quality of Service level

class callback : public mqtt::callback {
public:
    void connection_lost(const std::string& cause) override {
        std::cout << "Kết nối bị mất: " << cause << std::endl;
    }

    void message_arrived(const std::string& topic, mqtt::message_ptr msg) override {
        std::cout << "Tin nhắn đến từ topic: " << topic << std::endl;
        std::cout << "Nội dung tin nhắn: " << msg->to_string() << std::endl;
    }

    void delivery_complete(mqtt::delivery_token_ptr token) override {
        std::cout << "Gửi tin nhắn thành công!" << std::endl;
    }
};

int main() {
    mqtt::async_client client(SERVER_ADDRESS, CLIENT_ID);
    callback cb;

    // Cấu hình các tùy chọn kết nối
    mqtt::connect_options connOpts;
    connOpts.set_clean_session(true);

    // Thiết lập callback
    client.set_callback(cb);

    try {
        // Kết nối đến broker
        std::cout << "Đang kết nối tới broker..." << std::endl;
        client.connect(connOpts)->wait();
        std::cout << "Kết nối thành công!" << std::endl;

        // Đăng ký nhận tin nhắn từ topic
        client.subscribe(TOPIC, QOS);

        // Gửi tin nhắn tới topic
        std::string message = "Xin chào từ C++ MQTT!";
        mqtt::message_ptr pubmsg = std::make_shared<mqtt::message>(message);
        pubmsg->set_qos(QOS);
        client.publish(TOPIC, pubmsg);

        // Chờ nhận tin nhắn (giữ client hoạt động trong 10 giây)
        std::this_thread::sleep_for(std::chrono::seconds(10));

        // Ngắt kết nối sau khi gửi/nhận xong
        client.disconnect()->wait();
        std::cout << "Ngắt kết nối thành công!" << std::endl;
    }
    catch (const mqtt::exception& exc) {
        std::cerr << "Lỗi MQTT: " << exc.what() << std::endl;
        return 1;
    }

    return 0;
}
