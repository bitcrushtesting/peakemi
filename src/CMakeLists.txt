# Module targets. Further modules (hal, drivers, python, reporting) are added here
# as they land — see docs/architecture.md §2.2 and §14.
add_subdirectory(core)
add_subdirectory(hal)
add_subdirectory(drivers)
add_subdirectory(python)
add_subdirectory(reporting)
add_subdirectory(cli)
add_subdirectory(ui)
add_subdirectory(app)
