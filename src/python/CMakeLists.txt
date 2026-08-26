# The Python bridge is optional (NFR-BLD-5). Without PEAKEMI_WITH_PYTHON the
# module still builds: the trust store and the manifest rules are plain C++ and
# useful on their own, and everything that needs an interpreter reports
# ErrorCode::NotImplemented.
peakemi_add_module(python
    SOURCES
        bindings/PluginModule.cpp
        bridge/PluginManifest.cpp
        bridge/PluginRegistry.cpp
        bridge/PythonDriverProxy.cpp
        bridge/PythonInterpreter.cpp
        bridge/TrustStore.cpp
    HEADERS
        include/peakemi/python/PluginManifest.h
        include/peakemi/python/PluginRegistry.h
        include/peakemi/python/PythonDriverProxy.h
        include/peakemi/python/PythonInterpreter.h
        include/peakemi/python/TrustStore.h
    PUBLIC
        peakemi::core
        peakemi::hal
        Qt6::Core
    PRIVATE
        nlohmann_json::nlohmann_json)

if(PEAKEMI_WITH_PYTHON)
    target_link_libraries(peakemi_python PRIVATE pybind11::embed)
    target_compile_definitions(peakemi_python PRIVATE PEAKEMI_HAVE_PYTHON)
endif()
