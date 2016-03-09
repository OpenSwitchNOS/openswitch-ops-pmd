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
 * Source file for pluggable module data processing functions.
 ***************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <vswitch-idl.h>
#include <openswitch-idl.h>

#include "pmd.h"
#include "plug.h"

VLOG_DEFINE_THIS_MODULE(plug);

extern struct shash ovs_intfs;
extern YamlConfigHandle global_yaml_handle;

extern int sfpp_sum_verify(unsigned char *);
extern int pm_parse(pm_sfp_serial_id_t *serial_datap, pm_port_t *port);
extern void pm_set_a2(pm_port_t *port, unsigned char *a2_data);

//
// pm_set_enabled: change the enabled state of pluggable modules
//
// input: none
//
// output: success only
//
int
pm_set_enabled(void)
{
    struct shash_node *node;
    pm_port_t   *port = NULL;

    SHASH_FOR_EACH(node, &ovs_intfs) {
        port = (pm_port_t *)node->data;

        pm_configure_port(port);
    }

    return 0;
}

#define MAX_DEVICE_NAME_LEN 1024

//
// pm_delete_all_data: mark all attributes as deleted
//                     except for connector, which is always present
//
void
pm_delete_all_data(pm_port_t *port)
{
    DELETE(port, connector_status);
    DELETE_FREE(port, supported_speeds);
    DELETE(port, cable_technology);
    DELETE_FREE(port, cable_length);
    DELETE_FREE(port, max_speed);
    DELETE(port, power_mode);
    DELETE_FREE(port, vendor_name);
    DELETE_FREE(port, vendor_oui);
    DELETE_FREE(port, vendor_part_number);
    DELETE_FREE(port, vendor_revision);
    DELETE_FREE(port, vendor_serial_number);
    DELETE_FREE(port, a0);
    DELETE_FREE(port, a2);
    DELETE_FREE(port, a0_uppers);
}

static bool
pm_get_presence(pm_port_t *port)
{
#ifdef PLATFORM_SIMULATION
    if (NULL != port->module_data) {
        return true;
    }
    return false;
#else
    // presence detection data
    bool                present;
    unsigned char       byte;
    unsigned short      word;
    unsigned long       dword;
    unsigned long       result;

    // device data
    const YamlDevice    *device;

    int rc;

    // i2c interface structures
    i2c_op              op;
    i2c_op *            cmds[2];
    i2c_bit_op *        reg_op;

    // retry up to 5 times if data is invalid or op fails
    int                 retry_count = 2;

    if (0 == strcmp(port->module_device->connector, CONNECTOR_SFP_PLUS)) {
        reg_op = port->module_device->module_signals.sfp.sfpp_mod_present;
    } else if (0 == strcmp(port->module_device->connector,
                           CONNECTOR_QSFP_PLUS)) {
        reg_op = port->module_device->module_signals.qsfp.qsfpp_mod_present;
    } else if (0 == strcmp(port->module_device->connector,
                           CONNECTOR_QSFP28)) {
        reg_op = port->module_device->module_signals.qsfp28.qsfp28p_mod_present;
    } else {
        VLOG_ERR("port is not pluggable: %s", port->instance);
        return false;
    }
retry_read:
    // get the presence detection device
    device = yaml_find_device(global_yaml_handle, port->subsystem, reg_op->device);

    if (NULL == device) {
        VLOG_ERR("unable to find matching YAML device for module: %s",
                 port->instance);
        return false;
    }

    // construct the i2c operation
    op.direction = READ;
    op.device = reg_op->device;
    op.register_address = reg_op->register_address;
    op.byte_count = reg_op->register_size;

    // generally, just one byte, but we allow up to 4
    switch (reg_op->register_size) {
        case 1:
            op.data = &byte;
            break;
        case 2:
            op.data = (unsigned char *)&word;
            break;
        case 4:
            op.data = (unsigned char *)&dword;
            break;
        default:
            VLOG_ERR("invalid data size for module presence: %s",
                     port->instance);
            break;
    }

    op.set_register = false;
    op.negative_polarity = false;

    cmds[0] = &op;
    cmds[1] = NULL;

    // execute the operation
    rc = i2c_execute(global_yaml_handle, port->subsystem, device, cmds);

    if (rc != 0) {
        if (retry_count != 0) {
            VLOG_WARN("module presence read failed, retrying: %s",
                      port->instance);
            retry_count--;
            goto retry_read;
        }
        VLOG_ERR("unable to read module presence: %s", port->instance);
        return false;
    }

    // get the result from the appropriately sized variable
    switch (reg_op->register_size) {
        case 1:
            result = byte & reg_op->bit_mask;
            break;
        case 2:
            result = word & reg_op->bit_mask;
            break;
        case 4:
            result = dword & reg_op->bit_mask;
            break;
        default:
            // already handled above
            result = 0; // to satisfy compiler
            break;
    }

    // calculate presence
    present = (result != 0);

    // for devices where 0 bit means present, reverse the logic
    if (reg_op->negative_polarity) {
        present = !present;
    }

    return present;
#endif
}

