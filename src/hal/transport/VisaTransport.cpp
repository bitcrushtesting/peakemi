#include <peakemi/core/Logging.h>
#include <peakemi/hal/Scpi.h>
#include <peakemi/hal/VisaTransport.h>

#include <QLibrary>
#include <QStringList>

#include <array>
#include <cstring>
#include <utility>

namespace peakemi::hal {
namespace {

#ifdef PEAKEMI_WITH_VISA

// The handful of VISA types this transport needs. Declaring them here keeps the
// build independent of whether a visa.h exists on the machine.
using ViStatus = long;
using ViSession = unsigned int;
using ViUInt32 = unsigned int;
using ViAttr = unsigned int;
using ViAttrState = unsigned long;

constexpr ViStatus VisaSuccess = 0;
constexpr ViAttr AttributeTimeout = 0x3FFF001AU;
constexpr ViUInt32 NoLock = 0;

/// Function pointers resolved from whichever VISA runtime is installed.
struct VisaRuntime
{
    QLibrary library;
    bool ready{false};

    ViStatus (*openDefaultRM)(ViSession*){nullptr};
    ViStatus (*open)(ViSession, const char*, ViUInt32, ViUInt32, ViSession*){nullptr};
    ViStatus (*close)(ViSession){nullptr};
    ViStatus (*write)(ViSession, const unsigned char*, ViUInt32, ViUInt32*){nullptr};
    ViStatus (*read)(ViSession, unsigned char*, ViUInt32, ViUInt32*){nullptr};
    ViStatus (*setAttribute)(ViSession, ViAttr, ViAttrState){nullptr};
    ViStatus (*clear)(ViSession){nullptr};
    ViStatus (*findRsrc)(ViSession, const char*, ViSession*, ViUInt32*, char*){nullptr};
    ViStatus (*findNext)(ViSession, char*){nullptr};
    ViStatus (*statusDesc)(ViSession, ViStatus, char*){nullptr};

