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
#include "VoiceChat/PeerPreferences.h"

namespace kv {
    ini::Config* userSettings;

    void ReadAll() {
        
        try {
            auto settingsPath = helper::GetSettingsFolderPath(globals::appName) + "\\user_settings.ini";
            if (!std::filesystem::exists(settingsPath)) {
                std::ofstream newFile(settingsPath);
            }
            userSettings = new ini::Config(settingsPath);
            userSettings->Read();
            VoiceChat::peerPreferences.Configure(settingsPath);
        }
        catch (const std::runtime_error& e) {
            throw std::runtime_error(std::format("userSettings->Read: {}", e.what()));
        }
        console::AddLog("successfully read user_settings " + std::to_string(userSettings->Size()) + " entries from " + userSettings->GetPath());
        
    }
};
