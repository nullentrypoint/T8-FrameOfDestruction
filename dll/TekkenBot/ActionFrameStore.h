#pragma once
#include <Windows.h>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <map>
#include <string>

namespace ActionFrameStore {
    using Frames = std::map<std::pair<int, int>, int>;

    inline bool parse_id(const std::string& text, int& value) {
        auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc() && result.ptr == text.data() + text.size() && value >= 0;
    }

    inline Frames load(const std::string& path) {
        std::ifstream input(path);
        Frames frames;
        char token;
        if (!(input >> token) || token != '{') return {};
        input >> std::ws;
        if (input.peek() != '}') {
            while (input) {
                std::string key;
                int frame, character, move;
                if (input.peek() != '"' || !(input >> std::quoted(key) >> token >> frame) ||
                    token != ':' || frame <= 0) return {};
                auto colon = key.find(':');
                if (colon == std::string::npos ||
                    !parse_id(key.substr(0, colon), character) ||
                    !parse_id(key.substr(colon + 1), move)) return {};
                frames[{character, move}] = frame;
                input >> std::ws;
                if (input.peek() == '}') break;
                if (!(input >> token) || token != ',') return {};
                input >> std::ws;
            }
        }
        if (!(input >> token) || token != '}') return {};
        input >> std::ws;
        return input.eof() ? frames : Frames{};
    }

    inline bool save(const std::string& path, const Frames& frames) {
        const auto temporary = path + ".tmp";
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) return false;
        output << "{\n";
        bool first = true;
        for (const auto& [key, frame] : frames) {
            if (!first) output << ",\n";
            first = false;
            output << "  \"" << key.first << ':' << key.second << "\": " << frame;
        }
        output << "\n}\n";
        output.close();
        if (!output) {
            DeleteFileA(temporary.c_str());
            return false;
        }
        if (!MoveFileExA(temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileA(temporary.c_str());
            return false;
        }
        return true;
    }
}
