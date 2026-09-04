peakemi_add_module(hal
    QT_AUTOMOC
    SOURCES
        discovery/Discovery.cpp
        discovery/UsbDiscovery.cpp
        registry/DriverRegistry.cpp
        scpi/Scpi.cpp
        scpi/UsbTmcProtocol.cpp
        transport/SerialScpiTransport.cpp
        transport/TcpScpiTransport.cpp
        transport/TransportFactory.cpp
        transport/UsbTmcTransport.cpp
        transport/VisaTransport.cpp
        transport/Vxi11Protocol.cpp
        transport/Vxi11Transport.cpp
    HEADERS
        include/peakemi/hal/Discovery.h
        include/peakemi/hal/UsbDiscovery.h
        include/peakemi/hal/DriverRegistry.h
        include/peakemi/hal/Scpi.h
        include/peakemi/hal/SerialScpiTransport.h
        include/peakemi/hal/TcpScpiTransport.h
        include/peakemi/hal/TransportFactory.h
        include/peakemi/hal/UsbTmcProtocol.h
        include/peakemi/hal/UsbTmcTransport.h
        include/peakemi/hal/VisaTransport.h
        include/peakemi/hal/Vxi11Protocol.h
        include/peakemi/hal/Vxi11Transport.h
    PUBLIC
        peakemi::core
        Qt6::Core
        Qt6::Network
        Qt6::SerialPort
    PRIVATE
        Qt6::Concurrent)

# USB support is optional: without libusb the transport still compiles and every
# entry point reports ErrorCode::NotImplemented (NFR-BLD-5 applies here too).
if(PEAKEMI_WITH_USBTMC)
    target_link_libraries(peakemi_hal PRIVATE peakemi::libusb)
    target_compile_definitions(peakemi_hal PRIVATE PEAKEMI_HAVE_LIBUSB)
endif()

# VISA is resolved at run time, so this only decides whether the code is built;
# no VISA headers or import libraries are needed to compile it.
if(PEAKEMI_WITH_VISA)
    target_compile_definitions(peakemi_hal PRIVATE PEAKEMI_WITH_VISA)
endif()
