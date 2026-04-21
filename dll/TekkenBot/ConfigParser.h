#pragma once
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>       // Добавляем для std::map
#include <vector>    // Добавляем для std::vector
#include <cctype>    // Для ::isspace
#include <cstdint>   // Для uintptr_t


class ConfigParser {
private:
    std::map<std::string, std::map<std::string, std::vector<uintptr_t>>> config_data;

    std::vector<uintptr_t> parse_hex_values(const std::string& value) {
        std::vector<uintptr_t> result;
        std::istringstream iss(value);
        std::string token;

        while (std::getline(iss, token, ' ')) {
            // Удаляем возможные пробелы и пустые токены
            token.erase(std::remove_if(token.begin(), token.end(), ::isspace), token.end());
            if (!token.empty()) {
                // Преобразуем hex строку в uintptr_t
                uintptr_t num;
                std::stringstream ss;
                ss << std::hex << token;
                ss >> num;
                result.push_back(num);
            }
        }
        return result;
    }

public:
    bool load(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }

        std::string current_section;
        std::string line;

        while (std::getline(file, line)) {
            // Удаляем комментарии
            line = line.substr(0, line.find_first_of("#;"));
            // Удаляем пробелы по краям
            line.erase(0, line.find_first_not_of(" \t"));
            line.erase(line.find_last_not_of(" \t") + 1);

            if (line.empty()) continue;

            // Обработка секций [section]
            if (line.front() == '[' && line.back() == ']') {
                current_section = line.substr(1, line.size() - 2);
                continue;
            }

            // Обработка ключ=значение
            size_t delimiter_pos = line.find('=');
            if (delimiter_pos != std::string::npos) {
                std::string key = line.substr(0, delimiter_pos);
                key.erase(key.find_last_not_of(" \t") + 1);

                std::string value = line.substr(delimiter_pos + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);

                if (!current_section.empty() && !key.empty()) {
                    config_data[current_section][key] = parse_hex_values(value);
                }
            }
        }

        return true;
    }

    const std::map<std::string, std::map<std::string, std::vector<uintptr_t>>>& data() const {
        return config_data;
    }
};