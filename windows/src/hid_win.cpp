// Win32 HID: find the Jam by VID/PID, read input reports with overlapped ReadFile, write the LED
// reports, and reopen the device after a replug. No third-party library.
//
// Two handles are opened on the same device: a read handle (overlapped) used by the HID thread and
// a write handle used for the LED reports, so the two directions never share an OVERLAPPED.

#include "hid.h"

#include "jam.h"

#include <windows.h>
// clang-format off
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
}
// clang-format on

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

namespace hid {
namespace {

std::thread thread;
std::atomic<bool> running{false};
std::atomic<bool> isConnected{false};

HANDLE readHandle = INVALID_HANDLE_VALUE;
HANDLE writeHandle = INVALID_HANDLE_VALUE;
std::mutex writeMutex;

USHORT inputLength = 0;   // InputReportByteLength, report ID included
USHORT outputLength = 0;  // OutputReportByteLength, report ID included

std::function<void(const uint8_t*, size_t)> reportCallback;
std::function<void(bool)> connectionCallback;

std::string lastError(DWORD e) {
    char* msg = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, e, 0, reinterpret_cast<char*>(&msg), 0, nullptr);
    std::string s = msg ? msg : "";
    if (msg) LocalFree(msg);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == '.')) s.pop_back();
    return s + " (" + std::to_string(e) + ")";
}

// The device interface path of the Jam's vendor-defined HID collection, or empty.
std::wstring findJam() {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO info = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
                                         DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (info == INVALID_HANDLE_VALUE) return {};

    std::wstring found;
    SP_DEVICE_INTERFACE_DATA ifd{};
    ifd.cbSize = sizeof ifd;
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(info, nullptr, &hidGuid, i, &ifd); i++) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(info, &ifd, nullptr, 0, &needed, nullptr);
        if (!needed) continue;
        std::vector<uint8_t> buf(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(info, &ifd, detail, needed, nullptr, nullptr)) continue;

        // Open with no access: enough for the attributes, and it never fails on a busy device.
        HANDLE h = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        HIDD_ATTRIBUTES attr{};
        attr.Size = sizeof attr;
        bool match = HidD_GetAttributes(h, &attr) && attr.VendorID == jam::vendorId &&
                     attr.ProductID == jam::productId;
        if (match) {
            // The Jam exposes one vendor-defined collection (usage page 0xFF01). If a future
            // firmware adds more, take the one that carries the 49-byte input reports.
            PHIDP_PREPARSED_DATA pp = nullptr;
            if (HidD_GetPreparsedData(h, &pp)) {
                HIDP_CAPS caps{};
                if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS && caps.InputReportByteLength < 17) {
                    match = false;
                }
                HidD_FreePreparsedData(pp);
            }
        }
        CloseHandle(h);
        if (match) {
            found = detail->DevicePath;
            break;
        }
    }
    SetupDiDestroyDeviceInfoList(info);
    return found;
}

void closeDevice() {
    std::lock_guard<std::mutex> lock(writeMutex);
    if (writeHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(writeHandle);
        writeHandle = INVALID_HANDLE_VALUE;
    }
    if (readHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(readHandle);
        readHandle = INVALID_HANDLE_VALUE;
    }
}

