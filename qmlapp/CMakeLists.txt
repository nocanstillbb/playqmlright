qt_add_executable(appqmlapp
    main.cpp
)

qt_add_qml_module(appqmlapp
    URI qmlapp
    VERSION 1.0
    QML_FILES
        main.qml
        example/TestBench.qml
)

set_target_properties(appqmlapp PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appqmlapp
    PRIVATE qmlinspector Qt6::Quick
)

include(GNUInstallDirs)
install(TARGETS appqmlapp
    BUNDLE  DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
