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
#include <time.h>

#include <vswitch-idl.h>
#include <openswitch-idl.h>

#include "pmd.h"
#include "plug.h"
#include "pm_dom.h"

VLOG_DEFINE_THIS_MODULE(plug);

extern struct shash ovs_intfs;
extern YamlConfigHandle global_yaml_handle;

extern int sfpp_sum_verify(unsigned char *);


/*
 * module Reset
 */
typedef enum
{
  SET_RESET = 0,
  CLEAR_RESET
} clear_reset_t;

#if 0
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
    pm_module_t   *module = NULL;

    SHASH_FOR_EACH(node, &ovs_intfs) {
        module = (pm_module_t *)node->data;

        pm_configure_module(module);
    }

    return 0;
}
#endif

#define MAX_DEVICE_NAME_LEN 1024

bool
pm_get_presence(const pm_module_t *module)
{
#ifdef PLATFORM_SIMULATION
    if (NULL != module->module_data) {
        return true;
    }
    return false;
#else
    // presence detection data
    bool                present;
    uint32_t            result;

    int rc;

    // i2c interface structures
    i2c_bit_op *        reg_op;

    // retry up to 5 times if data is invalid or op fails
    int                 retry_count = 2;

    if (0 == strcmp(module->module_device->connector, CONNECTOR_SFP_PLUS)) {
        reg_op = module->module_device->module_signals.sfp.sfpp_mod_present;
    } else if (0 == strcmp(module->module_device->connector,
                           CONNECTOR_QSFP_PLUS)) {
        reg_op = module->module_device->module_signals.qsfp.qsfpp_mod_present;
    } else if (0 == strcmp(module->module_device->connector,
                           CONNECTOR_QSFP28)) {
        reg_op = module->module_device->module_signals.qsfp28.qsfp28p_mod_present;
    } else {
        VLOG_ERR("module is not pluggable: %s", module->instance);
        return false;
    }
retry_read:

    // execute the operation
    rc = i2c_reg_read(global_yaml_handle, module->subsystem->name, reg_op, &result);

    if (rc != 0) {
        if (retry_count != 0) {
            VLOG_WARN("module presence read failed, retrying: %s",
                      module->instance);
            retry_count--;
            goto retry_read;
        }
        VLOG_ERR("unable to read module presence: %s", module->instance);
        return false;
    }

    // calculate presence
    present = (result != 0);

    return present;
#endif
}

static int
pm_read_a0(pm_module_t *module, unsigned char *data, size_t offset)
{
#ifdef PLATFORM_SIMULATION
    memcpy(data, module->module_data, sizeof(pm_sfp_serial_id_t));
    return 0;
#else
    // device data
    const YamlDevice *device;

    int                 rc;

    // OPS_TODO: Need to read ready bit for QSFP modules (?)

    // get device for module eeprom
    device = yaml_find_device(global_yaml_handle, module->subsystem->name,
                              module->module_device->module_eeprom);

    rc = i2c_data_read(global_yaml_handle, device, module->subsystem->name,
                       offset, sizeof(pm_sfp_serial_id_t), data);

    if (rc != 0) {
        VLOG_ERR("module read failed: %s", module->instance);
        return -1;
    }

    return 0;
#endif
}

static int
pm_read_a2(pm_module_t *module, unsigned char *a2_data)
{
#ifdef PLATFORM_SIMULATION
    return -1;
#else
    // device data
    const YamlDevice    *device;

    int                 rc;
    char                a2_device_name[MAX_DEVICE_NAME_LEN];

    VLOG_DBG("Read A2 address from yaml files.");
    if (strcmp(module->module_device->connector, CONNECTOR_SFP_PLUS) == 0) {
        strncpy(a2_device_name, module->module_device->module_eeprom, MAX_DEVICE_NAME_LEN);
        strncat(a2_device_name, "_dom", MAX_DEVICE_NAME_LEN);
    }
    else {
        strncpy(a2_device_name, module->module_device->module_eeprom, MAX_DEVICE_NAME_LEN);
    }

    // get constructed A2 device
    device = yaml_find_device(global_yaml_handle, module->subsystem->name, a2_device_name);

    rc = i2c_data_read(global_yaml_handle, device, module->subsystem->name, 0,
                       sizeof(pm_sfp_dom_t), a2_data);

    if (rc != 0) {
        VLOG_ERR("module dom read failed: %s", module->instance);
        return -1;
    }

    return 0;
#endif
}

