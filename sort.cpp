#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <chrono>

using json = nlohmann::ordered_json;
using namespace std;

std::chrono::milliseconds parse_time_to_milliseconds(const std::string& timeStr) {
    std::tm tm = {};
    std::istringstream ss(timeStr);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");

    std::time_t timeInSeconds = std::mktime(&tm);
    return std::chrono::milliseconds(timeInSeconds * 1000);
}

bool compare_by_total_time_ms(const json& a, const json& b) {
    auto timeA = parse_time_to_milliseconds(a["time"]);
    auto timeB = parse_time_to_milliseconds(b["time"]);

    // Tổng thời gian: thời gian gốc + duration_ms (đã ở dạng mili giây)
    std::chrono::milliseconds totalA = timeA + std::chrono::milliseconds(a["duration_ms"].get<int>());
    std::chrono::milliseconds totalB = timeB + std::chrono::milliseconds(b["duration_ms"].get<int>());

    return totalA < totalB;
}

int main() {
    ifstream input("log.txt");
    ofstream output("output_log.txt");

    // Kiểm tra xem file input có mở được không
    if (!input.is_open()) {
        cerr << "Lỗi mở file log.txt!" << endl;
        return 1;
    }

    vector<json> logs;
    string line;

    while (getline(input, line)) {
        if (line.empty()) {
            continue; // Bỏ qua dòng trống
        }

        try {
            json log = json::parse(line); // Thử phân tích JSON
            logs.push_back(log);
        } catch (const json::parse_error& e) {
            cerr << "Lỗi phân tích dòng: " << line << " - " << e.what() << endl;
            continue; // Bỏ qua dòng không hợp lệ
        }
    }

    // Sắp xếp các dòng theo tổng thời gian (time + duration_ms) tính bằng mili giây
    sort(logs.begin(), logs.end(), compare_by_total_time_ms);

    // Ghi lại các dòng đã sắp xếp vào file mới
    for (const auto& log : logs) {
        output << log.dump() << endl;
    }

    // cout << "Logs đã được sắp xếp và ghi vào file output_log.txt" << endl;

    return 0;
}
