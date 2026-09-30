#pragma once

#include <string>
#include <vector>

namespace fileExplorer {

std::string selectFolder();
std::vector<std::string> selectFiles();
std::string lastError();
bool openFile(const std::string& path);
bool showInExplorer(const std::string& path);

}
