cmake_minimum_required(VERSION 3.0)
project(grafana_handle)

add_executable(grafana_metrics_exporter
    main.cpp
    metrics_exporter.cpp
    ../StorageResourceAPI.cpp
)

target_include_directories(grafana_metrics_exporter PRIVATE
    ${CMAKE_SOURCE_DIR}/../../include
)

# Link any project libraries if necessary. If StorageResourceAPI functions
# are in a library target, link it here. Example:
# target_link_libraries(grafana_metrics_exporter PRIVATE StorageResourceAPI)
set_target_properties(grafana_metrics_exporter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/grafana_handle
)
