#include <peakemi/hal/SerialScpiTransport.h>
#include <peakemi/hal/TcpScpiTransport.h>
#include <peakemi/hal/TransportFactory.h>
#include <peakemi/hal/UsbTmcTransport.h>
#include <peakemi/hal/VisaTransport.h>
#include <peakemi/hal/Vxi11Transport.h>

#include <memory>

namespace peakemi::hal {

Result<TransportPtr> makeTransport(const TransportDescriptor& descriptor)
{
    switch (descriptor.kind) {
        case TransportKind::Tcp:
            return std::make_shared<TcpScpiTransport>(descriptor);
        case TransportKind::Vxi11:
            return std::make_shared<Vxi11Transport>(descriptor);
        case TransportKind::Serial:
            return std::make_shared<SerialScpiTransport>(descriptor);
        case TransportKind::UsbTmc:
            if (!UsbTmcTransport::isSupported()) {
                return fail(ErrorCode::NotImplemented,
                            "this build has no USBTMC transport; configure it with "
                            "-DPEAKEMI_WITH_USBTMC=ON");
            }
            return std::make_shared<UsbTmcTransport>(descriptor);
        case TransportKind::Visa:
            if (!VisaTransport::isAvailable()) {
                return fail(ErrorCode::NotImplemented,
                            "no VISA runtime was found on this machine; PeakEmi reaches "
                            "instruments over TCP, VXI-11, USB and serial without one");
            }
            return std::make_shared<VisaTransport>(descriptor);
        case TransportKind::Simulated:
            return fail(ErrorCode::InvalidConfiguration,
                        "the simulated instrument is a driver, not a transport");
    }
    return fail(ErrorCode::InvalidConfiguration, "unknown transport kind");
}

} // namespace peakemi::hal
