cmake_minimum_required(VERSION 3.17)

project(
    bluetooth_manager
    VERSION 0.1.0
    DESCRIPTION "Cross-platform Bluetooth pairing GDExtension for Godot"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DBUS REQUIRED dbus-1)
endif()

add_subdirectory(godot-cpp)

set(TARGET_NAME bluetooth_manager)

set(BLUETOOTH_SOURCES
    src/register_types.cpp
    src/register_types.h
    src/bluetooth_manager.cpp
    src/bluetooth_manager.h
    src/bluetooth_device_info.h
    src/backend/bluetooth_backend.h
    src/backend/bluetooth_commands.h
    src/backend/bluetooth_events.h
    src/backend/backend_factory.cpp
    src/threading/thread_safe_queue.h
    src/threading/worker_thread.cpp
    src/threading/worker_thread.h
)

if(WIN32)
    list(APPEND BLUETOOTH_SOURCES
        src/platform/windows/windows_backend.cpp
        src/platform/windows/windows_backend.h
        src/platform/windows/winrt_bluetooth.cpp
        src/platform/windows/winrt_bluetooth.h
    )
elseif(APPLE)
    list(APPEND BLUETOOTH_SOURCES
        src/platform/macos/macos_backend.mm
        src/platform/macos/macos_backend.h
    )
elseif(UNIX)
    list(APPEND BLUETOOTH_SOURCES
        src/platform/linux/bluez_agent.cpp
        src/platform/linux/bluez_agent.h
        src/platform/linux/bluez_dbus.cpp
        src/platform/linux/bluez_dbus.h
        src/platform/linux/linux_backend.cpp
        src/platform/linux/linux_backend.h
    )
endif()

add_library(${TARGET_NAME} SHARED ${BLUETOOTH_SOURCES})

target_include_directories(${TARGET_NAME} PRIVATE src)

target_link_libraries(${TARGET_NAME} PRIVATE godot-cpp)

get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)

set(OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/demo/bin/")

set_target_properties(
    ${TARGET_NAME}
    PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        POSITION_INDEPENDENT_CODE ON
        LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
        PREFIX "lib"
        OUTPUT_NAME "bluetooth_manager${GODOTCPP_SUFFIX}"
)

if(WIN32)
    target_link_libraries(${TARGET_NAME} PRIVATE windowsapp)
    target_compile_definitions(${TARGET_NAME} PRIVATE _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS)
    target_compile_options(${TARGET_NAME} PRIVATE /EHsc /std:c++20)
    if(MSVC)
        set_target_properties(${TARGET_NAME} PROPERTIES
            PDB_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
        )
    endif()
elseif(APPLE)
    set_target_properties(
        ${TARGET_NAME}
        PROPERTIES
            SUFFIX ""
            OUTPUT_NAME "bluetooth_manager${GODOTCPP_SUFFIX}"
    )
    target_link_libraries(${TARGET_NAME} PRIVATE
        "-framework IOBluetooth"
        "-framework Foundation"
    )
elseif(UNIX)
    target_include_directories(${TARGET_NAME} PRIVATE ${DBUS_INCLUDE_DIRS})
    target_link_libraries(${TARGET_NAME} PRIVATE ${DBUS_LIBRARIES})
endif()

add_executable(bluetooth_manager_tests tests/test_device_info.cpp)
set_target_properties(bluetooth_manager_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests"
    CXX_STANDARD 17
)