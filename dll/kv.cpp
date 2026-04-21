#include "stdafx.h"
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <sstream>
#include <filesystem>
#include "namespaces.h"
#include "helper.h"

namespace kv {
    ini::Config* movesOverrides;
    ini::Config* userSettings;

    void ReadAll() {
        try {
            movesOverrides = new ini::Config(helper::GetCurrentDllDirectory() + "/config/moves_overrides.ini");
            movesOverrides->Read();
        }
        catch (const std::runtime_error& e) {
            throw std::runtime_error(std::format("movesOverrides->Read: {}", e.what()));
        }
        console::AddLog("successfully read moves_overrides " + std::to_string(movesOverrides->Size()) + " entries from " + movesOverrides->GetPath());
        
        try {
            auto settingsPath = helper::GetSettingsFolderPath(globals::appName) + "\\user_settings.ini";
            if (!std::filesystem::exists(settingsPath)) {
                std::ofstream newFile(settingsPath);
            }
            userSettings = new ini::Config(settingsPath);
            userSettings->Read();
        }
        catch (const std::runtime_error& e) {
            throw std::runtime_error(std::format("userSettings->Read: {}", e.what()));
        }
        console::AddLog("successfully read user_settings " + std::to_string(userSettings->Size()) + " entries from " + userSettings->GetPath());
        
    }
};