    template<class Signature>
    void resolve(Signature& target, const char* name)
    {
        target = reinterpret_cast<Signature>(library.resolve(name));
        if (target == nullptr) {
            ready = false;
        }
    }
};

/// Candidate library names, most specific first, per platform.
[[nodiscard]] QStringList candidateLibraries()
{
#    if defined(Q_OS_WIN)
    return {QStringLiteral("visa64"), QStringLiteral("visa32")};
#    elif defined(Q_OS_MACOS)
    return {QStringLiteral("/Library/Frameworks/VISA.framework/VISA"),
            QStringLiteral("/Library/Frameworks/RsVisa.framework/RsVisa"),
            QStringLiteral("visa")};
#    else
    return {QStringLiteral("visa"), QStringLiteral("rsvisa"), QStringLiteral("iovisa")};
#    endif
}

void loadRuntime(VisaRuntime& loaded)
{
    for (const auto& name : candidateLibraries()) {
        loaded.library.setFileName(name);
        if (!loaded.library.load()) {
            continue;
        }
        loaded.ready = true;
        loaded.resolve(loaded.openDefaultRM, "viOpenDefaultRM");
        loaded.resolve(loaded.open, "viOpen");
        loaded.resolve(loaded.close, "viClose");
        loaded.resolve(loaded.write, "viWrite");
        loaded.resolve(loaded.read, "viRead");
        loaded.resolve(loaded.setAttribute, "viSetAttribute");
        loaded.resolve(loaded.clear, "viClear");
        loaded.resolve(loaded.findRsrc, "viFindRsrc");
        loaded.resolve(loaded.findNext, "viFindNext");
        loaded.resolve(loaded.statusDesc, "viStatusDesc");
        if (loaded.ready) {
            qCInfo(lcTransport) << "VISA runtime loaded:" << loaded.library.fileName();
            return;
        }
        loaded.library.unload();
    }
}

/// QLibrary is not copyable, so the runtime is configured where it lives.
[[nodiscard]] VisaRuntime& runtime()
{
    static VisaRuntime instance;
    static const bool once = [] {
        loadRuntime(instance);
        return true;
    }();
    (void)once;
    return instance;
}

[[nodiscard]] Error toError(ViStatus status, std::string_view context)
{
    std::array<char, 256> description{};
    std::string text;
    if (runtime().statusDesc != nullptr &&
        runtime().statusDesc(0, status, description.data()) == VisaSuccess)
    {
        text = description.data();
    } else {
        text = "VISA status " + std::to_string(status);
    }
    // VISA reports a timeout as VI_ERROR_TMO (0xBFFF0015).
    const auto code = static_cast<unsigned long>(status) == 0xBFFF0015UL
                          ? ErrorCode::Timeout
                          : ErrorCode::TransportFailure;
    return Error{code, std::string{context} + ": " + text};
}

#endif // PEAKEMI_WITH_VISA

} // namespace

#ifdef PEAKEMI_WITH_VISA

struct VisaTransport::Impl
{
    ViSession resourceManager{0};
    ViSession session{0};
    bool open{false};
};

bool VisaTransport::isAvailable()
{
    return runtime().ready;
}

QString VisaTransport::runtimeName()
{
    return runtime().ready ? runtime().library.fileName() : QString{};
}

Result<std::vector<std::string>> VisaTransport::findResources(std::string_view filter)
{
    if (!isAvailable()) {
        return fail(ErrorCode::NotImplemented, "no VISA runtime is installed");
    }

    ViSession manager = 0;
    if (const auto status = runtime().openDefaultRM(&manager); status != VisaSuccess) {
        return std::unexpected(toError(status, "viOpenDefaultRM"));
    }

    std::vector<std::string> resources;
    ViSession search = 0;
    ViUInt32 count = 0;
    std::array<char, 256> buffer{};
    const std::string pattern{filter};

    auto status = runtime().findRsrc(manager, pattern.c_str(), &search, &count, buffer.data());
    if (status == VisaSuccess) {
        resources.emplace_back(buffer.data());
        for (ViUInt32 i = 1; i < count; ++i) {
            if (runtime().findNext(search, buffer.data()) != VisaSuccess) {
                break;
            }
            resources.emplace_back(buffer.data());
        }
        runtime().close(search);
    }
    runtime().close(manager);
    return resources;
}

VisaTransport::VisaTransport(TransportDescriptor descriptor)
    : m_descriptor{std::move(descriptor)}
    , m_impl{std::make_unique<Impl>()}
{}

VisaTransport::~VisaTransport()
{
    VisaTransport::close();
}

Status VisaTransport::open()
{
    if (isOpen()) {
        return {};
    }
    if (!isAvailable()) {
        return fail(ErrorCode::NotImplemented, "no VISA runtime is installed");
    }

    if (const auto status = runtime().openDefaultRM(&m_impl->resourceManager);
        status != VisaSuccess)
    {
        return std::unexpected(toError(status, "viOpenDefaultRM"));
    }
    const auto timeout = static_cast<ViUInt32>(m_descriptor.defaultTimeout.count());
    if (const auto status = runtime().open(m_impl->resourceManager,
                                           m_descriptor.address.c_str(),
                                           NoLock,
                                           timeout,
                                           &m_impl->session);
        status != VisaSuccess)
    {
        runtime().close(m_impl->resourceManager);
        m_impl->resourceManager = 0;
        return std::unexpected(toError(status, "viOpen " + m_descriptor.address));
    }

    (void)runtime().setAttribute(m_impl->session, AttributeTimeout, timeout);
    m_impl->open = true;
    return {};
}

bool VisaTransport::isOpen() const
{
    return m_impl && m_impl->open;
}

void VisaTransport::close()
{
    if (!m_impl || !m_impl->open) {
        return;
    }
    runtime().close(m_impl->session);
    runtime().close(m_impl->resourceManager);
    m_impl->session = 0;
    m_impl->resourceManager = 0;
    m_impl->open = false;
}

Status VisaTransport::write(std::string_view command)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_descriptor.displayName());
    }

    std::string payload{command};
    if (!payload.ends_with(m_descriptor.terminator)) {
        payload += m_descriptor.terminator;
    }
    qCDebug(lcScpi).noquote() << ">" << QString::fromStdString(payload).trimmed();

    ViUInt32 written = 0;
    const auto status = runtime().write(m_impl->session,
                                        reinterpret_cast<const unsigned char*>(payload.data()),
                                        static_cast<ViUInt32>(payload.size()),
                                        &written);
    if (status != VisaSuccess) {
        return std::unexpected(toError(status, "viWrite"));
    }
    if (written != payload.size()) {
        return fail(ErrorCode::TransportFailure, "short VISA write");
    }
    return {};
}

