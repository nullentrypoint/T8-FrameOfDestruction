#include "stdafx.h"
#include <cassert>
#include <thread>
#include <iostream>
namespace console { extern std::vector<std::string> items; void ClearLog(); }
namespace globals { const std::string appVersion = "test"; }
namespace menu { void LoadSettings() {} }
namespace kv { void ReadAll() {} }
namespace overlay {
    uint64_t AddFlyingText(const std::string&, const std::optional<ScreenAnchor>&,
        const std::string&, const std::optional<WorldPoint>&) { return 0; }
}
int main() {
    console::ClearLog();
    console::AddLog("100% ready\nsecond line\r\n");
    assert(console::items.size() == 2);
    for (const auto& line : console::items) {
        assert(line.size() >= 15 && line[0] == '[' && line[3] == ':' && line[6] == ':' && line[9] == '.' && line[13] == ']');
        assert(line.find('\n') == std::string::npos && line.find('\r') == std::string::npos);
    }
    assert(console::items[0].substr(15) == "100% ready");
    console::AddLogError("device failed");
    assert(console::items.back().substr(15) == "[ERROR] device failed");
    std::array<std::thread, 4> writers;
    for (auto& writer : writers) writer = std::thread([] {
        for (int i = 0; i < 1000; ++i) console::AddLog("concurrent event");
    });
    for (auto& writer : writers) writer.join();
    assert(console::items.size() == 1000);
    for (const auto& line : console::items) assert(line.substr(15) == "concurrent event");
    console::ClearLog();
    assert(console::items.empty());
    std::cout << "Console timestamps, multiline, errors, concurrent writers and history bound: PASS\n";
}
