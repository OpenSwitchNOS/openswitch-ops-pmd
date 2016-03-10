/*
 *  (c) Copyright 2015 Hewlett Packard Enterprise Development LP
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License. You may obtain
 *  a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 *  License for the specific language governing permissions and limitations
 *  under the License.
 */

/************************************************************************//**
 * @ingroup ops-pmd
 *
 * @file
 * Source file for pluggable module detection functions.
 ***************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <math.h>

#include <vswitch-idl.h>
#include <openswitch-idl.h>

#include "pmd.h"
#include "plug.h"

VLOG_DEFINE_THIS_MODULE(pm_detect);

#define SFP_BIT_RATE_NOMINAL_10G    0x64

// some fields are padded with spaces - need to strip trailing spaces
#define SPACE   0x20

static char ascii_map[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                           '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

//
// hex_to_ascii: Converts binary data in input buffer to ascii format.
//               Allocates and returns converted ascii buffer.
//
char *
hex_to_ascii(char *buf, int buf_size)
{
    int i = 0;
    int j = 0;
    const int word_size = 4;
    char *ascii;

    // Allocate sufficient space for ascii conversion, word separation
    // characters and NUL termination.
    ascii = malloc((2 * buf_size) + (buf_size / word_size) + 1);
    if (NULL == ascii) {
        return NULL;
    }

    for (i=0; i<buf_size; ++i) {
        if ( (i > 0 ) && (0 ==  i % word_size)) {
            ascii[j++] = SPACE;
        }
        ascii[j++] = ascii_map[(buf[i] & 0xf0) >> 4];
        ascii[j++] = ascii_map[buf[i] & 0xf];
    }
    ascii[j] = 0;
    return ascii;
}

STATIC void
set_supported_speeds(pm_port_t *port, size_t count, ...)
{
    va_list args;
    size_t idx;
    int speed;
    char *speeds = NULL;
    char *new_speeds = NULL;

    va_start(args, count);
    if (NULL != port->ovs_module_columns.supported_speeds) {
        free(port->ovs_module_columns.supported_speeds);
    }

    for (idx = 0; idx < count; idx++) {
        speed = va_arg(args, int);
        asprintf(&new_speeds, "%s%s%d",
                 (NULL == speeds) ? "" :  speeds,
                 (0 == idx) ? "" :  " ",
                 speed);
        if (speeds != NULL) {
            free(speeds);
        }
        speeds = new_speeds;
    }

    va_end(args);
    port->ovs_module_columns.supported_speeds = speeds;
    port->module_info_changed = 1;
}

//
// pm_oui_format: format oui data into a string, instead of binary data
//
STATIC void
pm_oui_format(char *ascii_oui, unsigned char *binary_oui)
{
    snprintf(ascii_oui,
        PM_VENDOR_OUI_LEN*3,
        "%02x-%02x-%02x",
        binary_oui[0],
        binary_oui[1],
        binary_oui[2]);
}

//
// pm_parse: get important data out of serial id data
//
int
pm_parse(
    pm_sfp_serial_id_t  *serial_datap,
    pm_port_t           *port)
{
    int                     type;
    char                    vendor_name[PM_VENDOR_NAME_LEN+1];
    char                    vendor_part_number[PM_VENDOR_PN_LEN+1];
    char                    vendor_revision[PM_SFP_VENDOR_REV_LEN+1];
    char                    vendor_serial_number[PM_VENDOR_SN_LEN+1];
    char                    vendor_oui[PM_VENDOR_OUI_LEN*3];
    size_t                  idx;
    pm_qsfp_serial_id_t*    qsfpp_serial_id;

    // ignore modules that aren't pluggable
    if (false == port->module_device->pluggable) {
        VLOG_DBG("port is not pluggable: %s", port->instance);
        return 0;
    }

    // ignore modules that don't have connector data
    if (NULL == port->module_device->connector) {
        VLOG_WARN("no connector info for port: %s", port->instance);
        return -1;
    }

    // prepare for handling SFP+, QSFP+ and QSFP28 differently
    if (strcmp(port->module_device->connector, CONNECTOR_SFP_PLUS) == 0) {
        type = MODULE_TYPE_SFP_PLUS;
    } else if (strcmp(port->module_device->connector, CONNECTOR_QSFP_PLUS) == 0) {
        type = MODULE_TYPE_QSFP_PLUS;
    } else if (strcmp(port->module_device->connector, CONNECTOR_QSFP28) == 0) {
        type = MODULE_TYPE_QSFP28;
    } else {
        VLOG_WARN("unknown connector type for port: %s (%s)",
                  port->instance, port->module_device->connector);
        pm_delete_all_data(port);
        SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_UNKNOWN);
        return -1;
    }

    switch (type) {
        case MODULE_TYPE_SFP_PLUS:
            VLOG_DBG("port is SFP plus pluggable: %s", port->instance);
            // Supported SFP module types
            if (PM_CONNECTOR_COPPER_PIGTAIL == serial_datap->connector) {
                unsigned int speed = 0;
                char *cable_tech = OVSREC_INTERFACE_PM_INFO_CABLE_TECHNOLOGY_PASSIVE;
                port->optical = false;

                SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_DAC);
                SET_STATIC_STRING(port, connector_status,
                                  OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                VLOG_DBG("bit rate for %s is 0x%x",
                         port->instance, serial_datap->bit_rate_nominal);
                if (serial_datap->bit_rate_nominal >= SFP_BIT_RATE_NOMINAL_10G) {
                    speed = 10000;
                } else {
                    speed = 1000;
                }
                VLOG_DBG("module is DAC at %d: %s", speed, port->instance);
                SET_INT_STRING(port, max_speed, speed);
                set_supported_speeds(port, 1, speed);
                // determine active/passive
                if (0 != serial_datap->transceiver.cable_technology_active) {
                    cable_tech = OVSREC_INTERFACE_PM_INFO_CABLE_TECHNOLOGY_ACTIVE;
                } else if (0 != serial_datap->transceiver.cable_technology_passive) {
                    cable_tech = OVSREC_INTERFACE_PM_INFO_CABLE_TECHNOLOGY_PASSIVE;
                }
                SET_STATIC_STRING(port, cable_technology, cable_tech);
                SET_INT_STRING(port, cable_length, serial_datap->length_copper);
            } else if (0 != serial_datap->transceiver.enet_1000base_sx) {
                VLOG_DBG("module is 1G_SX: %s", port->instance);
                // handle sx type
                port->optical = true;
                SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_SX);
                SET_STATIC_STRING(port, connector_status,
                                  OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                SET_INT_STRING(port, max_speed, 1000);
                set_supported_speeds(port, 1, 1000);
                DELETE(port, cable_technology);
                DELETE_FREE(port, cable_length);
            } else if (0 != serial_datap->transceiver.enet_1000base_lx) {
                // handle lx type
                VLOG_DBG("module is 1G_LX: %s", port->instance);
                port->optical = true;
                SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_LX);
                SET_STATIC_STRING(port, connector_status,
                                  OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                SET_INT_STRING(port, max_speed, 1000);
                set_supported_speeds(port, 1, 1000);
                DELETE(port, cable_technology);
                DELETE_FREE(port, cable_length);
            } else if (0 != serial_datap->transceiver.enet_1000base_cx) {
                // handle cx type
                port->optical = false;
                VLOG_DBG("module is 1G_CX: %s", port->instance);
                SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_CX);
                SET_STATIC_STRING(port, connector_status,
                                  OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                SET_INT_STRING(port, max_speed, 1000);
                set_supported_speeds(port, 1, 1000);
                DELETE(port, cable_technology);
                DELETE_FREE(port, cable_length);
            } else if (0 != serial_datap->transceiver.enet_1000base_t) {
                // handle RJ45 type
                port->optical = false;
                VLOG_DBG("module is 1G RJ45: %s", port->instance);
                SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_RJ45);
                SET_STATIC_STRING(port, connector_status,
                                  OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                SET_INT_STRING(port, max_speed, 1000);
                set_supported_speeds(port, 1, 1000);
                DELETE(port, cable_technology);
                DELETE_FREE(port, cable_length);
            } else if (0 != serial_datap->transceiver.enet_10gbase_sr) {
                // handle sr type
                VLOG_DBG("module is 10G SR: %s", port->instance);
                port->optical = true;
                SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_SR);
                SET_STATIC_STRING(port, connector_status,
                                  OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                SET_INT_STRING(port, max_speed, 10000);
                set_supported_speeds(port, 1, 10000);
                DELETE(port, cable_technology);
                DELETE_FREE(port, cable_length);
            } else if (0 != serial_datap->transceiver.enet_10gbase_lr) {
                VLOG_DBG("module is 10G LR: %s", port->instance);
                port->optical = true;
                SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_LR);
                SET_STATIC_STRING(port, connector_status,
                                  OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                SET_INT_STRING(port, max_speed, 10000);
                set_supported_speeds(port, 1, 10000);
                DELETE(port, cable_technology);
                DELETE_FREE(port, cable_length);
            } else if (0 != serial_datap->transceiver.enet_10gbase_lrm) {
                VLOG_DBG("module is 10G LRM: %s", port->instance);
                port->optical = true;
                SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_SFP_LRM);
                SET_STATIC_STRING(port, connector_status,
                                  OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                SET_INT_STRING(port, max_speed, 10000);
                set_supported_speeds(port, 1, 10000);
                DELETE(port, cable_technology);
                DELETE_FREE(port, cable_length);
            } else {
                VLOG_DBG("module is unrecognized: %s", port->instance);
                port->optical = false;
                SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_UNKNOWN);
                SET_STATIC_STRING(port, connector_status,
                                  OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_UNRECOGNIZED);
                SET_INT_STRING(port, max_speed, 0);
                set_supported_speeds(port, 1, 0);
                DELETE(port, cable_technology);
                DELETE_FREE(port, cable_length);
            }
            // fill in the rest of the data
            DELETE(port, power_mode);

            // vendor name
            memcpy(vendor_name, serial_datap->vendor_name, PM_VENDOR_NAME_LEN);
            vendor_name[PM_VENDOR_NAME_LEN] = 0;
            // strip trailing spaces
            idx = PM_VENDOR_NAME_LEN - 1;
            while (idx > 0 && SPACE == vendor_name[idx]) {
                vendor_name[idx] = 0;
                idx--;
            }

            SET_STRING(port, vendor_name, vendor_name);

            // vendor_oui
            pm_oui_format(vendor_oui, serial_datap->vendor_oui);

            SET_STRING(port, vendor_oui, vendor_oui);

            // vendor_part_number
            memcpy(vendor_part_number, serial_datap->vendor_part_number, PM_VENDOR_PN_LEN);
            vendor_part_number[PM_VENDOR_PN_LEN] = 0;
            // strip trailing spaces
            idx = PM_VENDOR_PN_LEN - 1;
            while (idx > 0 && SPACE == vendor_part_number[idx]) {
                vendor_part_number[idx] = 0;
                idx--;
            }

            SET_STRING(port, vendor_part_number, vendor_part_number);

            // vendor_revision
            memcpy(vendor_revision, serial_datap->vendor_revision, PM_SFP_VENDOR_REV_LEN);
            vendor_revision[PM_SFP_VENDOR_REV_LEN] = 0;
            // strip trailing spaces
            idx = PM_SFP_VENDOR_REV_LEN - 1;
            while (idx > 0 && SPACE == vendor_revision[idx]) {
                vendor_revision[idx] = 0;
                idx--;
            }

            SET_STRING(port, vendor_revision, vendor_revision);

            // vendor_serial_number
            memcpy(vendor_serial_number, serial_datap->vendor_serial_number, PM_VENDOR_SN_LEN);
            vendor_serial_number[PM_VENDOR_SN_LEN] = 0;
            // strip trailing spaces
            idx = PM_VENDOR_SN_LEN - 1;
            while (idx > 0 && SPACE == vendor_serial_number[idx]) {
                vendor_serial_number[idx] = 0;
                idx--;
            }

            SET_STRING(port, vendor_serial_number, vendor_serial_number);

            // a0
            SET_BINARY(port, a0, (char *) serial_datap, sizeof(pm_sfp_serial_id_t));
            break;
        case MODULE_TYPE_QSFP_PLUS:

            // QSFP has a different structure definition (similar, but not
            // the same)
            VLOG_DBG("port is QSFP plus pluggable: %s", port->instance);

            qsfpp_serial_id = (pm_qsfp_serial_id_t *)serial_datap;

            if (0 != qsfpp_serial_id->spec_compliance.enet_40gbase_lr4) {
                VLOG_DBG("module is 40G_LR4: %s", port->instance);
                // handle LR4 type
                port->optical = true;
                SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP_LR4);
                SET_STATIC_STRING(port, connector_status,
                                  OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                SET_INT_STRING(port, max_speed, 40000);
                set_supported_speeds(port, 1, 40000);
                DELETE(port, cable_technology);
                DELETE_FREE(port, cable_length);
            } else if (0 != qsfpp_serial_id->spec_compliance.enet_40gbase_sr4) {
                VLOG_DBG("module is 40G_SR4: %s", port->instance);
                // handle SR4 type
                port->optical = true;
                SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP_SR4);
                SET_STATIC_STRING(port, connector_status,
                                  OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                SET_INT_STRING(port, max_speed, 40000);
                set_supported_speeds(port, 1, 40000);
                DELETE(port, cable_technology);
                DELETE_FREE(port, cable_length);
            } else if (0 != qsfpp_serial_id->spec_compliance.enet_40gbase_cr4) {
                VLOG_DBG("module is 40G_CR4: %s", port->instance);
                // handle CR4 type
                port->optical = false;
                SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP_CR4);
                SET_STATIC_STRING(port, connector_status,
                                  OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                SET_INT_STRING(port, max_speed, 40000);
                set_supported_speeds(port, 1, 40000);
                DELETE(port, cable_technology);
                DELETE_FREE(port, cable_length);
            } else {
                VLOG_DBG("module is unsupported: %s", port->instance);
                port->optical = false;
                SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_UNKNOWN);
                SET_STATIC_STRING(port, connector_status,
                                  OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_UNRECOGNIZED);
                SET_INT_STRING(port, max_speed, 0);
                set_supported_speeds(port, 1, 0);
                DELETE(port, cable_technology);
                DELETE_FREE(port, cable_length);
            }
            // fill in the rest of the data
            // OPS_TODO: fill in the power mode
            DELETE(port, power_mode);

            // vendor name
            memcpy(vendor_name, qsfpp_serial_id->vendor_name, PM_VENDOR_NAME_LEN);
            vendor_name[PM_VENDOR_NAME_LEN] = 0;
            // strip trailing spaces
            idx = PM_VENDOR_NAME_LEN - 1;
            while (idx > 0 && SPACE == vendor_name[idx]) {
                vendor_name[idx] = 0;
                idx--;
            }

            SET_STRING(port, vendor_name, vendor_name);

            // vendor_oui
            pm_oui_format(vendor_oui, qsfpp_serial_id->vendor_oui);

            SET_STRING(port, vendor_oui, vendor_oui);

            // vendor_part_number
            memcpy(vendor_part_number, qsfpp_serial_id->vendor_part_number, PM_VENDOR_PN_LEN);
            vendor_part_number[PM_VENDOR_PN_LEN] = 0;
            // strip trailing spaces
            idx = PM_VENDOR_PN_LEN - 1;
            while (idx > 0 && SPACE == vendor_part_number[idx]) {
                vendor_part_number[idx] = 0;
                idx--;
            }

            SET_STRING(port, vendor_part_number, vendor_part_number);

            // vendor_revision
            memcpy(vendor_revision, qsfpp_serial_id->vendor_revision, PM_QSFP_VENDOR_REV_LEN);
            vendor_revision[PM_QSFP_VENDOR_REV_LEN] = 0;
            // strip trailing spaces
            idx = PM_QSFP_VENDOR_REV_LEN - 1;
            while (idx > 0 && SPACE == vendor_revision[idx]) {
                vendor_revision[idx] = 0;
                idx--;
            }

            SET_STRING(port, vendor_revision, vendor_revision);

            // vendor_serial_number
            memcpy(vendor_serial_number, qsfpp_serial_id->vendor_serial_number, PM_VENDOR_SN_LEN);
            vendor_serial_number[PM_VENDOR_SN_LEN] = 0;
            // strip trailing spaces
            idx = PM_VENDOR_SN_LEN - 1;
            while (idx > 0 && SPACE == vendor_serial_number[idx]) {
                vendor_serial_number[idx] = 0;
                idx--;
            }

            SET_STRING(port, vendor_serial_number, vendor_serial_number);

            // a0
            SET_BINARY(port, a0, (char *) qsfpp_serial_id, sizeof(pm_qsfp_serial_id_t));
            break;
        case MODULE_TYPE_QSFP28:

            // Use the same structure definition used for MODULE_TYPE_QSFP_PLUS case
            VLOG_DBG("port is QSFP 28 pluggable: %s", port->instance);

            qsfpp_serial_id = (pm_qsfp_serial_id_t *)serial_datap;

            if (0 != qsfpp_serial_id->spec_compliance.enet_extended) {
                switch (qsfpp_serial_id->options.ext_compliance_code) {
                    case PM_QSFP_EXT_COMPLIANCE_CODE_100GBASE_SR4:
                        VLOG_DBG("module is 100G_SR4: %s", port->instance);
                        // handle SR4 type
                        port->optical = true;
                        SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP28_SR4);
                        SET_STATIC_STRING(port, connector_status,
                                          OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                        SET_INT_STRING(port, max_speed, 100000);
                        set_supported_speeds(port, 1, 100000);
                        DELETE(port, cable_technology);
                        DELETE_FREE(port, cable_length);
                        break;
                    case PM_QSFP_EXT_COMPLIANCE_CODE_100GBASE_LR4:
                        VLOG_DBG("module is 100G_LR4: %s", port->instance);
                        // handle LR4 type
                        port->optical = true;
                        SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP28_LR4);
                        SET_STATIC_STRING(port, connector_status,
                                          OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                        SET_INT_STRING(port, max_speed, 100000);
                        set_supported_speeds(port, 1, 100000);
                        DELETE(port, cable_technology);
                        DELETE_FREE(port, cable_length);
                        break;
                    case PM_QSFP_EXT_COMPLIANCE_CODE_100GBASE_CWDM4:
                        VLOG_DBG("module is 100G_CWDM4: %s", port->instance);
                        // handle CWDM4 type
                        port->optical = true;
                        SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP28_CWDM4);
                        SET_STATIC_STRING(port, connector_status,
                                          OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                        SET_INT_STRING(port, max_speed, 100000);
                        set_supported_speeds(port, 1, 100000);
                        DELETE(port, cable_technology);
                        DELETE_FREE(port, cable_length);
                        break;
                    case PM_QSFP_EXT_COMPLIANCE_CODE_100GBASE_PSM4:
                        VLOG_DBG("module is 100G_PSM4: %s", port->instance);
                        // handle PSM4 type
                        port->optical = true;
                        SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP28_PSM4);
                        SET_STATIC_STRING(port, connector_status,
                                          OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                        SET_INT_STRING(port, max_speed, 100000);
                        set_supported_speeds(port, 1, 100000);
                        DELETE(port, cable_technology);
                        DELETE_FREE(port, cable_length);
                        break;
                    case PM_QSFP_EXT_COMPLIANCE_CODE_100GBASE_CR4:
                        VLOG_DBG("module is 100G_CR4: %s", port->instance);
                        // handle CR4 type
                        port->optical = false;
                        SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP28_CR4);
                        SET_STATIC_STRING(port, connector_status,
                                          OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                        SET_INT_STRING(port, max_speed, 100000);
                        set_supported_speeds(port, 1, 100000);
                        DELETE(port, cable_technology);
                        DELETE_FREE(port, cable_length);
                        break;
                    case PM_QSFP_EXT_COMPLIANCE_CODE_100GBASE_CLR4:
                        VLOG_DBG("module is 100G_CLR4: %s", port->instance);
                        // handle CLR4 type
                        port->optical = true;
                        SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP28_CLR4);
                        SET_STATIC_STRING(port, connector_status,
                                          OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                        SET_INT_STRING(port, max_speed, 100000);
                        set_supported_speeds(port, 1, 100000);
                        DELETE(port, cable_technology);
                        DELETE_FREE(port, cable_length);
                        break;
                    default:
                        VLOG_DBG("module is unsupported: %s", port->instance);
                        port->optical = false;
                        SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_UNKNOWN);
                        SET_STATIC_STRING(port, connector_status,
                                          OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_UNRECOGNIZED);
                        SET_INT_STRING(port, max_speed, 0);
                        set_supported_speeds(port, 1, 0);
                        DELETE(port, cable_technology);
                        DELETE_FREE(port, cable_length);
                        break;
                }
            } else {
                if (0 != qsfpp_serial_id->spec_compliance.enet_40gbase_lr4) {
                    VLOG_DBG("module is 40G_LR4: %s", port->instance);
                    // handle LR4 type
                    port->optical = true;
                    SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP_LR4);
                    SET_STATIC_STRING(port, connector_status,
                                      OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                    SET_INT_STRING(port, max_speed, 40000);
                    set_supported_speeds(port, 1, 40000);
                    DELETE(port, cable_technology);
                    DELETE_FREE(port, cable_length);
                } else if (0 != qsfpp_serial_id->spec_compliance.enet_40gbase_sr4) {
                    VLOG_DBG("module is 40G_SR4: %s", port->instance);
                    // handle SR4 type
                    port->optical = true;
                    SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP_SR4);
                    SET_STATIC_STRING(port, connector_status,
                                      OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                    SET_INT_STRING(port, max_speed, 40000);
                    set_supported_speeds(port, 1, 40000);
                    DELETE(port, cable_technology);
                    DELETE_FREE(port, cable_length);
                } else if (0 != qsfpp_serial_id->spec_compliance.enet_40gbase_cr4) {
                    VLOG_DBG("module is 40G_CR4: %s", port->instance);
                    // handle CR4 type
                    port->optical = false;
                    SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_QSFP_CR4);
                    SET_STATIC_STRING(port, connector_status,
                                      OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_SUPPORTED);
                    SET_INT_STRING(port, max_speed, 40000);
                    set_supported_speeds(port, 1, 40000);
                    DELETE(port, cable_technology);
                    DELETE_FREE(port, cable_length);
                } else {
                    VLOG_DBG("module is unsupported: %s", port->instance);
                    port->optical = false;
                    SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_UNKNOWN);
                    SET_STATIC_STRING(port, connector_status,
                                      OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_UNRECOGNIZED);
                    SET_INT_STRING(port, max_speed, 0);
                    set_supported_speeds(port, 1, 0);
                    DELETE(port, cable_technology);
                    DELETE_FREE(port, cable_length);
                }
            }
            // fill in the rest of the data
            // OPS_TODO: fill in the power mode
            DELETE(port, power_mode);

            // vendor name
            memcpy(vendor_name, qsfpp_serial_id->vendor_name, PM_VENDOR_NAME_LEN);
            vendor_name[PM_VENDOR_NAME_LEN] = 0;
            // strip trailing spaces
            idx = PM_VENDOR_NAME_LEN - 1;
            while (idx > 0 && SPACE == vendor_name[idx]) {
                vendor_name[idx] = 0;
                idx--;
            }

            SET_STRING(port, vendor_name, vendor_name);

            // vendor_oui
            pm_oui_format(vendor_oui, qsfpp_serial_id->vendor_oui);

            SET_STRING(port, vendor_oui, vendor_oui);

            // vendor_part_number
            memcpy(vendor_part_number, qsfpp_serial_id->vendor_part_number, PM_VENDOR_PN_LEN);
            vendor_part_number[PM_VENDOR_PN_LEN] = 0;
            // strip trailing spaces
            idx = PM_VENDOR_PN_LEN - 1;
            while (idx > 0 && SPACE == vendor_part_number[idx]) {
                vendor_part_number[idx] = 0;
                idx--;
            }

            SET_STRING(port, vendor_part_number, vendor_part_number);

            // vendor_revision
            memcpy(vendor_revision, qsfpp_serial_id->vendor_revision, PM_QSFP_VENDOR_REV_LEN);
            vendor_revision[PM_QSFP_VENDOR_REV_LEN] = 0;
            // strip trailing spaces
            idx = PM_QSFP_VENDOR_REV_LEN - 1;
            while (idx > 0 && SPACE == vendor_revision[idx]) {
                vendor_revision[idx] = 0;
                idx--;
            }

            SET_STRING(port, vendor_revision, vendor_revision);

            // vendor_serial_number
            memcpy(vendor_serial_number, qsfpp_serial_id->vendor_serial_number, PM_VENDOR_SN_LEN);
            vendor_serial_number[PM_VENDOR_SN_LEN] = 0;
            // strip trailing spaces
            idx = PM_VENDOR_SN_LEN - 1;
            while (idx > 0 && SPACE == vendor_serial_number[idx]) {
                vendor_serial_number[idx] = 0;
                idx--;
            }

            SET_STRING(port, vendor_serial_number, vendor_serial_number);

            // a0
            SET_BINARY(port, a0, (char *) qsfpp_serial_id, sizeof(pm_qsfp_serial_id_t));
            break;
        default:
            VLOG_WARN("port is unrecognized pluggable type: %s", port->instance);
            break;
    }

    return 0;
} // pm_parse

//
// pm_byte_sum: caluclate the sum of bytes from start to end (inclusive)
//              and compare to byte at offset
//
static int
pm_byte_sum(unsigned char *block, int start, int end, int offset)
{
    unsigned char value = 0;
    int idx;

    for (idx = start; idx <= end; idx++) {
        value += block[idx];
    }

    if (value != block[offset]) {
        return 1;
    }

    return 0;
}

//
// sfpp_sum_verify: verify the checksum values (works for both SFP+ and QSFP)
//
int
sfpp_sum_verify(unsigned char *serial_datap)
{
    int check;
    // verify CC_BASE
    check = pm_byte_sum(serial_datap, 0, 62, 63);

    // verify CC_EXT
    check += pm_byte_sum(serial_datap, 64, 94, 95);

    return check;
}