//
// pm_read_module_state: read the presence and id page for a pluggable module
//
// input: module structure
//
// output: success 0, failure !0
//
// OPS_TODO: this code needs to be refactored to simplify and clarify
int
pm_read_module_state(pm_module_t *module)
{
    int             rc;

    // presence detection data
    bool            present;

    // serial id data (SFP+ structure)
    pm_sfp_serial_id_t a0;

    // a2 page is for SFP+, only
    pm_sfp_dom_t a2;
    // unsigned char   a2[PM_SFP_A2_PAGE_SIZE];

    // retry up to 2 times if data is invalid or op fails
    int             retry_count = 2;
    unsigned char   offset;

    memset(&a0, 0, sizeof(a0));
    memset(&a2, 0, sizeof(a2));

    // SFP+ and QSFP serial id data are at different offsets
    // take this opportunity to get the correct presence detection operation

    if (0 == strcmp(module->module_device->connector, CONNECTOR_SFP_PLUS)) {
        offset = SFP_SERIAL_ID_OFFSET;
    } else if ((0 == strcmp(module->module_device->connector,
                            CONNECTOR_QSFP_PLUS)) ||
               (0 == strcmp(module->module_device->connector,
                            CONNECTOR_QSFP28))) {
        offset = QSFP_SERIAL_ID_OFFSET;
    } else {
        VLOG_ERR("module is not pluggable: %s", module->instance);
        return -1;
    }

retry_read:
    present = pm_get_presence(module);

    if (!present) {
        return 0;
    }

    VLOG_DBG("transceiver is present for module: %s", module->instance);

    rc = pm_read_a0(module, (unsigned char *)&a0, offset);

    if (rc != 0) {
        if (retry_count != 0) {
            VLOG_DBG("module serial ID data read failed, resetting and retrying: %s",
                    module->instance);
            pm_reset_module(module);
            retry_count--;
            goto retry_read;
        } else {
            return -1;
        }
    }

    // do checksum validation
    if (sfpp_sum_verify((unsigned char *)&a0) != 0) {
        if (retry_count != 0) {
            VLOG_DBG("module serial ID data failed checksum, resetting and retrying: %s", module->instance);
            pm_reset_module(module);
            retry_count--;
            goto retry_read;
        } else {
            return -1;
        }
    }

    if (!a2_read_available(&a0)) {
        return 0;
    }

retry_read_a2:
    rc = pm_read_a2(module, (unsigned char *)&a2);

    if (rc != 0) {
        if (retry_count != 0) {
            VLOG_DBG("module a2 read failed, retrying: %s", module->instance);
            retry_count--;
            goto retry_read_a2;
        }

        VLOG_WARN("module a2 read failed: %s", module->instance);

        memset(&a2, 0xff, sizeof(a2));
    }

    rc = pm_parse(&a0, &a2, module);
    return rc;
}

#if 0
//
// pm_read_module_state: read the module state for a module
//
// input: module structure
//
// output: success only
//
int
pm_read_module_state(pm_module_t *module)
{
    if (NULL == module) {
        return 0;
    }

    pm_read_module_state(module);

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
        pm_module_t *module;

        module = (pm_module_t *)node->data;

        pm_read_module_state(module);
    }

    return 0;
}
#endif
//
// pm_configure_qsfp: enable/disable qsfp module
//
// input: module structure
//
// output: none
//
void
pm_configure_qsfp(pm_module_t *module, bool enable)
{
    uint8_t             data = 0x00;
    unsigned int        idx;

#ifdef PLATFORM_SIMULATION
    if (true == module->split) {
        data = 0x00;
        for (idx = 0; idx < MAX_SPLIT_COUNT; idx++) {

            if (false == module->hw_enable_subport[idx]) {
                data |= (1 << idx);
            }
        }
    } else {
        if (false == enable) {
            data = 0x0f;
        } else {
            data = 0x00;
        }
    }

    module->module_enable = data;
    return;
#else
    const YamlDevice    *device;

    int                 rc;

    if (false == module->present) {
        return;
    }

    if (false == module->optical) {
        return;
    }

    // OPS_TODO: the split indicator needs to be filled in
    if (true == module->split) {
        data = 0x00;
        for (idx = 0; idx < MAX_SPLIT_COUNT; idx++) {

            if (false == module->hw_enable_subport[idx]) {
                data |= (1 << idx);
            }
        }
    } else {
        if (false == enable) {
            data = 0x0f;
        } else {
            data = 0x00;
        }
    }

    device = yaml_find_device(global_yaml_handle, module->subsystem->name,
                              module->module_device->module_eeprom);

    rc = i2c_data_write(global_yaml_handle, device, module->subsystem->name,
                        QSFP_DISABLE_OFFSET, sizeof(data), &data);

    if (0 != rc) {
        VLOG_WARN("Failed to write QSFP enable/disable: %s (%d)",
                  module->instance, rc);
    } else {
        VLOG_DBG("Set QSFP enabled/disable: %s to %0X",
                 module->instance, data);
    }

    return;
#endif
}


