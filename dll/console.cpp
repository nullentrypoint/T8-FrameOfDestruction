#include "stdafx.h"
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include "namespaces.h"
#include "helper.h"
#include "VoiceChat/VoicePresentation.h"
#include "VoiceChat/LoungeLayout.h"
#include <format>
#include <mutex>

namespace console {
    bool isOpen = false;
    char inputBuf[256] = "";
    std::vector<std::string> items;
    static std::mutex logMutex;
    std::vector<std::string> history;
    int historyPos = -1;
    bool scrollToBottom = false;
    bool reclaimFocus = false;

    std::shared_ptr<PlayerSnapshot> p1 = nullptr;
    std::shared_ptr<PlayerSnapshot> p2 = nullptr;

    // ������� � �� �����������
    std::unordered_map<std::string, std::function<void(const std::vector<std::string>&)>> commands;

    // ���������� callback
    static int TextEditCallback(ImGuiInputTextCallbackData* data) {
        if (!data) return 0;

        switch (data->EventFlag) {
        case ImGuiInputTextFlags_CallbackHistory:
        {
            const int prevHistoryPos = historyPos;
            if (data->EventKey == ImGuiKey_UpArrow) {
                if (historyPos == -1)
                    historyPos = (int)history.size() - 1;
                else if (historyPos > 0)
                    historyPos--;
            }
            else if (data->EventKey == ImGuiKey_DownArrow) {
                if (historyPos != -1) {
                    historyPos++;
                    if (historyPos >= (int)history.size())
                        historyPos = -1;
                }
            }

            if (prevHistoryPos != historyPos && historyPos >= 0 && historyPos < (int)history.size()) {
                const char* historyStr = history[historyPos].c_str();
                data->DeleteChars(0, data->BufTextLen);
                data->InsertChars(0, historyStr);
            }
        }
        break;

        case ImGuiInputTextFlags_CallbackCompletion:
        {
            // ������� ������ �������� �����
            const char* wordEnd = data->Buf + data->CursorPos;
            const char* wordStart = wordEnd;
            while (wordStart > data->Buf) {
                const char c = *(wordStart - 1);
                if (c == ' ' || c == '\t' || c == ',' || c == ';')
                    break;
                wordStart--;
            }

            std::string currentWord(wordStart, wordEnd);

            // ���� ����������
            std::vector<std::string> matches;
            for (const auto& cmd : commands) {
                if (cmd.first.find(currentWord) == 0)
                    matches.push_back(cmd.first);
            }

            if (matches.size() == 1) {
                // �������� �����
                int startPos = (int)(wordStart - data->Buf);
                int endPos = (int)(wordEnd - wordStart);
                data->DeleteChars(startPos, endPos);
                data->InsertChars(startPos, matches[0].c_str());
                data->InsertChars(startPos + matches[0].size(), " ");
            }
            else if (matches.size() > 1) {
                // ���������� ��������
                AddLog("possible completions:");
                for (const auto& match : matches)
                    AddLog("- " + match);
            }
        }
        break;
        }
        return 0;
    }

    void AddLog(const std::string& str) {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        const auto prefix = std::format("[{:02}:{:02}:{:02}.{:03}] ", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
        std::lock_guard lock(logMutex);
        size_t begin = 0;
        do {
            const auto end = str.find('\n', begin);
            auto line = str.substr(begin, end == std::string::npos ? end : end - begin);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() > 4096) line = line.substr(0, 4096) + " [truncated]";
            if (items.size() >= 1000) items.erase(items.begin());
            items.push_back(prefix + line);
            if (end == std::string::npos) break;
            begin = end + 1;
        } while (begin < str.size());
        scrollToBottom = true;
    }

    void AddLogError(const std::string& str) {
        AddLog("[ERROR] " + str);
    }

    void ClearLog() {
        std::lock_guard lock(logMutex);
        items.clear();
        scrollToBottom = false;
    }

