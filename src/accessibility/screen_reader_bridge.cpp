#include "screen_reader_bridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QStringList>

#include <algorithm>
#include <array>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace fpdz {
namespace {

enum class Backend {
    None,
    Nvda,
    Baoyi,
    Zhengdu,
    Narrator,
};

std::wstring backendDisplayName(Backend backend) {
    switch (backend) {
    case Backend::Nvda:
        return L"NVDA";
    case Backend::Baoyi:
        return L"保益悦听";
    case Backend::Zhengdu:
        return L"争渡读屏";
    case Backend::Narrator:
        return L"Windows 讲述人（UIA 公告）";
    case Backend::None:
        return L"未检测到正在运行的官方读屏接口";
    }
    return L"未检测到正在运行的官方读屏接口";
}

QString firstExistingPath(const QStringList& candidates) {
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::toNativeSeparators(candidate);
        }
    }
    return {};
}

#ifdef _WIN32
struct StartedBackend {
    Backend backend = Backend::None;
    unsigned long long startedAt = 0;
};

Backend backendForProcess(const wchar_t* executableName) {
    if (_wcsicmp(executableName, L"nvda.exe") == 0) {
        return Backend::Nvda;
    }
    if (_wcsicmp(executableName, L"BoyPcReader.exe") == 0) {
        return Backend::Baoyi;
    }
    if (_wcsicmp(executableName, L"ZDSRMain.exe") == 0 ||
        _wcsicmp(executableName, L"ZDSRMain_x64.exe") == 0) {
        return Backend::Zhengdu;
    }
    if (_wcsicmp(executableName, L"Narrator.exe") == 0) {
        return Backend::Narrator;
    }
    return Backend::None;
}

std::vector<Backend> backendsByMostRecentStart() {
    std::vector<StartedBackend> started;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return {};
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const Backend backend = backendForProcess(entry.szExeFile);
            if (backend == Backend::None) {
                continue;
            }
            const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                               entry.th32ProcessID);
            if (!process) {
                continue;
            }
            FILETIME creation{}, exit{}, kernel{}, user{};
            if (GetProcessTimes(process, &creation, &exit, &kernel, &user)) {
                ULARGE_INTEGER time{};
                time.LowPart = creation.dwLowDateTime;
                time.HighPart = creation.dwHighDateTime;
                started.push_back({backend, time.QuadPart});
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    std::sort(started.begin(), started.end(), [](const StartedBackend& left,
                                                 const StartedBackend& right) {
        return left.startedAt > right.startedAt;
    });
    std::vector<Backend> ordered;
    for (const StartedBackend& item : started) {
        if (std::find(ordered.begin(), ordered.end(), item.backend) == ordered.end()) {
            ordered.push_back(item.backend);
        }
    }
    return ordered;
}

unsigned long long currentReaderSessionSignature() {
    constexpr unsigned long long offsetBasis = 1469598103934665603ULL;
    constexpr unsigned long long prime = 1099511628211ULL;
    unsigned long long signature = offsetBasis;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    std::vector<StartedBackend> started;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const Backend backend = backendForProcess(entry.szExeFile);
            if (backend == Backend::None) {
                continue;
            }
            const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                               entry.th32ProcessID);
            if (!process) {
                continue;
            }
            FILETIME creation{}, exit{}, kernel{}, user{};
            if (GetProcessTimes(process, &creation, &exit, &kernel, &user)) {
                ULARGE_INTEGER time{};
                time.LowPart = creation.dwLowDateTime;
                time.HighPart = creation.dwHighDateTime;
                started.push_back({backend, time.QuadPart});
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    std::sort(started.begin(), started.end(), [](const StartedBackend& left,
                                                 const StartedBackend& right) {
        if (left.startedAt != right.startedAt) {
            return left.startedAt < right.startedAt;
        }
        return static_cast<int>(left.backend) < static_cast<int>(right.backend);
    });
    for (const StartedBackend& item : started) {
        signature ^= static_cast<unsigned long long>(item.backend);
        signature *= prime;
        signature ^= item.startedAt;
        signature *= prime;
    }
    return started.empty() ? 0 : signature;
}
#else
std::vector<Backend> backendsByMostRecentStart() { return {}; }
unsigned long long currentReaderSessionSignature() { return 0; }
#endif

} // namespace

