add_executable(slotmap-demo)

target_sources(slotmap-demo
        PUBLIC FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/demo.cppm
)

target_link_libraries(slotmap-demo PRIVATE inco::slotmap)