static int
pm_read_a0(pm_port_t *port, unsigned char *data, size_t offset)
{
#ifdef PLATFORM_SIMULATION
    memcpy(data, port->module_data, sizeof(pm_sfp_serial_id_t));
    return 0;
#else
    // device data
    const YamlDevice *device;

    // i2c interface structures
    i2c_op          op;
    i2c_op *        cmds[2];
    int             rc;

    // OPS_TODO: Need to read ready bit for QSFP modules (?)

    // get device for module eeprom
    device = yaml_find_device(global_yaml_handle, port->subsystem, port->module_device->module_eeprom);

    // construct the i2c operation to read the data
    op.direction = READ;
    op.device = device->name;
    op.register_address = offset;
    op.byte_count = sizeof(pm_sfp_serial_id_t);
    op.data = data;
    op.set_register = false;
    op.negative_polarity = false;

    cmds[0] = &op;
    cmds[1] = NULL;

    rc = i2c_execute(global_yaml_handle, port->subsystem, device, cmds);

    if (rc != 0) {
        VLOG_ERR("module read failed: %s", port->instance);
        return -1;
    }

    return 0;
#endif
}

static int
pm_read_a2(pm_port_t *port, unsigned char *a2_data)
{
#ifdef PLATFORM_SIMULATION
    return -1;
#else
    // device data
    const YamlDevice    *device;

    // i2c interface structures
    i2c_op              op;
    i2c_op *            cmds[2];
    int                 rc;
    char                a2_device_name[MAX_DEVICE_NAME_LEN];

    strncpy(a2_device_name, port->module_device->module_eeprom, MAX_DEVICE_NAME_LEN);
    strncat(a2_device_name, "_A2", MAX_DEVICE_NAME_LEN);

    // get constructed A2 device
    device = yaml_find_device(global_yaml_handle, port->subsystem, a2_device_name);

    cmds[0] = &op;
    cmds[1] = NULL;

    // initialize i2c operation
    op.direction = READ;
    op.device = device->name;
    op.register_address = 0;
    op.byte_count = PM_SFP_A2_PAGE_SIZE;
    op.data = a2_data;
    op.set_register = false;
    op.negative_polarity = false;

    rc = i2c_execute(global_yaml_handle, port->subsystem, device, cmds);

    if (rc != 0) {
        return -1;
    }

    return 0;
#endif
}