    void ExecCommand(const std::string& commandLine) {
        AddLog("> " + commandLine);

        std::vector<std::string> tokens;
        std::istringstream iss(commandLine);
        std::string token;

        while (iss >> token) {
            tokens.push_back(token);
        }

        if (tokens.empty()) return;

        // ��������� � �������
        history.push_back(commandLine);
        if (history.size() > 50) {
            history.erase(history.begin());
        }
        historyPos = -1;

        // ���� �������
        std::string cmd = tokens[0];
        auto it = commands.find(cmd);
        if (it != commands.end()) {
            try {
                it->second(tokens);
            }
            catch (const std::exception& e) {
                AddLogError(e.what());
            }
        }
        else {
            AddLog("unknown command: '" + cmd + "'");
        }
    }

    void Init(ImGuiIO& io) {
        io.MouseDrawCursor = isOpen;

        if (!isOpen) return;

        ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
        ImGui::Begin("Console", &isOpen);

        DrawConsoleContent();

        //ImGui::SetItemDefaultFocus();
        //if (reclaimFocus) {
        //    ImGui::SetKeyboardFocusHere(-1);
        //    reclaimFocus = false;
        //}

        ImGui::End();
    }

    void DrawConsoleContent() {
        // ������������ �������
        commands["clear"] = [](const std::vector<std::string>&) { ClearLog(); };
        commands["voice_preview"] = [](const std::vector<std::string>&) {
            VoiceChat::avatarPreviewUntil = GetTickCount64() + 10000;
            AddLog("Voice avatar preview: 10 seconds. Drag either avatar with the left mouse button; the other mirrors it. Keep the menu open for mouse input. Final coordinates will be logged.");
        };
        commands["voice_debug"] = [](const std::vector<std::string>& args) {
            if (args.size() == 1) {
                AddLog(std::format("Voice debug log: {}. File: {}\\steam_diagnostics.log",
                    VoiceChat::voiceDebugLog ? "on" : "off", helper::GetCurrentDllDirectory()));
                return;
            }
            if (args.size() != 2 || (args[1] != "on" && args[1] != "off")) {
                AddLogError("Usage: voice_debug [on|off]"); return;
            }
            VoiceChat::voiceDebugLog = args[1] == "on";
            if (kv::userSettings) {
                kv::userSettings->SetKey("debug_log", VoiceChat::voiceDebugLog ? "true" : "false", "VoiceChat");
                if (!kv::userSettings->Save()) AddLogError("Logging changed, but saving settings failed.");
            }
            AddLog(std::format("Voice debug log {}: {}\\steam_diagnostics.log",
                args[1], helper::GetCurrentDllDirectory()));
        };
        commands["lounge_info"] = [](const std::vector<std::string>&) {
            VoiceChat::LoungeLayout layout;
            std::string error;
            const auto path = std::filesystem::path(helper::GetCurrentDllDirectory()) /
                "T8FrameOfDestruction" / "config" / "memory_address.ini";
            if (!VoiceChat::LoadLoungeLayout(path, layout, error)) { AddLogError(error); return; }
            const auto read = [](void*, uintptr_t address, void* output, size_t size) -> bool {
                SIZE_T copied = 0;
                return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                    output, size, &copied) && copied == size;
            };
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"Polaris-Win64-Shipping.exe"));
            const auto snapshot = VoiceChat::LoungeReader(base, layout, read, nullptr).Read();
            AddLog(std::string("[Lounge] ") + VoiceChat::LoungeStatusName(snapshot.status));
            if (snapshot.status != VoiceChat::LoungeReadStatus::Ok) return;
            AddLog(std::format("[Lounge] room={} participants={} local=({:.1f}, {:.1f}, {:.1f})",
                snapshot.roomKey, snapshot.participants.size(), snapshot.localPosition.x,
                snapshot.localPosition.y, snapshot.localPosition.z));
            for (const auto& peer : snapshot.participants)
                AddLog(std::format("[Lounge] {} | platform={} Steam candidate={} | distance={:.1f} game units",
                    peer.name, peer.platform, peer.steamIdCandidate ? "yes" : "no", peer.distance));
        };
        commands["lounge_avatar_distance"] = [](const std::vector<std::string>& args) {
            if (args.size() == 1) {
                AddLog(std::format("Lounge avatar distance: {} game units from camera (0 = hidden). Usage: lounge_avatar_distance <0..100000|reset>",
                    VoiceChat::loungeAvatarDistance.load()));
                return;
            }
            int distance = static_cast<int>(VoiceChat::LoungeAvatarMaxDistance);
            if (args.size() != 2 || (args[1] != "reset" && !VoiceChat::ParseLoungeAvatarDistance(args[1], distance))) {
                AddLogError("Usage: lounge_avatar_distance <0..100000|reset>. Use a whole number in game units; 0 hides world avatars.");
                return;
            }
            VoiceChat::loungeAvatarDistance = distance;
            AddLog(std::format("Lounge avatar distance: {} game units from camera{}", distance, distance ? "" : " (hidden)"));
            if (!kv::userSettings) { AddLog("Applied for this session; settings file is not available."); return; }
            kv::userSettings->SetKey("lounge_avatar_distance", std::to_string(distance), "VoiceChat");
            if (!kv::userSettings->Save()) AddLogError("Distance applied, but saving user settings failed.");
        };
        commands["help"] = [](const std::vector<std::string>&) {
            console::AddLog("available commands:");
            for (const auto& cmd : console::commands) {
                console::AddLog("- " + cmd.first);
            }
            };
        commands["echo"] = [](const std::vector<std::string>& args) {
            std::string result;
            for (size_t i = 1; i < args.size(); i++) {
                if (i > 1) result += " ";
                result += args[i];
            }
            console::AddLog(result);
            };
        commands["overlay"] = [](const std::vector<std::string>& args) {
            std::string result;
            for (size_t i = 1; i < args.size(); i++) {
                if (i > 1) result += " ";
                result += args[i];
            }
            overlay::AddFlyingText(result);
            };
        commands["info"] = [](const std::vector<std::string>& args) {
            console::AddLog("CurrentDllDirectory: " + helper::GetCurrentDllDirectory());
            console::AddLog("WindowsDirectory: " + helper::GetWindowsDir());
            };
        commands["kv_readall"] = [](const std::vector<std::string>& args) {
            kv::ReadAll();
            menu::LoadSettings();
            console::AddLog("Settings reloaded and applied.");
            };
        commands["version"] = [](const std::vector<std::string>& args) {
            console::AddLog("Version: " + globals::appVersion);
            };

        if (ImGui::SmallButton("Clear")) {
            ClearLog();
        }
        ImGui::Separator();

        const float footerHeightToReserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footerHeightToReserve), false, ImGuiWindowFlags_HorizontalScrollbar);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));
        std::vector<std::string> snapshot;
        bool scroll = false;
        {
            std::lock_guard lock(logMutex);
            snapshot = items;
            scroll = scrollToBottom;
            scrollToBottom = false;
        }
        for (const auto& item : snapshot) {
            const bool error = item.find("[ERROR]") != std::string::npos;
            if (error) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, .35f, .35f, 1));
            ImGui::TextWrapped("%s", item.c_str());
            if (error) ImGui::PopStyleColor();
        }
        ImGui::PopStyleVar();

        if (scroll) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::Separator();

        ImGuiInputTextFlags inputTextFlags = ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_CallbackCompletion |
            ImGuiInputTextFlags_CallbackHistory;

        if (ImGui::InputText("Input", inputBuf, IM_ARRAYSIZE(inputBuf), inputTextFlags,
            [](ImGuiInputTextCallbackData* data) -> int { return TextEditCallback(data); })) {
            if (inputBuf[0]) {
                ExecCommand(inputBuf);
                memset(inputBuf, 0, sizeof(inputBuf)); // ���������� �������
            }
            reclaimFocus = true;
        }
    }
}
