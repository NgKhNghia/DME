#include <iostream>
#include <nlohmann/json.hpp>

// Alias để dễ sử dụng
using json = nlohmann::json;

int main() {
    // Tạo một JSON bên ngoài
    json outerJson;

    // Tạo một JSON bên trong
    json innerJson = {
        {"name", "Alice"},
        {"age", 25},
        {"skills", {"C++", "Python", "Java"}}
    };

    // Thêm JSON bên trong vào JSON bên ngoài
    outerJson["employee"] = innerJson;

    // Thêm một trường khác vào JSON bên ngoài
    outerJson["department"] = "IT";

    // In ra JSON
    std::cout << outerJson.dump() << std::endl;

    return 0;
}