class ScreenReaderBridge::Impl {
public:
#ifdef _WIN32
    using NvdaTestIfRunning = unsigned long(WINAPI*)();
    using NvdaSpeakText = unsigned long(WINAPI*)(const wchar_t*);
    using NvdaCancelSpeech = unsigned long(WINAPI*)();

    using BoyCtrlInitialize = int(WINAPI*)(const wchar_t*);
    using BoyCtrlUninitialize = void(WINAPI*)();
    using BoyCtrlIsReaderRunning = bool(WINAPI*)();
    using BoyCtrlSpeak2 = int(WINAPI*)(const wchar_t*, bool, const wchar_t*, bool, bool,
                                       void*);
    using BoyCtrlStopSpeaking2 = int(WINAPI*)(bool, const wchar_t*);

    using ZdsrInitTts = int(WINAPI*)(int, const wchar_t*, BOOL);
    using ZdsrSpeak = int(WINAPI*)(const wchar_t*, BOOL);
    using ZdsrGetSpeakState = int(WINAPI*)();
    using ZdsrStopSpeak = void(WINAPI*)();
#endif

    Impl() {
        m_disabled = qEnvironmentVariableIntValue("FPDZ_DISABLE_SCREEN_READER_APIS") != 0;
        // This test-only override is honored only while private reader APIs are disabled,
        // so production routing can never be forced through an environment variable.
        m_forceNarratorForTests =
            m_disabled && qEnvironmentVariableIntValue("FPDZ_TEST_NARRATOR_ACTIVE") != 0;
#ifdef _WIN32
        if (!m_disabled) {
            loadNvda();
            loadBaoyi();
            loadZhengdu();
            m_readerSessionSignature = currentReaderSessionSignature();
        }
#endif
    }

    ~Impl() {
#ifdef _WIN32
        if (m_baoyiInitialized && m_boyUninitialize) {
            m_boyUninitialize();
        }
#endif
    }

    ScreenReaderDelivery speak(const std::wstring& text, bool interrupt) {
        if ((m_disabled && !m_forceNarratorForTests) || text.empty()) {
            return ScreenReaderDelivery::Unavailable;
        }
        const unsigned long long signature = currentReaderSessionSignature();
        m_readerSessionChanged = signature != m_readerSessionSignature;
        m_readerSessionSignature = signature;
        bool narratorAvailable = false;
        for (Backend backend : routingOrder()) {
            if (backend == Backend::Narrator) {
                narratorAvailable = true;
                continue;
            }
            if (!isActive(backend)) {
                continue;
            }
            if (speakThrough(backend, text, interrupt)) {
                m_lastBackend = backend;
                return ScreenReaderDelivery::PrivateApi;
            }
        }
        // UI Automation notifications are broadcast to accessibility clients. Prefer
        // a usable private reader API when another supported reader is also active,
        // otherwise Narrator and that reader could both announce the same message.
        if (narratorAvailable) {
            m_lastBackend = Backend::Narrator;
            return ScreenReaderDelivery::StandardAnnouncement;
        }
        m_lastBackend = Backend::None;
        return ScreenReaderDelivery::Unavailable;
    }

    void stop() {
        switch (m_lastBackend) {
        case Backend::Nvda:
#ifdef _WIN32
            if (m_nvdaCancel) {
                m_nvdaCancel();
            }
#endif
            break;
        case Backend::Baoyi:
#ifdef _WIN32
            if (m_boyStop) {
                m_boyStop(true, kChannelName);
            }
#endif
            break;
        case Backend::Zhengdu:
#ifdef _WIN32
            if (m_zdsrStop) {
                m_zdsrStop();
            }
#endif
            break;
        case Backend::Narrator:
            // Narrator owns its UIA speech queue; there is no app-side stop API.
            break;
        case Backend::None:
            break;
        }
    }

    std::wstring backendName() const {
        if (m_lastBackend == Backend::Narrator) {
            return backendDisplayName(m_lastBackend);
        }
        if (m_disabled) {
            return L"官方读屏接口已由测试环境禁用";
        }
        return backendDisplayName(m_lastBackend);
    }

    bool readerSessionChanged() const { return m_readerSessionChanged; }

private:
    static constexpr const wchar_t* kChannelName = L"飞船单机斗地主";

