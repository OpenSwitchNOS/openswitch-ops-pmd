#    Copyright (C) 2015 Hewlett-Packard Development Company, L.P.
#    All Rights Reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.

cmake_minimum_required (VERSION 2.8)

set (PMD pmd)
project (${PMD})

OPTION( PLATFORM_SIMULATION "Enable platform simulation" OFF )
configure_file ("${PROJECT_SOURCE_DIR}/pmd.h.in"
                "${PROJECT_BINARY_DIR}/pmd.h")

# Rules to locate needed libraries
include(FindPkgConfig)
pkg_check_modules(CONFIG_YAML config-yaml)
pkg_check_modules(OVSCOMMON libovscommon)
pkg_check_modules(OVSDB libovsdb)

include_directories (${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}
                     ${OVSCOMMON_INCLUDE_DIRS}
)

# Source files to build pmd
set (SOURCES pmd.c ovsdb_access.c config.c plug.c pm_detect.c)

# Rules to build pluggable module daemon
add_executable (${PMD} ${SOURCES})

target_link_libraries (${PMD} ${OVSCOMMON_LIBRARIES} ${OVSDB_LIBRARIES}
                       ${CONFIG_YAML_LIBRARIES}
                       -lpthread -lrt)

# Rules to install pmd binary in rootfs
install(TARGETS ${PMD}
        RUNTIME DESTINATION bin)
