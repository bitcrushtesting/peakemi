#include <peakemi/core/Logging.h>
#include <peakemi/ui/RunController.h>

#include <QMetaObject>
#include <QThread>

#include <utility>

namespace peakemi::ui {

RunController::RunController(QObject* parent)
    : QObject{parent}
    , m_thread{new QThread{this}}
    , m_engine{new MeasurementEngine}
{
    m_thread->setObjectName(QStringLiteral("peakemi.acquisition"));
    m_engine->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_engine, &QObject::deleteLater);

    // The engine decides when the operator's commands are sent; reaching the
    // equipment is the transport's business, which lives here.
    m_engine->setCommandSender([this](const std::string& command) -> Status {
        if (!m_transport) {
            return fail(ErrorCode::NotConnected, "no instrument is connected");
        }
        if (auto status = m_transport->write(command); !status) {
            return status;
        }
        if (command.find('?') == std::string::npos) {
            return {};
        }
        // A query is worth waiting for: it is how an operator checks that the
        // equipment did what the command asked.
        const CancelToken cancel;
        auto response = m_transport->read(m_config.commandTimeout, cancel);
        if (!response) {
            return std::unexpected(response.error());
        }
        emit rawResponse(QStringLiteral("< %1").arg(QString::fromStdString(*response)));
        return {};
    });

    connect(m_engine, &MeasurementEngine::phaseChanged, this, &RunController::phaseChanged);
    connect(m_engine, &MeasurementEngine::traceAcquired, this, &RunController::traceAcquired);
    connect(m_engine, &MeasurementEngine::peaksFlagged, this, &RunController::peaksFlagged);
    connect(m_engine, &MeasurementEngine::pointMeasured, this, &RunController::pointMeasured);
    connect(m_engine, &MeasurementEngine::progress, this, &RunController::progress);
    connect(m_engine, &MeasurementEngine::runFailed, this, &RunController::runFailed);
    connect(m_engine, &MeasurementEngine::runFinished, this, &RunController::runFinished);
    connect(m_engine, &MeasurementEngine::logMessage, this, &RunController::logMessage);

    // Emitted from the worker thread, so this queued self-connection is what
    // keeps the cached identity readable from the GUI thread.
    connect(this, &RunController::instrumentIdentified, this, [this](const InstrumentId& identity) {
        m_instrument = identity;
    });

    m_thread->start();
}

RunController::~RunController()
{
    abort();
    disconnectInstrument();
    m_thread->quit();
    m_thread->wait();
}

void RunController::connectInstrument(DriverPtr driver, TransportPtr transport)
{
    disconnectInstrument();
    m_driver = std::move(driver);
    m_transport = std::move(transport);
    if (!m_driver) {
        emit connectionChanged(false);
        return;
    }

    // Open and identify on the worker thread: opening a socket blocks. The
    // driver and transport are captured by value, so a disconnect racing with
    // this call cannot pull them out from under the worker.
    QMetaObject::invokeMethod(
        m_engine,
        [this, driver = m_driver, transport = m_transport] {
            if (auto status = driver->open(transport); !status) {
                emit runFailed(status.error());
                emit connectionChanged(false);
                return;
            }
            if (auto identity = driver->identify()) {
                emit instrumentIdentified(*identity);
            }
            m_engine->setDriver(driver);
            emit connectionChanged(true);
        },
        Qt::QueuedConnection);
}

void RunController::disconnectInstrument()
{
    if (!m_driver) {
        return;
    }
    m_driver->abort();

    auto driver = m_driver;
    auto transport = m_transport;
    m_driver.reset();
    m_transport.reset();
    m_instrument = {};

    QMetaObject::invokeMethod(
        m_engine,
        [this, driver, transport] {
            driver->close();
            if (transport) {
                transport->close();
            }
            m_engine->setDriver(nullptr);
        },
        Qt::QueuedConnection);
    emit connectionChanged(false);
}

bool RunController::isConnected() const
{
    return static_cast<bool>(m_driver);
}

bool RunController::isRunning() const
{
    return m_engine->isRunning();
}

Capabilities RunController::capabilities() const
{
    return m_driver ? m_driver->capabilities() : Capabilities{};
}

void RunController::setConfiguration(RunConfiguration config)
{
    m_config = std::move(config);
    auto copy = m_config;
    QMetaObject::invokeMethod(
        m_engine,
        [this, copy = std::move(copy)]() mutable { m_engine->setConfiguration(std::move(copy)); },
        Qt::QueuedConnection);
}

void RunController::setSessionMeta(SessionMeta meta)
{
    QMetaObject::invokeMethod(
        m_engine,
        [this, meta = std::move(meta)]() mutable { m_engine->setSessionMeta(std::move(meta)); },
        Qt::QueuedConnection);
}

void RunController::start()
{
    if (!m_driver) {
        emit runFailed(Error{ErrorCode::NotConnected, "connect an instrument first"});
        return;
    }
    QMetaObject::invokeMethod(m_engine, &MeasurementEngine::start, Qt::QueuedConnection);
}

void RunController::pause()
{
    // Deliberately a direct call: the worker's event loop is busy running the
    // sweep, so a queued slot would only arrive after the run had finished.
    m_engine->requestPause();
}

void RunController::resume()
{
    m_engine->requestResume();
}

void RunController::abort()
{
    m_engine->requestAbort();
}

void RunController::sendRawCommand(const QString& command)
{
    if (!m_transport) {
        emit rawResponse(tr("< not connected"));
        return;
    }
    if (isRunning()) {
        emit rawResponse(tr("< refused: a measurement run is in progress"));
        return;
    }

    QMetaObject::invokeMethod(
        m_engine,
        [this, command, transport = m_transport, timeout = m_config.operationTimeout] {
            const auto text = command.toStdString();
            qCInfo(lcScpi).noquote() << "console >" << command;
            if (auto status = transport->write(text); !status) {
                emit rawResponse(
                    QStringLiteral("< %1").arg(QString::fromStdString(status.error().message())));
                return;
            }
            if (!command.contains(QLatin1Char('?'))) {
                emit rawResponse(QStringLiteral("< ok"));
                return;
            }
            const CancelToken cancel;
            auto response = transport->read(timeout, cancel);
            if (!response) {
                emit rawResponse(
                    QStringLiteral("< %1").arg(QString::fromStdString(response.error().message())));
                return;
            }
            emit rawResponse(QStringLiteral("< %1").arg(QString::fromStdString(*response)));
        },
        Qt::QueuedConnection);
}

} // namespace peakemi::ui