    std::vector<Backend> routingOrder() const {
        std::vector<Backend> order = backendsByMostRecentStart();
        if (m_forceNarratorForTests &&
            std::find(order.begin(), order.end(), Backend::Narrator) == order.end()) {
            order.insert(order.begin(), Backend::Narrator);
        }
        constexpr std::array fallbackOrder{
            Backend::Nvda,
            Backend::Baoyi,
            Backend::Zhengdu,
        };
        for (Backend backend : fallbackOrder) {
            if (std::find(order.begin(), order.end(), backend) == order.end()) {
                order.push_back(backend);
            }
        }
        return order;
    }

    bool isActive(Backend backend) {
        switch (backend) {
        case Backend::Nvda:
#ifdef _WIN32
            return m_nvdaTest && m_nvdaTest() == ERROR_SUCCESS;
#else
            return false;
#endif
        case Backend::Baoyi:
#ifdef _WIN32
            return m_baoyiInitialized && m_boyIsRunning && m_boyIsRunning();
#else
            return false;
#endif
        case Backend::Zhengdu: {
#ifdef _WIN32
            if (!m_zdsrInit || !m_zdsrState || m_zdsrInit(1, kChannelName, TRUE) != 0) {
                return false;
            }
            const int state = m_zdsrState();
            return state == 3 || state == 4;
#else
            return false;
#endif
        }
        case Backend::Narrator:
            // Narrator is only added to the route when its process is present (or
            // through the disabled-APIs test override above).
            return true;
        case Backend::None:
            return false;
        }
        return false;
    }

    bool speakThrough(Backend backend, const std::wstring& text, bool interrupt) {
        switch (backend) {
        case Backend::Nvda:
#ifdef _WIN32
            if (!m_nvdaSpeak) {
                return false;
            }
            if (interrupt && m_nvdaCancel) {
                m_nvdaCancel();
            }
            return m_nvdaSpeak(text.c_str()) == ERROR_SUCCESS;
#else
            return false;
#endif
        case Backend::Baoyi:
#ifdef _WIN32
            return m_boySpeak &&
                   m_boySpeak(text.c_str(), true, kChannelName, !interrupt, true, nullptr) == 0;
#else
            return false;
#endif
        case Backend::Zhengdu:
#ifdef _WIN32
            return m_zdsrSpeak && m_zdsrSpeak(text.c_str(), interrupt ? TRUE : FALSE) == 0;
#else
            return false;
#endif
        case Backend::Narrator:
            return false;
        case Backend::None:
            return false;
        }
        return false;
    }

#ifdef _WIN32
    void loadNvda() {
        const QString path = QDir(QCoreApplication::applicationDirPath())
                                 .filePath(QStringLiteral("nvdaControllerClient.dll"));
        if (!QFileInfo::exists(path)) {
            return;
        }
        m_nvda.setFileName(QDir::toNativeSeparators(path));
        if (!m_nvda.load()) {
            return;
        }
        m_nvdaTest =
            reinterpret_cast<NvdaTestIfRunning>(m_nvda.resolve("nvdaController_testIfRunning"));
        m_nvdaSpeak =
            reinterpret_cast<NvdaSpeakText>(m_nvda.resolve("nvdaController_speakText"));
        m_nvdaCancel =
            reinterpret_cast<NvdaCancelSpeech>(m_nvda.resolve("nvdaController_cancelSpeech"));
        if (!m_nvdaTest || !m_nvdaSpeak || !m_nvdaCancel) {
            m_nvda.unload();
            m_nvdaTest = nullptr;
            m_nvdaSpeak = nullptr;
            m_nvdaCancel = nullptr;
        }
    }

