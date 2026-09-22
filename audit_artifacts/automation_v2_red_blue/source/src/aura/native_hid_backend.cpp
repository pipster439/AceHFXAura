#include "aura/native_hid_backend.h"
#include "utils/logger.h"
#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>

extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}

#include <algorithm>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

namespace aura {

namespace {

std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return std::string();
    std::string strTo(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), &strTo[0], sizeNeeded, nullptr, nullptr);
    return strTo;
}

std::wstring ToLowerW(const std::wstring& s) {
    std::wstring result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::towlower);
    return result;
}

} // namespace

NativeHidBackend::NativeHidBackend()
    : hDevice_(INVALID_HANDLE_VALUE),
      hEvent_(nullptr) {}

NativeHidBackend::~NativeHidBackend() {
    Disconnect();
}

bool NativeHidBackend::IsConnected() const {
    return hDevice_ != INVALID_HANDLE_VALUE;
}

void NativeHidBackend::Disconnect() {
    if (hDevice_ != INVALID_HANDLE_VALUE) {
        CloseHandle(hDevice_);
        hDevice_ = INVALID_HANDLE_VALUE;
    }
    if (hEvent_) {
        CloseHandle(hEvent_);
        hEvent_ = nullptr;
    }
    device_path_.clear();
}

bool NativeHidBackend::IsTargetLightingEndpoint(
    uint16_t vid, uint16_t pid,
    uint16_t usagePage, uint16_t usage,
    uint16_t outputReportLen,
    const std::wstring& devPath)
{
    if (vid != ASUS_VID || pid != FALCHION_ACE_HFX_PID) {
        return false;
    }
    if (usagePage != LIGHTING_USAGE_PAGE || usage != LIGHTING_USAGE) {
        return false;
    }
    if (outputReportLen != HID_REPORT_SIZE) {
        return false;
    }
    std::wstring lowerPath = ToLowerW(devPath);
    if (lowerPath.find(L"mi_01") == std::wstring::npos) {
        return false;
    }
    return true;
}

std::array<uint8_t, HID_REPORT_SIZE> NativeHidBackend::BuildReport(
    const uint8_t* indices,
    const uint8_t* colors_rgb,
    size_t count)
{
    std::array<uint8_t, HID_REPORT_SIZE> report{};
    report.fill(0x00);

    // Byte 0: Windows HID Report ID (0x00)
    report[0] = 0x00;

    // Byte 1..2: ASUS Direct RGB Command Header (0xC0 0x81, little-endian: 0x81C0)
    report[1] = 0xC0;
    report[2] = 0x81;

    // Byte 3..4: LED Entry Count (little-endian uint16)
    size_t clamped_count = std::min(count, MAX_LEDS_PER_HID_PACKET);
    report[3] = static_cast<uint8_t>(clamped_count & 0xFF);
    report[4] = static_cast<uint8_t>((clamped_count >> 8) & 0xFF);

    // Byte 5..64: 4-byte LED entries [Index, R, G, B]
    for (size_t i = 0; i < clamped_count; ++i) {
        size_t base = 5 + i * 4;
        report[base + 0] = indices ? indices[i] : 0;
        if (colors_rgb) {
            report[base + 1] = colors_rgb[i * 3 + 0];
            report[base + 2] = colors_rgb[i * 3 + 1];
            report[base + 3] = colors_rgb[i * 3 + 2];
        }
    }

    return report;
}

