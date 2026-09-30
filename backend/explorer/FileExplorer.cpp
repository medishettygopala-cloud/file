#include "FileExplorer.h"

#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;

static std::string dialogError;

namespace {
#ifdef _WIN32
std::string utf8FromWide(const std::wstring& value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring wideFromUtf8(const std::string& value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

class ComScope {
public:
    ComScope() : initialized(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
    ~ComScope() { if (initialized) CoUninitialize(); }
    bool ok() const { return initialized; }
private:
    bool initialized;
};

std::vector<std::string> choose(bool folder) {
    std::vector<std::string> paths;
    dialogError.clear();
    ComScope com;
    if (!com.ok()) {
        dialogError = "Windows COM could not be initialized";
        return paths;
    }

    IFileOpenDialog* dialog = nullptr;
    HRESULT createResult = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(createResult)) {
        dialogError = "Windows native file dialog could not be created";
        return paths;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM;
    if (folder) options |= FOS_PICKFOLDERS;
    else options |= FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST;
    dialog->SetOptions(options);
    dialog->SetTitle(folder ? L"Select a folder for FileFind" : L"Select files for FileFind");

    // Let Windows place the picker independently of the console hosting the server.
    HRESULT result = dialog->Show(nullptr);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        dialog->Release();
        return paths;
    }
    if (FAILED(result)) {
        dialogError = "Windows file dialog failed to open";
        dialog->Release();
        return paths;
    }
    if (SUCCEEDED(result)) {
        IShellItemArray* items = nullptr;
        if (folder) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR value = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &value))) {
                    paths.push_back(utf8FromWide(value));
                    CoTaskMemFree(value);
                }
                item->Release();
            }
        } else if (SUCCEEDED(dialog->GetResults(&items))) {
            DWORD count = 0;
            items->GetCount(&count);
            for (DWORD i = 0; i < count; ++i) {
                IShellItem* item = nullptr;
                if (FAILED(items->GetItemAt(i, &item))) continue;
                PWSTR value = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &value))) {
                    paths.push_back(utf8FromWide(value));
                    CoTaskMemFree(value);
                }
                item->Release();
            }
            items->Release();
        }
    }
    dialog->Release();
    return paths;
}
#endif
}

namespace fileExplorer {

std::string selectFolder() {
#ifdef _WIN32
    auto paths = choose(true);
    return paths.empty() ? std::string{} : paths.front();
#else
    return {};
#endif
}

std::vector<std::string> selectFiles() {
#ifdef _WIN32
    return choose(false);
#else
    return {};
#endif
}

std::string lastError() {
    return dialogError;
}

bool openFile(const std::string& path) {
#ifdef _WIN32
    if (!fs::is_regular_file(fs::path(path))) return false;
    std::wstring value = wideFromUtf8(path);
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", value.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
    return false;
#endif
}

bool showInExplorer(const std::string& path) {
#ifdef _WIN32
    if (!fs::is_regular_file(fs::path(path))) return false;
    std::wstring value = L"/select,\"" + wideFromUtf8(path) + L"\"";
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"explorer.exe", value.c_str(), nullptr, SW_SHOWNORMAL)) > 32;
#else
    return false;
#endif
}

}
