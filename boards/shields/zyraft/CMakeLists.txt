# Guard przed podwójnym przetwarzaniem: raz jako shield, raz jako moduł ZMK.
# Bez tego każdy plik trafia do dwóch bibliotek (libzephyr + module lib) → duplicate symbols.
get_property(_zyraft_done GLOBAL PROPERTY ZYRAFT_DONGLE_CMAKE_DONE)
if(_zyraft_done)
    return()
endif()
set_property(GLOBAL PROPERTY ZYRAFT_DONGLE_CMAKE_DONE TRUE)

# Observer połączeń split — kompiluje się dla dongle (centralnego)
if(CONFIG_SHIELD_ZYRAFT_DONGLE)
    zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
    zephyr_library_sources(src/split_central_status_changed.c)
    zephyr_library_sources(src/central_status_changed_observer.c)
endif()

# Pliki C dla shield - kompilują się tylko gdy dongle z display jest budowany
if(CONFIG_SHIELD_ZYRAFT_DONGLE AND CONFIG_ZMK_DISPLAY)
    zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
    zephyr_library_sources(src/status_screen.c)
    zephyr_library_sources(src/falbatech_logo_large.c)
    zephyr_library_sources(src/falbatech_logo_small.c)
endif()