//
// pm_read_module_state: read the presence and id page for a pluggable module
//
// input: port structure
//
// output: success 0, failure !0
//
// OPS_TODO: this code needs to be refactored to simplify and clarify
int
pm_read_module_state(pm_port_t *port)
{
    int             rc;

    // presence detection data
    bool            present;

    // serial id data (SFP+ structure)
    pm_sfp_serial_id_t a0;

    // a2 page is for SFP+, only
    unsigned char   a2[PM_SFP_A2_PAGE_SIZE];

    // retry up to 2 times if data is invalid or op fails
    int             retry_count = 2;
    unsigned char   offset;

    memset(&a0, 0, sizeof(a0));
    memset(a2, 0, sizeof(a2));

    // SFP+ and QSFP serial id data are at different offsets
    // take this opportunity to get the correct presence detection operation

    if (0 == strcmp(port->module_device->connector, CONNECTOR_SFP_PLUS)) {
        offset = SFP_SERIAL_ID_OFFSET;
    } else if ((0 == strcmp(port->module_device->connector,
                            CONNECTOR_QSFP_PLUS)) ||
               (0 == strcmp(port->module_device->connector,
                            CONNECTOR_QSFP28))) {
        offset = QSFP_SERIAL_ID_OFFSET;
    } else {
        VLOG_ERR("port is not pluggable: %s", port->instance);
        return -1;
    }

retry_read:
    present = pm_get_presence(port);

    if (!present) {
        // Update only if the module was previously present or
        // the entry is uninitialized.
        if ((port->present == true) ||
            (NULL == port->ovs_module_columns.connector)) {
            // delete current data from entry
            port->present = false;
            pm_delete_all_data(port);
            // set presence enum
            SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_ABSENT);
            SET_STATIC_STRING(port, connector_status,
                              OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_UNRECOGNIZED);
            VLOG_DBG("module is not present for port: %s", port->instance);
        }
        return 0;
    }

    if (port->present == false || port->retry == true) {
        // haven't read A0 data, yet

        VLOG_DBG("module is present for port: %s", port->instance);

        rc = pm_read_a0(port, (unsigned char *)&a0, offset);

        if (rc != 0) {
            if (retry_count != 0) {
                VLOG_DBG("module serial ID data read failed, retrying: %s",
                         port->instance);
                retry_count--;
                goto retry_read;
            }
            VLOG_WARN("module serial ID data read failed: %s", port->instance);
            pm_delete_all_data(port);
            port->present = true;
            port->retry = true;
            SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_UNKNOWN);
            SET_STATIC_STRING(port, connector_status,
                              OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_UNRECOGNIZED);
            return -1;
        }

        // do checksum validation
        if (sfpp_sum_verify((unsigned char *)&a0) != 0) {
            if (retry_count != 0) {
                VLOG_DBG("module serial ID data failed checksum, retrying: %s", port->instance);
                retry_count--;
                goto retry_read;
            }
            VLOG_WARN("module serial ID data failed checksum: %s", port->instance);
            // mark port as present
            port->present = true;
            port->retry = true;
            // delete all attributes, set "unknown" value
            pm_delete_all_data(port);
            SET_STATIC_STRING(port, connector, OVSREC_INTERFACE_PM_INFO_CONNECTOR_UNKNOWN);
            SET_STATIC_STRING(port, connector_status,
                              OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_UNRECOGNIZED);
            return -1;
        }

        // parse the data into important fields, and set it as pending data
        rc = pm_parse(&a0, port);

        if (rc == 0) {
            // mark port as present
            port->present = true;
            port->retry = false;
        } else {
            port->retry = true;
            // note: in failure case, pm_parse will already have logged
            // an appropriate message.
            VLOG_DBG("pm_parse has failed for port %s", port->instance);
        }
    }

    // OPS_TODO: determine how a2_read_requested will get set
    if (port->a2_read_requested == false || offset != 0) {
        return 0;
    }

retry_read_a2:
    rc = pm_read_a2(port, a2);

    if (rc != 0) {
        if (retry_count != 0) {
            VLOG_DBG("module a2 read failed, retrying: %s", port->instance);
            retry_count--;
            goto retry_read_a2;
        }

        VLOG_WARN("module a2 read failed: %s", port->instance);

        memset(a2, 0xff, sizeof(a2));
    }

    pm_set_a2(port, a2);

    port->a2_read_requested = false;

    return 0;
}

