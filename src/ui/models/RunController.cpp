#include <peakemi/core/Logging.hpp>
#include <peakemi/ui/RunController.hpp>

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

    connect(m_engine, &MeasurementEngine::phaseChanged, this, &RunController::phaseChanged);
    connect(m_engine, &MeasurementEngine::traceAcquired, this, &RunController::traceAcquired);
    connect(m_engine, &MeasurementEngine::peaksFlagged, this, &RunController::peaksFlagged);
    connect(m_engine, &MeasurementEngine::pointMeasured, this, &RunController::pointMeasured);
    connect(m_engine, &MeasurementEngine::progress, this, &RunController::progress);
    connect(m_engine, &MeasurementEngine::runFailed, this, &RunController::runFailed);
    connect(m_engine, &MeasurementEngine::runFinished, this, &RunController::runFinished);
    connect(m_engine, &MeasurementEngine::logMessage, this, &RunController::logMessage);

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

    // Open and identify on the worker thread: opening a socket blocks.
    QMetaObject::invokeMethod(
        m_engine,
        [this] {
            if (auto status = m_driver->open(m_transport); !status) {
                emit runFailed(status.error());
                m_driver.reset();
                m_transport.reset();
                emit connectionChanged(false);
                return;
            }
            if (auto identity = m_driver->identify()) {
                m_instrument = *identity;
                emit instrumentIdentified(m_instrument);
            }
            m_engine->setDriver(m_driver);
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
        [this, command] {
            const auto text = command.toStdString();
            qCInfo(lcScpi).noquote() << "console >" << command;
            if (auto status = m_transport->write(text); !status) {
                emit rawResponse(QStringLiteral("< %1")
                                     .arg(QString::fromStdString(status.error().message())));
                return;
            }
            if (!command.contains(QLatin1Char('?'))) {
                emit rawResponse(QStringLiteral("< ok"));
                return;
            }
            const CancelToken cancel;
            auto response = m_transport->read(m_config.operationTimeout, cancel);
            if (!response) {
                emit rawResponse(QStringLiteral("< %1")
                                     .arg(QString::fromStdString(response.error().message())));
                return;
            }
            emit rawResponse(QStringLiteral("< %1").arg(QString::fromStdString(*response)));
        },
        Qt::QueuedConnection);
}

} // namespace peakemi::ui
