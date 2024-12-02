// error.h
#ifndef ERROR_H
#define ERROR_H

#include "log.h"
#include <random>
#include <string>
#include <chrono>
#include <thread>
#include <map>


enum ErrorType {
    NETWORK_DISCONNECT_RECOVERABLE,   // Mất kết nối mạng nhưng có thể khôi phục
    NETWORK_DISCONNECT_NON_RECOVERABLE, // Mất kết nối mạng và không thể khôi phục
    MESSAGE_LOSS,                     // Mất gói tin trên đường truyền
    MESSAGE_DELAY,                    // Trễ gói tin
    MESSAGE_MODIFIED                   // Gói tin đã bị thay đổi
};

class ErrorSimulator {
private:
    std::random_device rd;
    std::mt19937 gen;
    std::map<ErrorType, double> errorProbabilities;
    bool isDisconnected = false;      // Trạng thái mất mạng
    bool isSpoofing = false;          // Trạng thái giả mạo

public:
    ErrorSimulator() : gen(rd()) {
        // Khởi tạo xác suất mặc định cho từng loại lỗi
        // default = 0
        errorProbabilities[NETWORK_DISCONNECT_RECOVERABLE] = 0;
        errorProbabilities[NETWORK_DISCONNECT_NON_RECOVERABLE] = 0;
        errorProbabilities[MESSAGE_LOSS] = 0;
        errorProbabilities[MESSAGE_DELAY] = 0;
        errorProbabilities[MESSAGE_MODIFIED] = 0;
    }

    // Đặt xác suất cho một loại lỗi
    void setErrorProbability(ErrorType errorType, double probability) {
        errorProbabilities[errorType] = probability;
    }

    // Sinh lỗi ngẫu nhiên dựa trên xác suất
    bool triggerError(ErrorType errorType) {
        std::uniform_real_distribution<> dis(0.0, 1.0);
        return dis(gen) < errorProbabilities[errorType];
    }

    // // Kiểm tra nếu mất mạng
    // bool isNetworkDisconnected() const {
    //     return isDisconnected;
    // }

    // Giả lập lỗi mất kết nối mạng
    bool simulateNetworkError() {
        if (triggerError(NETWORK_DISCONNECT_RECOVERABLE)) {
            std::this_thread::sleep_for(std::chrono::seconds(5));  // Tạm thời mất mạng
            return true;
        }
        // else if (triggerError(NETWORK_DISCONNECT_NON_RECOVERABLE)) {
        //     // isDisconnected = true;
        //     // Không khôi phục được
        //     // return true;

        // }
        return false;
    }

    // Giả lập lỗi mất gói tin
    bool simulateMessageLoss() {
        if (triggerError(MESSAGE_LOSS)) {
            return true;
        }
        return false;
    }

    // Giả lập lỗi trễ gói tin
    bool simulateMessageDelay() {
        if (triggerError(MESSAGE_DELAY)) {
            return true;
        }
        return false;
    }

    // Giả lập gói tin đã bị thay đổi
    bool simulateMessageModified(std::string& messageContent) {
        if (triggerError(MESSAGE_MODIFIED)) {
            messageContent = messageContent + "(Modified)";  // Thay đổi nội dung tin nhắn
            return true;
        }
        return false;
    }

    
};

#endif // ERROR_H