//
// pm_read_port_state: read the port state for a port
//
// input: port structure
//
// output: success only
//
int
pm_read_port_state(pm_port_t *port)
{
    if (NULL == port) {
        return 0;
    }

    pm_read_module_state(port);

    return 0;
}

//
// pm_read_state: read the state of all modules
//
// input: none
//
// output: none
//
int
pm_read_state(void)
{
    struct shash_node *node;

    SHASH_FOR_EACH(node, &ovs_intfs) {
        pm_port_t *port;

        port = (pm_port_t *)node->data;

        pm_read_port_state(port);
    }

    return 0;
}

//
// pm_configure_qsfp: enable/disable qsfp module
//
// input: port structure
//
// output: none
//
void
pm_configure_qsfp(pm_port_t *port)
{
    unsigned char       data = 0x00;
    unsigned int        idx;

#ifdef PLATFORM_SIMULATION
    if (true == port->split) {
        data = 0x00;
        for (idx = 0; idx < MAX_SPLIT_COUNT; idx++) {

            if (false == port->hw_enable_subport[idx]) {
                data |= (1 << idx);
            }
        }
    } else {
        if (false == port->hw_enable) {
            data = 0x0f;
        } else {
            data = 0x00;
        }
    }

    port->port_enable = data;
    return;
#else
    int                 rc;
    i2c_op              op;
    i2c_op              *ops[2];
    const YamlDevice    *device;

    if (false == port->present) {
        return;
    }

    if (false == port->optical) {
        return;
    }

    // OPS_TODO: the split indicator needs to be filled in
    if (true == port->split) {
        data = 0x00;
        for (idx = 0; idx < MAX_SPLIT_COUNT; idx++) {

            if (false == port->hw_enable_subport[idx]) {
                data |= (1 << idx);
            }
        }
    } else {
        if (false == port->hw_enable) {
            data = 0x0f;
        } else {
            data = 0x00;
        }
    }

    // write data to disable/enable QSFP+
    ops[0] = &op;
    ops[1] = NULL;

    device = yaml_find_device(global_yaml_handle, port->subsystem, port->module_device->module_eeprom);

    op.direction = WRITE;
    op.device = port->module_device->module_eeprom;
    op.byte_count = 1;
    op.register_address = QSFP_DISABLE_OFFSET;
    op.data = &data;
    op.set_register = true;
    op.negative_polarity = false;

    rc = i2c_execute(global_yaml_handle, port->subsystem, device, ops);

    if (0 != rc) {
        VLOG_WARN("Failed to write QSFP enable/disable: %s (%d)",
                  port->instance, rc);
    } else {
        VLOG_DBG("Set QSFP enabled/disable: %s to %0X",
                 port->instance, data);
    }

    return;
#endif
}

