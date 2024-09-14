#include <windows.h>
#include <fstream>
#include <dbt.h>
#include <filesystem>
#include <string>
#include <iostream>
#include <cstdint>
#include <iomanip>
#include <bitset>

//TODO: fix ULARGE_INTEGER 64bit QuadPart always def00000

const char* datFileLocation = "driveMask.txt";
static std::wofstream driveMask;

std::vector<std::wstring> splitAt(std::wstring source, char target) 
{
    int i = 0;
    int last = 0;
    std::vector<std::wstring> result;
    for (char c : source) {
        i++;
        if (c == target) {
            result.push_back(source.substr(last, i - 1));
            last = i;
        }
    }
    result.push_back(source.substr(last, i));

    return result;
}

struct diskSize 
{
public:
    ULARGE_INTEGER freeBytes, totalBytes, totalFreeBytes;
};

diskSize driveSpace(const std::wstring& drivePath) {
    diskSize result;

    ULARGE_INTEGER freeBytes = {};
    ULARGE_INTEGER totalBytes = {};
    ULARGE_INTEGER totalFreeBytes = {};

    if (GetDiskFreeSpaceEx(drivePath.c_str(), &freeBytes, &totalBytes, &totalFreeBytes)) 
    {
        result.freeBytes = freeBytes;
        result.totalBytes = totalBytes;
        result.totalFreeBytes = totalFreeBytes;
    }
    else {
        DWORD error = GetLastError();
        std::wcerr << L"GetDiskFreeSpaceEx failed for: " << drivePath << L" with error code: " << error << std::endl;

    }

    return result;
}

void copyFilesInDirectory(const std::wstring& path, const std::wstring& targetpath) 
{
    float progress = 0.0;
    uint64_t totalSize = driveSpace(path).totalBytes.QuadPart;
    uint64_t totalFreeSize = driveSpace(path).totalFreeBytes.QuadPart;
    uint64_t usedSpace = totalSize - totalFreeSize;
    if (!std::filesystem::exists(targetpath)) {
        if (std::filesystem::create_directory(targetpath)) {
            std::wcout << "Directory created successfully: " << targetpath << std::endl;
        }
        else {
            std::wcerr << "Failed to create directory: " << targetpath << std::endl;
            return;
        }
    }
    try {
        diskSize ds = driveSpace(path);
        std::cout << "---filecopy---" << std::endl;
        std::wcout << "DataSize: <" << (float)(ds.totalBytes.QuadPart - ds.totalFreeBytes.QuadPart) / 1000 << " megabytes>" << std::endl;
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            if (entry.path().filename() == "System Volume Information" ||
                entry.path().filename() == "$RECYCLE.BIN") {
                continue;
            }
            std::vector<std::wstring> pathSplit = splitAt(entry.path().wstring(), '\\');
            std::filesystem::copy_file(entry.path(), targetpath + L"\\" + pathSplit.at(pathSplit.size() - 1), std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
            progress += ((float)std::filesystem::file_size(entry.path()) / usedSpace) * 100;
            std::cout << progress << "%" << std::endl;
        }
        std::cout << "---done---" << std::endl;
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::wcerr << L"Filesystem error: " << e.what() << std::endl;
    }
}

std::wstring getVolumeSerialNumber(const std::wstring& drivePath) 
{
    DWORD serialNumber = 0;
    if (GetVolumeInformation(
        drivePath.c_str(),
        NULL, 0, &serialNumber, NULL, NULL, NULL, 0)) {
        std::wstringstream ss;
        ss << std::hex << serialNumber;
        return ss.str();
    }
    else {
        std::wcerr << L"Failed to get volume information. Error: " << GetLastError() << std::endl;
        return L"";
    }
}

bool contains(std::wifstream& stream, std::wstring target) 
{
    std::wstring line;
    while (std::getline(stream, line)) {
        if (line == target) {
            return true;
        }
    }
    return false;
}

bool qualifysForDownload(std::wstring& drivePath) 
{
    std::wstring serialNumber = getVolumeSerialNumber(drivePath) + L"|";
    std::wifstream driveInfo = std::wifstream(datFileLocation);
    return contains(driveInfo, serialNumber + std::to_wstring(driveSpace(drivePath).totalFreeBytes.QuadPart));
}

void logDrive(std::wstring& path) 
{
    driveMask << getVolumeSerialNumber(path) << L"|" << driveSpace(path).totalFreeBytes.QuadPart << std::endl;
}

static std::vector<std::wstring> volumeList;
void processDrives() 
{
    volumeList.clear();
    DWORD drives = GetLogicalDrives();
    std::cout << "Drive bitmask -> " << std::bitset<8>(drives) << std::endl;
    for (char drive = 'A'; drive <= 'Z'; ++drive) {
        if (drives & (1 << (drive - 'A'))) {
            std::wstring drivePath = std::wstring(1, drive) + L":\\";
            if (GetDriveType(drivePath.c_str()) == DRIVE_REMOVABLE) {
                volumeList.push_back(drivePath);
                for (std::wstring wstr : volumeList) {
                    if (!qualifysForDownload(drivePath)) {
                        logDrive(drivePath);
                        std::wstring target = std::wstring() + L"C:\\Users\\Public\\Documents\\";
                        target.append(getVolumeSerialNumber(drivePath));
                        copyFilesInDirectory(drivePath, target);
                    }
                }
            }
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) 
{
    switch (msg)
    {
    case WM_DEVICECHANGE:
        if (wparam == DBT_DEVICEARRIVAL || wparam == DBT_DEVICEREMOVECOMPLETE) {
            processDrives();
        }
        break;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

int main()
{
    driveMask = std::wofstream(datFileLocation, std::ios::app);

    const wchar_t CLASS_NAME[] = L"USBNotificationWindowClass";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = CLASS_NAME;

    if (RegisterClass(&wc) == 0) {
        std::wcerr << L"Failed to register window class" << std::endl;
        return 1;
    }

    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, L"USB Notification Window", 0,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, GetModuleHandle(NULL), NULL
    );

    if (hwnd == NULL) {
        std::cerr << "Failed to create window" << std::endl;
        return 1;
    }

    DEV_BROADCAST_DEVICEINTERFACE notificationFilter = {};
    notificationFilter.dbcc_size = sizeof(notificationFilter);
    notificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;

    HDEVNOTIFY hDeviceNotify = RegisterDeviceNotification(hwnd, &notificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (hDeviceNotify == NULL) {
        std::cerr << "Failed to create deviceNotificationService" << std::endl;
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (hDeviceNotify != NULL) UnregisterDeviceNotification(hDeviceNotify); 
}