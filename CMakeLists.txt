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

find_package(Python3 3.4 REQUIRED COMPONENTS Interpreter)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DBUS REQUIRED dbus-1)
    find_package(Threads REQUIRED)
endif()

# godot-cpp 4.4 ships a 4.4.1 API; pin bindings to 4.4.0 so the DLL loads on Godot 4.4.stable.
set(GODOT_44_API_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/godot-4.4.0-api/extension_api.json")
if(EXISTS "${GODOT_44_API_FILE}" AND NOT GODOTCPP_CUSTOM_API_FILE)
    set(GODOTCPP_CUSTOM_API_FILE "${GODOT_44_API_FILE}" CACHE FILEPATH "Godot extension API JSON for godot-cpp bindings")
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
        src/platform/linux/dbus_loader.cpp
        src/platform/linux/dbus_loader.h
        src/platform/linux/linux_backend.cpp
        src/platform/linux/linux_backend.h
    )
endif()

add_library(${TARGET_NAME} SHARED ${BLUETOOTH_SOURCES})

file(GLOB BLUETOOTH_DOC_XML CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/doc_classes/*.xml")
if(BLUETOOTH_DOC_XML)
    set(BLUETOOTH_DOC_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/gen/doc_source.cpp")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gen")
    add_custom_command(
        OUTPUT "${BLUETOOTH_DOC_SOURCE}"
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_extension_docs.py"
            "${BLUETOOTH_DOC_SOURCE}"
            ${BLUETOOTH_DOC_XML}
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_extension_docs.py"
            "${godot-cpp_SOURCE_DIR}/doc_source_generator.py"
            ${BLUETOOTH_DOC_XML}
        COMMENT "Generating BluetoothManager editor documentation"
        VERBATIM
    )
    target_sources(${TARGET_NAME} PRIVATE "${BLUETOOTH_DOC_SOURCE}")
    set_source_files_properties("${BLUETOOTH_DOC_SOURCE}" PROPERTIES GENERATED TRUE)
    source_group("generated" FILES "${BLUETOOTH_DOC_SOURCE}")
endif()

target_include_directories(${TARGET_NAME} PRIVATE src)

target_link_libraries(${TARGET_NAME} PRIVATE godot-cpp)

get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)

set(OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/bin")

set(ADDONS_BIN_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/addons/bluetooth_gd/bin"
)

set(DEMO_ADDONS_BIN_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/demo/addons/bluetooth_gd/bin"
)

file(MAKE_DIRECTORY "${ADDONS_BIN_DIR}")
file(MAKE_DIRECTORY "${DEMO_ADDONS_BIN_DIR}")

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

add_custom_command(
    TARGET ${TARGET_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${TARGET_NAME}>"
        "${ADDONS_BIN_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${TARGET_NAME}>"
        "${DEMO_ADDONS_BIN_DIR}/"
)

if(WIN32)
    target_link_libraries(${TARGET_NAME} PRIVATE windowsapp)
    target_compile_definitions(${TARGET_NAME} PRIVATE _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS)
    target_compile_options(${TARGET_NAME} PRIVATE /EHsc /std:c++20)

    if(MSVC)
        foreach(CONFIG DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            string(TOUPPER ${CONFIG} CONFIG_UPPER)
            set_target_properties(${TARGET_NAME} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${OUTPUT_DIR}"
                LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${OUTPUT_DIR}"
            )
        endforeach()

        set_target_properties(${TARGET_NAME} PROPERTIES
            PDB_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
        )

        add_custom_command(
            TARGET ${TARGET_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_PDB_FILE:${TARGET_NAME}>"
                "${ADDONS_BIN_DIR}/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_PDB_FILE:${TARGET_NAME}>"
                "${DEMO_ADDONS_BIN_DIR}/"
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
    # libdbus is loaded at runtime via dlopen (see dbus_loader.cpp). D-Bus symbols stay
    # undefined at link time so libdbus is not a DT_NEEDED dependency of the extension.
    target_link_libraries(${TARGET_NAME} PRIVATE dl Threads::Threads)
    target_link_options(${TARGET_NAME} PRIVATE
        "-Wl,--unresolved-symbols=ignore-all"
    )
endif()

add_executable(bluetooth_manager_tests tests/test_device_info.cpp)

set_target_properties(
    bluetooth_manager_tests
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests"
        CXX_STANDARD 17
)