void
pm_clear_reset(pm_port_t *port)
{
    i2c_bit_op *        reg_op = NULL;
    i2c_op              op;
    i2c_op *            ops[2];
    const YamlDevice    *device;
    unsigned char       byte;
    unsigned short      word;
    unsigned long       dword;
    unsigned long       old;
    int                 rc;

    if (0 == strcmp(port->module_device->connector, CONNECTOR_QSFP_PLUS)) {
        reg_op = port->module_device->module_signals.qsfp.qsfpp_reset;
    } else if (0 == strcmp(port->module_device->connector, CONNECTOR_QSFP28)) {
        reg_op = port->module_device->module_signals.qsfp28.qsfp28p_reset;
    }

    if (NULL == reg_op) {
        VLOG_DBG("port %s does does not have a reset", port->instance);
        return;
    }

    device = yaml_find_device(global_yaml_handle, port->subsystem, reg_op->device);

    if (NULL == device) {
        VLOG_WARN("unable to find device for port disable: %s (%s)",
                  port->instance, reg_op->device);
        return;
    }

    ops[0] = &op;
    ops[1] = NULL;

    op.direction = READ;
    op.device = reg_op->device;
    op.byte_count = reg_op->register_size;
    op.register_address = reg_op->register_address;
    op.set_register = false;
    op.negative_polarity = false;

    switch (op.byte_count) {
        case 1:
            op.data = &byte;
            break;
        case 2:
            op.data = (unsigned char *)&word;
            break;
        case 4:
            op.data = (unsigned char *)&dword;
            break;
        default:
            VLOG_WARN("Invalid register size for port disable %s",
                      port->instance);
            return;
    }

    rc = i2c_execute(global_yaml_handle, port->subsystem, device, ops);

    if (rc != 0) {
        VLOG_WARN("Failed to read module disable register: %s (%d)",
                  port->instance, rc);
        return;
    }

    op.direction = WRITE;
    op.device = reg_op->device;
    op.byte_count = reg_op->register_size;
    op.register_address = reg_op->register_address;
    op.set_register = false;
    op.negative_polarity = false;

    /* Note that op.data still points to the correct address */

    switch (op.byte_count) {
        case 1:
            old = byte;
            if (reg_op->negative_polarity) {
                byte |= reg_op->bit_mask;
            } else {
                byte &= ~(reg_op->bit_mask);
            }
            if (old == byte) {
                VLOG_DBG("port is correctly configured: %s",
                        port->instance);
                return;
            }
            break;
        case 2:
            old = word;
            if (reg_op->negative_polarity) {
                word |= reg_op->bit_mask;
            } else {
                word &= ~(reg_op->bit_mask);
            }
            if (old == word) {
                VLOG_DBG("port is correctly configured: %s",
                         port->instance);
                return;
            }
            break;
        case 4:
            old = dword;
            if (reg_op->negative_polarity) {
                dword |= reg_op->bit_mask;
            } else {
                dword &= ~(reg_op->bit_mask);
            }
            if (old == dword) {
                VLOG_DBG("port is correctly configured: %s",
                         port->instance);
                return;
            }
            break;
        default:
            // already checked
            VLOG_WARN("invalid register size: %s", port->instance);
            return;
    }

    rc = i2c_execute(global_yaml_handle, port->subsystem, device, ops);

    if (rc != 0) {
        VLOG_WARN("Unable to set module disable for port: %s (%d)",
                  port->instance, rc);
        return;
    }
}

