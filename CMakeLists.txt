cmake_minimum_required(VERSION 3.16)

project(qml_inspector_suite VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.2 COMPONENTS Quick Network REQUIRED)

add_subdirectory(qmlinspector)

option(BUILD_QMLAPP "Build the example QML application" OFF)
if(BUILD_QMLAPP)
    add_subdirectory(qmlapp)
endif()