bool NativeHidBackend::Connect() {
    Disconnect();
    last_error_.clear();

    // Check ASUS Exclusive Mutex (warning only, do not terminate)
    HANDLE hMutex = OpenMutexW(SYNCHRONIZE, FALSE, L"Global\\ExclusiveExecution_ROGKB");
    if (hMutex != nullptr) {
        LOG_WARN("ASUS 键盘 HAL 互斥量 (Global\\ExclusiveExecution_ROGKB) 处于活动状态，Armoury Crate 可能正在运行");
        CloseHandle(hMutex);
    }

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO hDevInfo = SetupDiGetClassDevsW(
        &hidGuid,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );

    if (hDevInfo == INVALID_HANDLE_VALUE) {
        last_error_ = "SetupDiGetClassDevsW 失败，错误码: " + std::to_string(::GetLastError());
        LOG_ERROR(last_error_);
        return false;
    }

    SP_DEVICE_INTERFACE_DATA ifData{};
    ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    std::wstring matchedPath;
    NativeHidDeviceInfo matchedInfo;

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(hDevInfo, nullptr, &hidGuid, i, &ifData); ++i) {
        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(hDevInfo, &ifData, nullptr, 0, &requiredSize, nullptr);
        if (requiredSize == 0) continue;

        std::vector<BYTE> detailBuffer(requiredSize);
        auto* pDetail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuffer.data());
        pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(hDevInfo, &ifData, pDetail, requiredSize, nullptr, nullptr)) {
            continue;
        }

        std::wstring devPath = pDetail->DevicePath;

        HANDLE hQuery = CreateFileW(
            devPath.c_str(),
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );

        if (hQuery == INVALID_HANDLE_VALUE) {
            continue;
        }

        HIDD_ATTRIBUTES attrib{};
        attrib.Size = sizeof(HIDD_ATTRIBUTES);
        if (HidD_GetAttributes(hQuery, &attrib)) {
            if (attrib.VendorID == ASUS_VID && attrib.ProductID == FALCHION_ACE_HFX_PID) {
                PHIDP_PREPARSED_DATA preparsed = nullptr;
                if (HidD_GetPreparsedData(hQuery, &preparsed)) {
                    HIDP_CAPS caps{};
                    if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
                        if (IsTargetLightingEndpoint(attrib.VendorID, attrib.ProductID,
                                                     caps.UsagePage, caps.Usage,
                                                     caps.OutputReportByteLength, devPath)) {
                            matchedPath = devPath;
                            matchedInfo.path = devPath;
                            matchedInfo.vid = attrib.VendorID;
                            matchedInfo.pid = attrib.ProductID;
                            matchedInfo.usagePage = caps.UsagePage;
                            matchedInfo.usage = caps.Usage;
                            matchedInfo.outputReportByteLength = caps.OutputReportByteLength;
                            matchedInfo.isMatch = true;
                        }
                    }
                    HidD_FreePreparsedData(preparsed);
                }
            }
        }
        CloseHandle(hQuery);

        if (matchedInfo.isMatch) {
            break;
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);

    if (!matchedInfo.isMatch) {
        last_error_ = "未找到匹配的目标灯光端点 (VID: 0x0B05, PID: 0x1B7E, MI_01, UsagePage: 0xFF00, Usage: 0x0001, OutLen: 65)";
        LOG_WARN(last_error_);
        return false;
    }

    hDevice_ = CreateFileW(
        matchedPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED | FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hDevice_ == INVALID_HANDLE_VALUE) {
        DWORD err = ::GetLastError();
        last_error_ = "打开 HID 设备句柄失败，Win32 错误码: " + std::to_string(err);
        LOG_ERROR(last_error_);
        return false;
    }

    hEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!hEvent_) {
        DWORD err = ::GetLastError();
        CloseHandle(hDevice_);
        hDevice_ = INVALID_HANDLE_VALUE;
        last_error_ = "创建重叠 I/O 事件句柄失败，错误码: " + std::to_string(err);
        LOG_ERROR(last_error_);
        return false;
    }

    device_path_ = matchedPath;
    LOG_INFO("[+] Native HID backend connected (VID=0x0B05, PID=0x1B7E, UsagePage=0xFF00, Usage=0x0001, Endpoint=MI_01, backend=native_hid)");
    return true;
}

