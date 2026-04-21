#pragma once
#include <string>

namespace helper {
	extern std::string GetCurrentDllDirectory();
	extern std::string GetWindowsDir();
	extern std::string GetSettingsFolderPath(const std::string& folder);
}