    void loadBaoyi() {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");
        const QString path = firstExistingPath({
            QDir(appDir).filePath(QStringLiteral("byctrl-x64.dll")),
            QDir(programFilesX86).filePath(QStringLiteral("BoyPcReader/byctrl-x64.dll")),
        });
        if (path.isEmpty()) {
            return;
        }
        m_baoyi.setFileName(path);
        if (!m_baoyi.load()) {
            return;
        }
        m_boyInitialize = reinterpret_cast<BoyCtrlInitialize>(m_baoyi.resolve("BoyCtrlInitialize"));
        m_boyUninitialize =
            reinterpret_cast<BoyCtrlUninitialize>(m_baoyi.resolve("BoyCtrlUninitialize"));
        m_boyIsRunning =
            reinterpret_cast<BoyCtrlIsReaderRunning>(m_baoyi.resolve("BoyCtrlIsReaderRunning"));
        m_boySpeak = reinterpret_cast<BoyCtrlSpeak2>(m_baoyi.resolve("BoyCtrlSpeak2"));
        m_boyStop =
            reinterpret_cast<BoyCtrlStopSpeaking2>(m_baoyi.resolve("BoyCtrlStopSpeaking2"));
        if (!m_boyInitialize || !m_boyUninitialize || !m_boyIsRunning || !m_boySpeak ||
            !m_boyStop) {
            m_baoyi.unload();
            return;
        }
        m_baoyiInitialized = m_boyInitialize(nullptr) == 0;
    }

    void loadZhengdu() {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");
        const QString path = firstExistingPath({
            QDir(appDir).filePath(QStringLiteral("ZDSRAPI_x64.dll")),
            QDir(programFilesX86).filePath(QStringLiteral("zdsr/zdsr/ZDSRAPI_x64.dll")),
        });
        if (path.isEmpty()) {
            return;
        }
        m_zhengdu.setFileName(path);
        if (!m_zhengdu.load()) {
            return;
        }
        m_zdsrInit = reinterpret_cast<ZdsrInitTts>(m_zhengdu.resolve("InitTTS"));
        m_zdsrSpeak = reinterpret_cast<ZdsrSpeak>(m_zhengdu.resolve("Speak"));
        m_zdsrState = reinterpret_cast<ZdsrGetSpeakState>(m_zhengdu.resolve("GetSpeakState"));
        m_zdsrStop = reinterpret_cast<ZdsrStopSpeak>(m_zhengdu.resolve("StopSpeak"));
        if (!m_zdsrInit || !m_zdsrSpeak || !m_zdsrState || !m_zdsrStop) {
            m_zhengdu.unload();
            m_zdsrInit = nullptr;
            m_zdsrSpeak = nullptr;
            m_zdsrState = nullptr;
            m_zdsrStop = nullptr;
        }
    }
#endif

    bool m_disabled = false;
    bool m_forceNarratorForTests = false;
    bool m_readerSessionChanged = false;
    unsigned long long m_readerSessionSignature = 0;
    Backend m_lastBackend = Backend::None;
#ifdef _WIN32
    QLibrary m_nvda;
    QLibrary m_baoyi;
    QLibrary m_zhengdu;
    NvdaTestIfRunning m_nvdaTest = nullptr;
    NvdaSpeakText m_nvdaSpeak = nullptr;
    NvdaCancelSpeech m_nvdaCancel = nullptr;
    BoyCtrlInitialize m_boyInitialize = nullptr;
    BoyCtrlUninitialize m_boyUninitialize = nullptr;
    BoyCtrlIsReaderRunning m_boyIsRunning = nullptr;
    BoyCtrlSpeak2 m_boySpeak = nullptr;
    BoyCtrlStopSpeaking2 m_boyStop = nullptr;
    bool m_baoyiInitialized = false;
    ZdsrInitTts m_zdsrInit = nullptr;
    ZdsrSpeak m_zdsrSpeak = nullptr;
    ZdsrGetSpeakState m_zdsrState = nullptr;
    ZdsrStopSpeak m_zdsrStop = nullptr;
#endif
};

ScreenReaderBridge::ScreenReaderBridge() : m_impl(std::make_unique<Impl>()) {}
ScreenReaderBridge::~ScreenReaderBridge() = default;

ScreenReaderDelivery ScreenReaderBridge::speak(const std::wstring& text, bool interrupt) {
    return m_impl->speak(text, interrupt);
}

void ScreenReaderBridge::stop() { m_impl->stop(); }

bool ScreenReaderBridge::readerSessionChanged() const {
    return m_impl->readerSessionChanged();
}

std::wstring ScreenReaderBridge::routeName() const {
    return L"当前读屏官方接口自动接管，Windows讲述人使用UIA公告，Qt/UIA焦点事件兜底";
}

std::wstring ScreenReaderBridge::backendName() const { return m_impl->backendName(); }

} // namespace fpdz