bool openDevice() {
    std::wstring path = findJam();
    if (path.empty()) return false;

    HANDLE rd = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (rd == INVALID_HANDLE_VALUE) {
        static DWORD reported = 0;
        DWORD e = GetLastError();
        if (e != reported) {
            reported = e;
            jam::log("cannot open the Jam for reading: " + lastError(e));
            if (e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION)
                jam::log("  another program has it open exclusively (NI's agent? Controller Editor?)");
        }
        return false;
    }

    HANDLE wr = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, 0, nullptr);
    if (wr == INVALID_HANDLE_VALUE) {
        jam::log("cannot open the Jam for writing: " + lastError(GetLastError()));
        CloseHandle(rd);
        return false;
    }

    PHIDP_PREPARSED_DATA pp = nullptr;
    USHORT inLen = 49, outLen = 89;
    if (HidD_GetPreparsedData(rd, &pp)) {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
            inLen = caps.InputReportByteLength;
            outLen = caps.OutputReportByteLength;
            if (jam::verbose) {
                char buf[160];
                std::snprintf(buf, sizeof buf,
                              "HID caps: usage page %04X usage %04X, in %u out %u feature %u",
                              caps.UsagePage, caps.Usage, caps.InputReportByteLength,
                              caps.OutputReportByteLength, caps.FeatureReportByteLength);
                jam::log(buf);
            }
        }
        HidD_FreePreparsedData(pp);
    }

    {
        std::lock_guard<std::mutex> lock(writeMutex);
        readHandle = rd;
        writeHandle = wr;
        inputLength = inLen;
        outputLength = outLen;
    }
    return true;
}

void readLoop() {
    std::vector<uint8_t> buf(std::max<size_t>(inputLength, 64), 0);
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    while (running) {
        ResetEvent(ov.hEvent);
        DWORD got = 0;
        if (!ReadFile(readHandle, buf.data(), inputLength, &got, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) break;  // unplugged, or the handle went away
            // Wait in short slices so stop() does not leave us hanging on a silent device.
            bool done = false;
            while (true) {
                DWORD w = WaitForSingleObject(ov.hEvent, 200);
                if (w == WAIT_OBJECT_0) {
                    done = true;
                    break;
                }
                if (w != WAIT_TIMEOUT) break;  // the handle went away
                if (!running) {
                    CancelIo(readHandle);
                    WaitForSingleObject(ov.hEvent, 1000);
                    break;
                }
            }
            if (!done) break;
            if (!GetOverlappedResult(readHandle, &ov, &got, FALSE)) break;
        }
        if (got > 0 && reportCallback) reportCallback(buf.data(), got);
    }
    CancelIo(readHandle);
    CloseHandle(ov.hEvent);
}

void watchLoop() {
    while (running) {
        if (!openDevice()) {
            for (int i = 0; i < 10 && running; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        isConnected = true;
        if (connectionCallback) connectionCallback(true);
        readLoop();
        isConnected = false;
        closeDevice();
        if (running && connectionCallback) connectionCallback(false);
    }
    closeDevice();
}

}  // namespace

void start(std::function<void(const uint8_t*, size_t)> onReport,
           std::function<void(bool)> onConnection) {
    reportCallback = std::move(onReport);
    connectionCallback = std::move(onConnection);
    running = true;
    thread = std::thread(watchLoop);
}

bool write(uint8_t reportId, const uint8_t* data, size_t n) {
    std::lock_guard<std::mutex> lock(writeMutex);
    if (writeHandle == INVALID_HANDLE_VALUE) return false;

    // Windows wants exactly OutputReportByteLength bytes: report ID, the data, then zeros.
    size_t len = std::max<size_t>(outputLength, n + 1);
    std::vector<uint8_t> buf(len, 0);
    buf[0] = reportId;
    std::memcpy(buf.data() + 1, data, n);

    DWORD written = 0;
    if (WriteFile(writeHandle, buf.data(), static_cast<DWORD>(len), &written, nullptr)) return true;

    DWORD e = GetLastError();
    // Some HID devices have no interrupt OUT pipe; SET_REPORT over the control pipe still works.
    if (HidD_SetOutputReport(writeHandle, buf.data(), static_cast<ULONG>(len))) return true;
    if (jam::verbose) {
        char msg[64];
        std::snprintf(msg, sizeof msg, "LED write %02X failed: ", reportId);
        jam::log(msg + lastError(e));
    }
    return false;
}

bool connected() { return isConnected; }

void stop() {
    running = false;
    if (thread.joinable()) thread.join();
}

}  // namespace hid