//
// pm_configure_port: enable/disable pluggable module
//
// input: port structure
//
// output: none
//
void
pm_configure_port(pm_port_t *port)
{
#ifdef PLATFORM_SIMULATION
    bool                enabled;

    if ((0 == strcmp(port->module_device->connector, CONNECTOR_QSFP_PLUS)) ||
        (0 == strcmp(port->module_device->connector, CONNECTOR_QSFP28))) {
        pm_configure_qsfp(port);
    } else {
        enabled = port->hw_enable;

        if (enabled) {
            port->port_enable = 1;
        } else {
            port->port_enable = 0;
        }
    }

    return;
#else
    int                 rc;
    i2c_bit_op          *reg_op;
    i2c_op              op;
    i2c_op              *ops[2];
    const YamlDevice    *device;
    unsigned char       byte;
    unsigned short      word;
    unsigned long       dword;
    unsigned long       old;
    bool                enabled;

    if (NULL == port) {
        return;
    }

    if ((0 == strcmp(port->module_device->connector, CONNECTOR_QSFP_PLUS)) ||
        (0 == strcmp(port->module_device->connector, CONNECTOR_QSFP28))) {
        pm_configure_qsfp(port);
        return;
    }

    reg_op = port->module_device->module_signals.sfp.sfpp_tx_disable;

    device = yaml_find_device(global_yaml_handle, port->subsystem, reg_op->device);

    if (NULL == device) {
        VLOG_WARN("unable to find device for port disable: %s (%s)",
                  port->instance, reg_op->device);
        return;
    }

    ops[0] = &op;
    ops[1] = NULL;

    op.direction = READ;
    op.device = reg_op->device;
    op.byte_count = reg_op->register_size;
    op.register_address = reg_op->register_address;
    op.set_register = false;
    op.negative_polarity = false;

    switch (op.byte_count) {
        case 1:
            op.data = &byte;
            break;
        case 2:
            op.data = (unsigned char *)&word;
            break;
        case 4:
            op.data = (unsigned char *)&dword;
            break;
        default:
            VLOG_WARN("Invalid register size for port disable %s",
                      port->instance);
            return;
    }

    rc = i2c_execute(global_yaml_handle, port->subsystem, device, ops);

    if (rc != 0) {
        VLOG_WARN("Failed to read module disable register: %s (%d)",
                  port->instance, rc);
        return;
    }

    enabled = port->hw_enable;

    switch (op.byte_count) {
        case 1:
            old = byte;
            if (enabled || reg_op->negative_polarity) {
                byte &= ~(reg_op->bit_mask);
            } else {
                byte |= reg_op->bit_mask;
            }
            if (old == byte) {
                VLOG_DBG("port is correctly configured: %s",
                        port->instance);
                return;
            }
            break;
        case 2:
            old = word;
            if (enabled || reg_op->negative_polarity) {
                word &= ~(reg_op->bit_mask);
            } else {
                word |= reg_op->bit_mask;
            }
            if (old == word) {
                VLOG_DBG("port is correctly configured: %s",
                         port->instance);
                return;
            }
            break;
        case 4:
            old = dword;
            if (enabled || reg_op->negative_polarity) {
                dword &= ~(reg_op->bit_mask);
            } else {
                dword |= reg_op->bit_mask;
            }
            if (old == dword) {
                VLOG_DBG("port is correctly configured: %s",
                         port->instance);
                return;
            }
            break;
        default:
            // already checked
            VLOG_WARN("invalid register size: %s", port->instance);
            return;
    }

    op.direction = WRITE;
    op.device = reg_op->device;
    op.byte_count = reg_op->register_size;
    op.register_address = reg_op->register_address;
    op.set_register = false;
    op.negative_polarity = false;

    rc = i2c_execute(global_yaml_handle, port->subsystem, device, ops);

    if (rc != 0) {
        VLOG_WARN("Unable to set module disable for port: %s (%d)",
                  port->instance, rc);
        return;
    }

    VLOG_DBG("set port %s to %s",
             port->instance, enabled ? "enabled" : "disabled");
#endif
}

#ifdef PLATFORM_SIMULATION
int
pmd_sim_insert(const char *name, const char *file, struct ds *ds)
{
    struct shash_node *node;
    pm_port_t *port;
    FILE *fp;
    unsigned char *data;

    node = shash_find(&ovs_intfs, name);
    if (NULL == node) {
        ds_put_cstr(ds, "No such interface");
        return -1;
    }
    port = (pm_port_t *)node->data;

    if (NULL != port->module_data) {
        free((void *)port->module_data);
        port->module_data = NULL;
    }

    fp = fopen(file, "r");

    if (NULL == fp) {
        ds_put_cstr(ds, "Can't open file");
        return -1;
    }

    data = (unsigned char *)malloc(sizeof(pm_sfp_serial_id_t));

    if (1 != fread(data, sizeof(pm_sfp_serial_id_t), 1, fp)) {
        ds_put_cstr(ds, "Unable to read data");
        free(data);
        fclose(fp);
        return -1;
    }

    fclose(fp);

    port->module_data = data;

    ds_put_cstr(ds, "Pluggable module inserted");

    return 0;
}

int
pmd_sim_remove(const char *name, struct ds *ds)
{
    struct shash_node *node;
    pm_port_t *port;

    node = shash_find(&ovs_intfs, name);
    if (NULL == node) {
        ds_put_cstr(ds, "No such interface");
        return -1;
    }
    port = (pm_port_t *)node->data;

    if (NULL == port->module_data) {
        ds_put_cstr(ds, "Pluggable module not present");
        return -1;
    }

    free((void *)port->module_data);
    port->module_data = NULL;

    ds_put_cstr(ds, "Pluggable module removed");
    return 0;
}
#endif
