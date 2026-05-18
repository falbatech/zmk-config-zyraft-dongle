# Pliki C dla shield - kompilują się tylko gdy dongle z display jest budowany
if(CONFIG_SHIELD_ZYRAFT_DONGLE AND CONFIG_ZMK_DISPLAY)
    zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
    zephyr_library_sources(src/status_screen.c)
    zephyr_library_sources(src/falbatech_logo_large.c)
    zephyr_library_sources(src/falbatech_logo_small.c)
endif()