//
// pm_reset: reset/clear reset of pluggable module
//
// input: module structure
//        indication to clear reset
//
// output: none
//

static void
pm_reset(pm_module_t *module, clear_reset_t clear)
{
    i2c_bit_op *        reg_op = NULL;
    uint32_t            data;
    int                 rc;

    if (0 == strcmp(module->module_device->connector, CONNECTOR_QSFP_PLUS)) {
        reg_op = module->module_device->module_signals.qsfp.qsfpp_reset;
    } else if (0 == strcmp(module->module_device->connector, CONNECTOR_QSFP28)) {
        reg_op = module->module_device->module_signals.qsfp28.qsfp28p_reset;
    }

    if (NULL == reg_op) {
        VLOG_DBG("module %s does does not have a reset", module->instance);
        return;
    }

    data = clear ? 0 : 0xffu;
    rc = i2c_reg_write(global_yaml_handle, module->subsystem->name, reg_op, data);

    if (rc != 0) {
        VLOG_WARN("Unable to %s reset for module: %s (%d)",
                  clear ? "clear" : "set", module->instance, rc);
        return;
    }
}

//
// pm_clear_reset: take pluggable module out of reset
//
// input: module structure
//
// output: none
//
#define ONE_MILLISECOND 1000000
#define TEN_MILLISECONDS (10*ONE_MILLISECOND)
void
pm_clear_reset(pm_module_t *module)
{
    struct timespec req = {0,TEN_MILLISECONDS};
    pm_reset(module, CLEAR_RESET);
    nanosleep(&req, NULL);
}


//
// pm_reset_module: reset a pluggable module
//
// input: module structure
//
// output: none
//
void
pm_reset_module(pm_module_t *module)
{
    struct timespec req = {0,ONE_MILLISECOND};
    pm_reset(module, SET_RESET);
    nanosleep(&req, NULL);
    pm_clear_reset(module);
}

//
// pm_configure_module: enable/disable pluggable module
//
// input: module structure
//
// output: none
//
void
pm_configure_module(pm_module_t *module, bool enabled)
{
#ifdef PLATFORM_SIMULATION

    if ((0 == strcmp(module->module_device->connector, CONNECTOR_QSFP_PLUS)) ||
        (0 == strcmp(module->module_device->connector, CONNECTOR_QSFP28))) {
        pm_configure_qsfp(module);
    } else {

        if (enabled) {
            module->module_enable = 1;
        } else {
            module->module_enable = 0;
        }
    }

    return;
#else
    int                 rc;
    uint32_t            data;
    i2c_bit_op          *reg_op;

    if (NULL == module) {
        return;
    }

    if ((0 == strcmp(module->module_device->connector, CONNECTOR_QSFP_PLUS)) ||
        (0 == strcmp(module->module_device->connector, CONNECTOR_QSFP28))) {
        pm_configure_qsfp(module, enabled);
        return;
    }

    reg_op = module->module_device->module_signals.sfp.sfpp_tx_disable;

    data = enabled ? 0: reg_op->bit_mask;

    rc = i2c_reg_write(global_yaml_handle, module->subsystem->name, reg_op, data);

    if (rc != 0) {
        VLOG_WARN("Unable to set module disable for module: %s (%d)",
                  module->instance, rc);
        return;
    }

    VLOG_DBG("set module %s to %s",
             module->instance, enabled ? "enabled" : "disabled");
#endif
}

#ifdef PLATFORM_SIMULATION
int
pmd_sim_insert(const char *name, const char *file, struct ds *ds)
{
    struct shash_node *node;
    pm_module_t *module;
    FILE *fp;
    unsigned char *data;

    node = shash_find(&ovs_intfs, name);
    if (NULL == node) {
        ds_put_cstr(ds, "No such interface");
        return -1;
    }
    module = (pm_module_t *)node->data;

    if (NULL != module->module_data) {
        free((void *)module->module_data);
        module->module_data = NULL;
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

    module->module_data = data;

    ds_put_cstr(ds, "Pluggable module inserted");

    return 0;
}

int
pmd_sim_remove(const char *name, struct ds *ds)
{
    struct shash_node *node;
    pm_module_t *module;

    node = shash_find(&ovs_intfs, name);
    if (NULL == node) {
        ds_put_cstr(ds, "No such interface");
        return -1;
    }
    module = (pm_module_t *)node->data;

    if (NULL == module->module_data) {
        ds_put_cstr(ds, "Pluggable module not present");
        return -1;
    }

    free((void *)module->module_data);
    module->module_data = NULL;

    ds_put_cstr(ds, "Pluggable module removed");
    return 0;
}
#endif
