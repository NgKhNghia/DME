#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include <vector>
#include <string>
#include <algorithm>

using json = nlohmann::ordered_json;
using namespace std;

bool compare_by_time_ms(const json& a, const json& b) {
    return a["time_ms"] < b["time_ms"];
}

int main() {
    ifstream input("log.txt");
    ofstream output("output_log.txt");

    if (!input.is_open()) {
        cerr << "Lỗi mở file!" << endl;
        return 1;
    }

    vector<json> logs;
    string line;

    while (getline(input, line)) {
        if (line.empty()) {
            continue; // Bỏ qua dòng trống
        }

        // try {
            json log = json::parse(line); // Thử phân tích JSON
            logs.push_back(log);
        // } catch (const json::parse_error& e) {
        //     cerr << "Lỗi phân tích dòng: " << line << " - " << e.what() << endl;
        //     continue; // Bỏ qua dòng không hợp lệ
        // }
    }

    // Sắp xếp các dòng theo trường "time_ms"
    sort(logs.begin(), logs.end(), compare_by_time_ms);

    // Ghi lại các dòng đã sắp xếp vào file mới
    for (const auto& log : logs) {
        output << log.dump() << std::endl << endl;
    }

    // cout << "Logs đã được sắp xếp và ghi vào file sorted_log.txt" << endl;
    return 0;
}
