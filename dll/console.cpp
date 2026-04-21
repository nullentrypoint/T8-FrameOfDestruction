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

    // Команды и их обработчики
    std::unordered_map<std::string, std::function<void(const std::vector<std::string>&)>> commands;

    // Безопасный callback
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
            // Находим начало текущего слова
            const char* wordEnd = data->Buf + data->CursorPos;
            const char* wordStart = wordEnd;
            while (wordStart > data->Buf) {
                const char c = *(wordStart - 1);
                if (c == ' ' || c == '\t' || c == ',' || c == ';')
                    break;
                wordStart--;
            }

            std::string currentWord(wordStart, wordEnd);

            // Ищем совпадения
            std::vector<std::string> matches;
            for (const auto& cmd : commands) {
                if (cmd.first.find(currentWord) == 0)
                    matches.push_back(cmd.first);
            }

            if (matches.size() == 1) {
                // Заменяем слово
                int startPos = (int)(wordStart - data->Buf);
                int endPos = (int)(wordEnd - wordStart);
                data->DeleteChars(startPos, endPos);
                data->InsertChars(startPos, matches[0].c_str());
                data->InsertChars(startPos + matches[0].size(), " ");
            }
            else if (matches.size() > 1) {
                // Показываем варианты
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

        // Добавляем в историю
        history.push_back(commandLine);
        if (history.size() > 50) {
            history.erase(history.begin());
        }
        historyPos = -1;

        // Ищем команду
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
        // Регистрируем команды
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
            };
        commands["version"] = [](const std::vector<std::string>& args) {
            console::AddLog("Version: " + globals::appVersion);
            };

        if (ImGui::SmallButton("Clear")) {
            ClearLog();
        }
        ImGui::Separator();

        if (menu::frameAdvantageHints.debugInfo) {
            if (ImGui::CollapsingHeader("Players states")) {
                try {
                    if (ImGui::TreeNode("Player 1")) {
                        if (p1 != nullptr) {
                            ImGui::Text("attack_type: %d %s", p1->attack_type, MoveInfoEnums::SafeEnum<MoveInfoEnums::AttackType>::to_string(p1->attack_type));
                            ImGui::Text("char_id: %d", p1->char_id);
                            ImGui::Text("complex_state: %d %s", p1->complex_state, MoveInfoEnums::SafeEnum<MoveInfoEnums::ComplexMoveStates>::to_string(p1->complex_state));
                            ImGui::Text("damage_taken: %d", p1->damage_taken);
                            ImGui::Text("frames_til_next_move: %d", p1->frames_til_next_move);
                            ImGui::Text("hit_outcome: %d %s", p1->hit_outcome, MoveInfoEnums::SafeEnum<MoveInfoEnums::HitOutcome>::to_string(p1->hit_outcome));
                            ImGui::Text("input_button: %d %s", p1->input_button, MoveInfoEnums::SafeEnum<MoveInfoEnums::InputAttackCodes>::to_string(p1->input_button));
                            ImGui::Text("input_direction: %d %s", p1->input_direction, MoveInfoEnums::SafeEnum<MoveInfoEnums::InputDirectionCodes>::to_string(p1->input_direction));
                            ImGui::Text("move_id: %d", p1->move_id);
                            ImGui::Text("move_timer: %d", p1->move_timer);
                            ImGui::Text("recovery: %d", p1->recovery);
                            ImGui::Text("simple_state: %d %s", p1->simple_state, MoveInfoEnums::SafeEnum<MoveInfoEnums::SimpleMoveStates>::to_string(p1->simple_state));
                            ImGui::Text("startup: %d", p1->startup);
                            ImGui::Text("stun_state: %d %s", p1->stun_state, MoveInfoEnums::SafeEnum<MoveInfoEnums::StunStates>::to_string(p1->stun_state));
                            ImGui::Text("throw_tech: %d %s", p1->throw_tech, MoveInfoEnums::SafeEnum<MoveInfoEnums::ThrowTechs>::to_string(p1->throw_tech));
                        }
                        ImGui::TreePop();
                    }

                    ImGui::Separator();

                    if (ImGui::TreeNode("Player 2")) {
                        if (p2 != nullptr) {
                            ImGui::Text("attack_type: %d %s", p2->attack_type, MoveInfoEnums::SafeEnum<MoveInfoEnums::AttackType>::to_string(p2->attack_type));
                            ImGui::Text("char_id: %d", p2->char_id);
                            ImGui::Text("complex_state: %d %s", p2->complex_state, MoveInfoEnums::SafeEnum<MoveInfoEnums::ComplexMoveStates>::to_string(p2->complex_state));
                            ImGui::Text("damage_taken: %d", p2->damage_taken);
                            ImGui::Text("frames_til_next_move: %d", p2->frames_til_next_move);
                            ImGui::Text("hit_outcome: %d %s", p2->hit_outcome, MoveInfoEnums::SafeEnum<MoveInfoEnums::HitOutcome>::to_string(p2->hit_outcome));
                            ImGui::Text("input_button: %d %s", p2->input_button, MoveInfoEnums::SafeEnum<MoveInfoEnums::InputAttackCodes>::to_string(p2->input_button));
                            ImGui::Text("input_direction: %d %s", p2->input_direction, MoveInfoEnums::SafeEnum<MoveInfoEnums::InputDirectionCodes>::to_string(p2->input_direction));
                            ImGui::Text("move_id: %d", p2->move_id);
                            ImGui::Text("move_timer: %d", p2->move_timer);
                            ImGui::Text("recovery: %d", p2->recovery);
                            ImGui::Text("simple_state: %d %s", p2->simple_state, MoveInfoEnums::SafeEnum<MoveInfoEnums::SimpleMoveStates>::to_string(p2->simple_state));
                            ImGui::Text("startup: %d", p2->startup);
                            ImGui::Text("stun_state: %d %s", p2->stun_state, MoveInfoEnums::SafeEnum<MoveInfoEnums::StunStates>::to_string(p2->stun_state));
                            ImGui::Text("throw_tech: %d %s", p2->throw_tech, MoveInfoEnums::SafeEnum<MoveInfoEnums::ThrowTechs>::to_string(p2->throw_tech));
                        }
                        ImGui::TreePop();
                    }
                }
                catch (...) {
                    console::AddLogError("exception2");
                }
            }

            ImGui::Separator();

            if (ImGui::CollapsingHeader("Console stats")) {
                ImGui::Text("Log items: %d", (int)items.size());
                ImGui::Text("inputBuf: %s", inputBuf);
                ImGui::Text("reclaimFocus: %d", (int)reclaimFocus);
            }
        }


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
                memset(inputBuf, 0, sizeof(inputBuf)); // Безопасная очистка
            }
            reclaimFocus = true;
        }
    }
}