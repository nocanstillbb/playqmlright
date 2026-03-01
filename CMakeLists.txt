cmake_minimum_required(VERSION 3.16)

project(qml_mcp VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.2 COMPONENTS Quick Network REQUIRED)

qt_add_executable(appqml_mcp
    main.cpp
    InspectorServer.h
    InspectorServer.cpp
)

qt_add_qml_module(appqml_mcp
    URI qml_mcp
    VERSION 1.0
    QML_FILES
        main.qml
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appqml_mcp PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appqml_mcp
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appqml_mcp
    PRIVATE Qt6::Quick Qt6::Network)

include(GNUInstallDirs)
install(TARGETS appqml_mcp
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
