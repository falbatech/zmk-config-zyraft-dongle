# Include ZMK headers globalnie dla naszych source files
zephyr_include_directories(boards/shields/zyraft/include)
zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)

zephyr_library()

# Source files tylko gdy zyraft_dongle z display
if(CONFIG_SHIELD_ZYRAFT_DONGLE AND CONFIG_ZMK_DISPLAY)
    zephyr_library_sources(boards/shields/zyraft/src/status_screen.c)
    zephyr_library_sources(boards/shields/zyraft/src/falbatech_logo_large.c)
    zephyr_library_sources(boards/shields/zyraft/src/falbatech_logo_small.c)
endif()
