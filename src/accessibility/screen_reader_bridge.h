#pragma once

#include <memory>
#include <string>

namespace fpdz {

enum class ScreenReaderDelivery {
    Unavailable,
    PrivateApi,
    StandardAnnouncement,
};

class ScreenReaderBridge {
public:
    ScreenReaderBridge();
    ~ScreenReaderBridge();

    ScreenReaderBridge(const ScreenReaderBridge&) = delete;
    ScreenReaderBridge& operator=(const ScreenReaderBridge&) = delete;

    ScreenReaderDelivery speak(const std::wstring& text, bool interrupt);
    void stop();
    bool readerSessionChanged() const;
    std::wstring routeName() const;
    std::wstring backendName() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace fpdz
