zephyr_library()
zephyr_library_sources_ifdef(CONFIG_PAW3950 src/paw3950.c)
zephyr_include_directories(include)
zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
add_subdirectory(src/behaviors)

add_library(paw3950_lib STATIC IMPORTED)
set_target_properties(paw3950_lib PROPERTIES 
  IMPORTED_LOCATION ${CMAKE_CURRENT_LIST_DIR}/lib/paw3950/libpaw3950.a
)
target_include_directories(paw3950_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
target_link_libraries(${ZEPHYR_CURRENT_LIBRARY} PUBLIC paw3950_lib)
