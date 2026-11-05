#include "stdafx.h"
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include "namespaces.h"
#include "helper.h"
#include <format>

namespace console {
    bool isOpen = false;
    char inputBuf[256] = "";
    std::vector<std::string> items;
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
        items.push_back(str);
        scrollToBottom = true;
    }

    void AddLogError(const std::string& str) {
        items.push_back("error: " + str);
        scrollToBottom = true;
    }

    void ClearLog() {
        items.clear();
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
        for (const auto& item : items) {
            ImGui::TextWrapped(item.c_str());
        }
        ImGui::PopStyleVar();

        if (scrollToBottom) {
            ImGui::SetScrollHereY(1.0f);
            scrollToBottom = false;
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
