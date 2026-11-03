cmake_minimum_required(VERSION 3.21)
project(SerialESPHelper VERSION 0.25.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets SerialPort)
add_library(serial_core
    src/filter.cpp src/filter.h
    src/esptool.cpp src/esptool.h
    src/flashplan.cpp src/flashplan.h
    src/config.cpp src/config.h
    src/terminal.cpp src/terminal.h
    src/logger.cpp src/logger.h
    src/session.cpp src/session.h)
target_include_directories(serial_core PUBLIC src)
target_link_libraries(serial_core PUBLIC Qt6::Core Qt6::Gui Qt6::SerialPort)
qt_add_executable(SerialESPHelper src/main.cpp src/window.cpp src/window.h
    src/esptool_dialog.cpp src/esptool_dialog.h
    src/flashplan_dialog.cpp src/flashplan_dialog.h)
target_link_libraries(SerialESPHelper PRIVATE serial_core Qt6::Widgets)
set_target_properties(SerialESPHelper PROPERTIES WIN32_EXECUTABLE TRUE)
configure_file(example.ini SerialESPHelper.ini COPYONLY)
install(TARGETS SerialESPHelper RUNTIME DESTINATION . BUNDLE DESTINATION .)
install(FILES example.ini DESTINATION . RENAME SerialESPHelper.ini)
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    qt_add_executable(serial_tests tests/test_core.cpp)
    target_link_libraries(serial_tests PRIVATE serial_core Qt6::Test)
    target_compile_definitions(serial_tests PRIVATE EXAMPLE_INI_PATH="${CMAKE_CURRENT_SOURCE_DIR}/example.ini")
    add_test(NAME serial_core COMMAND serial_tests)
    qt_add_executable(filter_tests tests/test_filter.cpp)
    target_link_libraries(filter_tests PRIVATE serial_core Qt6::Test)
    add_test(NAME line_filters COMMAND filter_tests)
    qt_add_executable(esptool_tests tests/test_esptool.cpp)
    target_link_libraries(esptool_tests PRIVATE serial_core Qt6::Test)
    add_test(NAME esptool_helpers COMMAND esptool_tests)
    qt_add_executable(flashplan_tests tests/test_flashplan.cpp)
    target_link_libraries(flashplan_tests PRIVATE serial_core Qt6::Test)
    add_test(NAME flash_plan COMMAND flashplan_tests)
    set_tests_properties(serial_core PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_test(NAME gui_smoke COMMAND SerialESPHelper --smoke-test)
    set_tests_properties(gui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
    qt_add_executable(widget_tests tests/test_window.cpp src/window.cpp src/window.h
        src/esptool_dialog.cpp src/esptool_dialog.h
        src/flashplan_dialog.cpp src/flashplan_dialog.h)
    target_link_libraries(widget_tests PRIVATE serial_core Qt6::Widgets Qt6::Test)
    qt_add_executable(fake_esptool tests/fake_esptool.cpp)
    target_link_libraries(fake_esptool PRIVATE Qt6::Core)
    add_dependencies(widget_tests fake_esptool)
    add_test(NAME session_widgets COMMAND widget_tests)
    set_tests_properties(session_widgets PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;FAKE_ESPTOOL_PATH=$<TARGET_FILE:fake_esptool>")
endif()