bool NativeHidBackend::SendReport(const std::array<uint8_t, HID_REPORT_SIZE>& report) {
    if (hDevice_ == INVALID_HANDLE_VALUE || !hEvent_) {
        last_error_ = "设备未打开或事件句柄无效";
        return false;
    }

    ResetEvent(hEvent_);
    OVERLAPPED ov{};
    ov.hEvent = hEvent_;

    DWORD bytesWritten = 0;
    BOOL res = WriteFile(
        hDevice_,
        report.data(),
        static_cast<DWORD>(report.size()),
        &bytesWritten,
        &ov
    );

    if (res) {
        if (bytesWritten == HID_REPORT_SIZE) {
            return true;
        }
        last_error_ = "WriteFile 同步短写: 期望 " + std::to_string(HID_REPORT_SIZE) + " 字节，实际写入 " + std::to_string(bytesWritten) + " 字节";
        return false;
    }

    DWORD err = ::GetLastError();
    if (err == ERROR_IO_PENDING) {
        DWORD waitRes = WaitForSingleObject(hEvent_, 100);
        if (waitRes == WAIT_OBJECT_0) {
            if (GetOverlappedResult(hDevice_, &ov, &bytesWritten, FALSE)) {
                if (bytesWritten == HID_REPORT_SIZE) {
                    return true;
                }
                last_error_ = "WriteFile 异步短写: 期望 " + std::to_string(HID_REPORT_SIZE) + " 字节，实际写入 " + std::to_string(bytesWritten) + " 字节";
                return false;
            }
            err = ::GetLastError();
        } else {
            DWORD waitErr = (waitRes == WAIT_FAILED) ? ::GetLastError() : 0;

            // 超时或等待失败：精准取消本次 I/O 请求
            CancelIoEx(hDevice_, &ov);

            // 无论 CancelIoEx 返回何值（含 ERROR_NOT_FOUND 即取消前已完成的情况），
            // 在栈上 ov 离开作用域前必须调用 GetOverlappedResult(..., TRUE) 彻底 drain 完成状态，
            // 确保驱动/内核不再持有对 ov 的任何引用。
            DWORD drainBytes = 0;
            BOOL drainOk = GetOverlappedResult(hDevice_, &ov, &drainBytes, TRUE);
            if (drainOk) {
                // 请求在超时边界或取消发起前已实际完成写入 (Completion Race)
                if (drainBytes == HID_REPORT_SIZE) {
                    return true;
                }
                last_error_ = "WriteFile 异步完成但短写: 期望 " + std::to_string(HID_REPORT_SIZE) + " 字节，实际写入 " + std::to_string(drainBytes) + " 字节";
                return false;
            }

            DWORD drainErr = ::GetLastError();
            if (waitRes == WAIT_TIMEOUT) {
                if (drainErr == ERROR_OPERATION_ABORTED) {
                    // 预期取消：内核已中止该请求，不属于未知硬件故障
                    last_error_ = "WriteFile 异步等待超时 (100ms)，已成功取消并回收 I/O 请求";
                } else {
                    last_error_ = "WriteFile 异步等待超时 (100ms) 且取消后状态异常，Win32 错误码: " + std::to_string(drainErr);
                }
            } else if (waitRes == WAIT_FAILED) {
                if (drainErr == ERROR_OPERATION_ABORTED) {
                    last_error_ = "WaitForSingleObject 失败，Win32 错误码: " + std::to_string(waitErr) + "，已成功取消并回收 I/O 请求";
                } else {
                    last_error_ = "WaitForSingleObject 失败，错误码: " + std::to_string(waitErr) + "，取消后状态码: " + std::to_string(drainErr);
                }
            } else {
                last_error_ = "WaitForSingleObject 异常状态码: " + std::to_string(waitRes) + "，取消后状态码: " + std::to_string(drainErr);
            }
            return false;
        }
    }

    last_error_ = "WriteFile 失败，Win32 错误码: " + std::to_string(err);
    return false;
}

bool NativeHidBackend::PushFrame(const FrameBuffer& frame, const std::vector<uint8_t>& padded_table) {
    if (!IsConnected()) {
        return false;
    }

    if (padded_table.empty()) {
        last_error_ = "硬件寻址表为空";
        return false;
    }

    for (size_t chunkStart = 0; chunkStart < padded_table.size(); chunkStart += MAX_LEDS_PER_HID_PACKET) {
        size_t chunkEnd = std::min(chunkStart + MAX_LEDS_PER_HID_PACKET, padded_table.size());
        size_t count = chunkEnd - chunkStart;

        uint8_t indices[MAX_LEDS_PER_HID_PACKET]{0};
        uint8_t colors[MAX_LEDS_PER_HID_PACKET * RGB_CHANNELS]{0};

        for (size_t i = 0; i < count; ++i) {
            uint8_t lid = padded_table[chunkStart + i];
            indices[i] = lid;
            if (lid != DUMMY_PADDING_LED_ID && lid < TOTAL_LEDS) {
                colors[i * 3 + 0] = frame.buffer[lid * 3 + 0];
                colors[i * 3 + 1] = frame.buffer[lid * 3 + 1];
                colors[i * 3 + 2] = frame.buffer[lid * 3 + 2];
            } else {
                colors[i * 3 + 0] = 0;
                colors[i * 3 + 1] = 0;
                colors[i * 3 + 2] = 0;
            }
        }

        auto report = BuildReport(indices, colors, count);
        if (!SendReport(report)) {
            return false;
        }
    }

    return true;
}

std::string NativeHidBackend::GetDevicePath() const {
    return WideToUtf8(device_path_);
}

} // namespace aura
