#pragma once

#include <peakemi/core/AbstractAnalyzerDriver.h>

#include <atomic>
#include <string>

namespace peakemi::drivers {

/// The vendor-specific parts of an otherwise standard SCPI spectrum analyzer.
///
/// Every SCPI string in the application lives in a structure like this or in the
/// driver that owns it — nothing above the HAL ever sees one (FR-HAL-4).
struct ScpiDialect
{
    std::string startFrequency{":SENSe:FREQuency:STARt"};
    std::string stopFrequency{":SENSe:FREQuency:STOP"};
    std::string resolutionBandwidth{":SENSe:BANDwidth:RESolution"};
    std::string videoBandwidth{":SENSe:BANDwidth:VIDeo"};
    std::string sweepPoints{":SENSe:SWEep:POINts"};
    std::string sweepTime{":SENSe:SWEep:TIME"};
    std::string detector{":SENSe:DETector:FUNCtion"};
    std::string attenuation{":SENSe:POWer:RF:ATTenuation"};
    std::string attenuationAuto{":SENSe:POWer:RF:ATTenuation:AUTO"};
    std::string preamp{":SENSe:POWer:RF:GAIN:STATe"};
    std::string referenceLevel{":DISPlay:WINDow:TRACe:Y:SCALe:RLEVel"};
    std::string amplitudeUnit{":UNIT:POWer"};
    std::string continuousSweep{":INITiate:CONTinuous"};
    std::string singleSweep{":INITiate:IMMediate"};
    std::string operationComplete{"*OPC?"};
    std::string traceFormat{":FORMat:TRACe:DATA"};
    std::string traceFormatAscii{"ASCii"};
    std::string traceQuery{":TRACe:DATA? TRACE1"};
    std::string errorQuery{":SYSTem:ERRor?"};
    std::string reset{"*RST"};
    std::string clearStatus{"*CLS"};

    std::string peakDetector{"POSitive"};
    std::string quasiPeakDetector{"QPEak"};
    std::string averageDetector{"AVERage"};
    std::string rmsDetector{"RMS"};
    std::string sampleDetector{"SAMPle"};

    /// dBuV keyword; a few instruments spell it DBUV, others DBμV.
    std::string dBuVKeyword{"DBUV"};
    std::string dBmKeyword{"DBM"};
};

/// Generic SCPI analyzer driver, parameterised by dialect and capabilities.
///
/// Covers the instruments whose remote command set follows SCPI closely
/// (Siglent SSA/SVA, Rigol DSA). A model needing more than a dialect tweak gets
/// its own subclass overriding the affected step.
class ScpiAnalyzerDriver : public AbstractAnalyzerDriver
{
public:
    ScpiAnalyzerDriver(DriverInfo info, Capabilities capabilities, ScpiDialect dialect = {});

    /// True once the instrument has named itself and a model profile was
    /// adopted, rather than the family defaults the driver started with.
    [[nodiscard]] bool hasModelProfile() const { return m_profileAdopted; }

    ~ScpiAnalyzerDriver() override;

    [[nodiscard]] DriverInfo info() const override { return m_info; }

    [[nodiscard]] Capabilities capabilities() const override { return m_capabilities; }

    [[nodiscard]] Status open(TransportPtr transport) override;
    [[nodiscard]] bool isOpen() const override;
    void close() override;

    [[nodiscard]] Result<InstrumentId> identify() override;
    [[nodiscard]] Status configureSweep(const SweepParams& params) override;
    [[nodiscard]] Status armAndTrigger(const CancelToken& cancel) override;
    [[nodiscard]] Result<Trace> fetchTrace(const CancelToken& cancel) override;
    void abort() override;
    [[nodiscard]] std::vector<InstrumentError> lastErrors() override;
    void setTimeout(std::chrono::milliseconds timeout) override;

protected:
    [[nodiscard]] Status send(const std::string& command);
    [[nodiscard]] Status sendValue(const std::string& command, const std::string& value);
    [[nodiscard]] Result<std::string> query(const std::string& command, const CancelToken& cancel);
    [[nodiscard]] std::string detectorKeyword(Detector detector) const;

    ScpiDialect m_dialect;

private:
    /// Narrow the capabilities and dialect to the model that just identified
    /// itself, if one is known.
    void adoptProfileFor(const InstrumentId& identity);

    DriverInfo m_info;
    Capabilities m_capabilities;
    bool m_profileAdopted{false};
    TransportPtr m_transport;
    SweepParams m_params;
    InstrumentId m_identity;
    std::chrono::milliseconds m_timeout{5000};
    std::atomic_bool m_abortRequested{false};
    CancelToken m_abortToken;
};

/// Instantiate the drivers of the v1 supported set (requirements 6, Q1).
[[nodiscard]] DriverPtr makeSiglentSsaDriver();
[[nodiscard]] DriverPtr makeRigolDsaDriver();

} // namespace peakemi::drivers
