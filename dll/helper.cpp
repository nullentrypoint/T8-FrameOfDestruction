#include "helper.h"
#include <string>
#include <Windows.h>
#include <shlobj.h>

namespace helper {
    std::string GetCurrentDllDirectory() {
        char path[MAX_PATH];
        HMODULE hm = NULL;

        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&GetCurrentDllDirectory, &hm)) {
            return "";
        }

        if (GetModuleFileNameA(hm, path, sizeof(path)) == 0) {
            return "";
        }

        std::string fullPath(path);
        size_t lastSlash = fullPath.find_last_of("\\/");

        if (lastSlash != std::string::npos) {
            return fullPath.substr(0, lastSlash);
        }

        return "";
    }

    std::string GetWindowsDir() {
        char path[MAX_PATH];

        if (SHGetFolderPathA(NULL, CSIDL_WINDOWS, NULL, 0, path) != S_OK) {
            return ""; // Ошибка
        }

        return std::string(path);
    }

    std::string GetSettingsFolderPath(const std::string& folder) {
        char appDataPath[MAX_PATH];

        // Получаем путь к AppData/Local
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appDataPath))) {
            std::string folderPath = std::string(appDataPath) + "\\" + folder;

            CreateDirectoryA(folderPath.c_str(), NULL);

            return folderPath;
        }

        return "";
    }
}