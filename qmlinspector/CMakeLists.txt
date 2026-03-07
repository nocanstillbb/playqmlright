add_library(qmlinspector SHARED
    InspectorServer.h
    InspectorServer.cpp
)

target_include_directories(qmlinspector
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/qmlinspector>
)

target_link_libraries(qmlinspector
    PUBLIC  Qt6::Quick
    PRIVATE Qt6::Network
)

set_target_properties(qmlinspector PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# ── Install ────────────────────────────────────────────────────────────────

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS qmlinspector
    EXPORT  qmlinspectorTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES InspectorServer.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qmlinspector
)

install(EXPORT qmlinspectorTargets
    FILE      qmlinspectorTargets.cmake
    NAMESPACE qmlinspector::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qmlinspector
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/qmlinspectorConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qmlinspector
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/qmlinspectorConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/qmlinspector
)