Result<std::vector<std::byte>> VisaTransport::readBinaryBlock(std::chrono::milliseconds timeout,
                                                              const CancelToken& cancel)
{
    if (!isOpen()) {
        return fail(ErrorCode::NotConnected, m_descriptor.displayName());
    }
    (void)runtime().setAttribute(
        m_impl->session, AttributeTimeout, static_cast<ViAttrState>(timeout.count()));

    constexpr ViUInt32 ChunkSize = 16384;
    std::vector<std::byte> payload;
    std::vector<unsigned char> buffer(ChunkSize);
    for (;;) {
        if (cancel.isCancelled()) {
            return fail(ErrorCode::Cancelled, "read cancelled");
        }
        ViUInt32 received = 0;
        const auto status = runtime().read(m_impl->session, buffer.data(), ChunkSize, &received);
        payload.insert(payload.end(),
                       reinterpret_cast<const std::byte*>(buffer.data()),
                       reinterpret_cast<const std::byte*>(buffer.data() + received));
        // VI_SUCCESS_MAX_CNT (0x3FFF0006) means there is more to come.
        if (static_cast<unsigned long>(status) == 0x3FFF0006UL) {
            continue;
        }
        if (status != VisaSuccess) {
            return std::unexpected(toError(status, "viRead"));
        }
        break;
    }
    return payload;
}

Result<std::string> VisaTransport::read(std::chrono::milliseconds timeout,
                                        const CancelToken& cancel)
{
    auto payload = readBinaryBlock(timeout, cancel);
    if (!payload) {
        return std::unexpected(payload.error());
    }
    std::string text;
    text.reserve(payload->size());
    for (const auto byte : *payload) {
        text.push_back(static_cast<char>(byte));
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    qCDebug(lcScpi).noquote() << "<" << QString::fromStdString(text);
    return text;
}

void VisaTransport::clear()
{
    if (isOpen()) {
        (void)runtime().clear(m_impl->session);
    }
}

#else // PEAKEMI_WITH_VISA

struct VisaTransport::Impl
{};

bool VisaTransport::isAvailable()
{
    return false;
}

QString VisaTransport::runtimeName()
{
    return {};
}

Result<std::vector<std::string>> VisaTransport::findResources(std::string_view /*filter*/)
{
    return fail(ErrorCode::NotImplemented,
                "this build has no VISA support; configure with -DPEAKEMI_WITH_VISA=ON");
}

VisaTransport::VisaTransport(TransportDescriptor descriptor) : m_descriptor{std::move(descriptor)}
{}

VisaTransport::~VisaTransport() = default;

Status VisaTransport::open()
{
    return fail(ErrorCode::NotImplemented, "this build has no VISA support");
}

bool VisaTransport::isOpen() const
{
    return false;
}

void VisaTransport::close() {}

Status VisaTransport::write(std::string_view /*command*/)
{
    return fail(ErrorCode::NotImplemented, "this build has no VISA support");
}

Result<std::string> VisaTransport::read(std::chrono::milliseconds /*timeout*/,
                                        const CancelToken& /*cancel*/)
{
    return fail(ErrorCode::NotImplemented, "this build has no VISA support");
}

Result<std::vector<std::byte>> VisaTransport::readBinaryBlock(std::chrono::milliseconds /*timeout*/,
                                                              const CancelToken& /*cancel*/)
{
    return fail(ErrorCode::NotImplemented, "this build has no VISA support");
}

void VisaTransport::clear() {}

#endif // PEAKEMI_WITH_VISA

} // namespace peakemi::hal
