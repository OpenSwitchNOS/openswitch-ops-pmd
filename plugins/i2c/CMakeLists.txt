# Copyright Mellanox Technologies, Ltd. 2001-2016.
# This software product is licensed under Apache version 2, as detailed in
# the COPYING file.

cmake_minimum_required (VERSION 2.8)

set (PMD_PLUGIN pmd-i2c-plugin)
project (${PMD_PLUGIN})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Wall -Werror")

# Rules to locate needed libraries
include(FindPkgConfig)
pkg_check_modules(OVSCOMMON REQUIRED libovscommon)

include_directories (${PROJECT_BINARY_DIR}
                     ${OVSCOMMON_INCLUDE_DIRS}
)

# Source files to build fand plugin
set (SOURCES i2c-plugin.c)

# Rules to build fand plugin
add_library (${PMD_PLUGIN} SHARED ${SOURCES})

target_link_libraries (${PMD_PLUGIN}
                       ${OVSCOMMON_LIBRARIES}
                       ${CONFIG_YAML_LIBRARIES}
)

# Rules to install ops-fand binary in rootfs
install(TARGETS ${PMD_PLUGIN}
        LIBRARY DESTINATION lib/platform/plugins
)
