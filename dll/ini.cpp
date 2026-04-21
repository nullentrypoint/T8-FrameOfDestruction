#include "stdafx.h"
#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <algorithm>
#include <sstream>
#include <filesystem>

namespace ini {
    // Вспомогательная функция для удаления пробелов
    std::string Config::trim(const std::string& str) {
        size_t start = str.find_first_not_of(" \t\n\r\f\v");
        if (start == std::string::npos) return "";

        size_t end = str.find_last_not_of(" \t\n\r\f\v");
        return str.substr(start, end - start + 1);
    }

    Config::Config(const std::string& file) : filepath(file) {}

    // Метод для чтения всех значений из INI файла
    void Config::Read() {
        data.clear();
        std::ifstream file(filepath);

        if (!file.is_open()) {
            throw std::runtime_error("can't open file: " + filepath);
        }

        std::string line;
        std::string currentSection;
        int lineNumber = 0;

        while (std::getline(file, line)) {
            lineNumber++;

            // Удаляем пробелы в начале и конце строки
            line = trim(line);

            // Пропускаем пустые строки и комментарии
            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }

            // Проверяем на секцию [section]
            if (line[0] == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.length() - 2);
                currentSection = trim(currentSection);
                continue;
            }

            // Парсим ключ=значение
            size_t equalPos = line.find('=');
            if (equalPos != std::string::npos) {
                std::string key = line.substr(0, equalPos);
                std::string value = line.substr(equalPos + 1);

                key = trim(key);
                value = trim(value);

                // Формируем полный ключ с учетом секции
                std::string fullKey = currentSection.empty() ? key : currentSection + "." + key;
                data[fullKey] = value;
            }
        }

        file.close();
    }

    // Метод для добавления нового ключа с данными
    void Config::SetKey(const std::string& key, const std::string& value, const std::string& section = "") {
        std::string fullKey = section.empty() ? key : section + "." + key;
        data[fullKey] = value;
        console::AddLog("added key: " + fullKey + " = " + value);
    }

    // Метод для сохранения всех значений в алфавитном порядке ключей
    bool Config::Save() {
        std::ofstream file(filepath);

        if (!file.is_open()) {
            console::AddLogError("can't open file for writing: " + filepath);
            return false;
        }

        // Сохраняем все данные в алфавитном порядке (map уже отсортирован)
        std::map<std::string, std::map<std::string, std::string>> sections;

        // Группируем ключи по секциям
        for (const auto& [fullKey, value] : data) {
            size_t dotPos = fullKey.find('.');
            if (dotPos != std::string::npos) {
                std::string section = fullKey.substr(0, dotPos);
                std::string key = fullKey.substr(dotPos + 1);
                sections[section][key] = value;
            }
            else {
                sections[""][fullKey] = value; // Без секции
            }
        }

        // Записываем в файл
        for (const auto& [section, keys] : sections) {
            if (!section.empty()) {
                file << "[" << section << "]" << std::endl;
            }

            for (const auto& [key, value] : keys) {
                file << key << "=" << value << std::endl;
            }

            if (!section.empty() && sections.size() > 1) {
                file << std::endl; // Разделяем секции пустой строкой
            }
        }

        file.close();
        console::AddLog("successfully saved " + std::to_string(data.size()) + " entries to " + filepath);
        return true;
    }

    // Метод для удаления ключа
    bool Config::RemoveKey(const std::string& key, const std::string& section = "") {
        std::string fullKey = section.empty() ? key : section + "." + key;
        auto it = data.find(fullKey);
        if (it != data.end()) {
            data.erase(it);
            console::AddLog("removed key: " + fullKey);
            return true;
        }
        console::AddLogError("key not found: " + fullKey);
        return false;
    }

    // Вспомогательный метод для получения значения по ключу
    std::string Config::GetValue(const std::string& key, const std::string& section = "") {
        std::string fullKey = section.empty() ? key : section + "." + key;
        auto it = data.find(fullKey);
        if (it != data.end()) {
            return it->second;
        }
        return "";
    }

    // Вспомогательный метод для проверки существования ключа
    bool Config::HasKey(const std::string& key, const std::string& section = "") {
        std::string fullKey = section.empty() ? key : section + "." + key;
        return data.find(fullKey) != data.end();
    }

    // Вспомогательный метод для вывода всех данных (для отладки)
    void Config::Print() {
        console::AddLog("=== INI File Contents (sorted by keys) ===");
        for (const auto& [key, value] : data) {
            console::AddLog(key + " = " + value);
        }
        console::AddLog("=============================================");
    }

    // Получить все ключи
    std::vector<std::string> Config::GetKeys() {
        std::vector<std::string> keys;
        for (const auto& [key, _] : data) {
            keys.push_back(key);
        }
        return keys;
    }

    // Получить все ключи из определенной секции
    std::vector<std::string> Config::GetKeysInSection(const std::string& section) {
        std::vector<std::string> keys;
        std::string prefix = section.empty() ? "" : section + ".";

        for (const auto& [fullKey, _] : data) {
            if (section.empty()) {
                // Если секция не указана, берем ключи без точки
                if (fullKey.find('.') == std::string::npos) {
                    keys.push_back(fullKey);
                }
            }
            else {
                // Ищем ключи с указанным префиксом
                if (fullKey.find(prefix) == 0 && fullKey.length() > prefix.length()) {
                    std::string key = fullKey.substr(prefix.length());
                    // Убеждаемся, что это не вложенная секция (нет дополнительных точек)
                    if (key.find('.') == std::string::npos) {
                        keys.push_back(key);
                    }
                }
            }
        }

        return keys;
    }

    // Очистить все данные
    void Config::Clear() {
        data.clear();
        console::AddLog("cleared all data");
    }

    // Получить количество записей
    size_t Config::Size() {
        return data.size();
    }

    std::string Config::GetPath() {
        return filepath;
    }
}