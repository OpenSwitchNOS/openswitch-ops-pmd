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
 * Source file for pluggable module OVSDB interface functions.
 ***************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dynamic-string.h>
#include <vswitch-idl.h>
#include <openswitch-idl.h>

#include "pmd.h"
#include "pm_dom.h"
#include "pm_plugins.h"
#include "pm_interface.h"

VLOG_DEFINE_THIS_MODULE(ovsdb_access);

#define CHECK_RC(rc, msg, args...) \
    do {                           \
        if (rc) {                  \
            VLOG_ERR(msg, args);   \
            goto exit;             \
        }                          \
    } while (0);


struct ovsdb_idl *idl;

static unsigned int idl_seqno;

#define NAME_IN_DAEMON_TABLE "ops-pmd"

static bool cur_hw_set = false;

struct shash ovs_intfs;
struct shash ovs_subs;

static bool
ovsdb_if_intf_get_hw_enable(const struct ovsrec_interface *intf)
{
    bool hw_enable = false;
    const char *hw_intf_config_enable =
        smap_get(&intf->hw_intf_config, INTERFACE_HW_INTF_CONFIG_MAP_ENABLE);

    if (hw_intf_config_enable) {
        if (!strcmp(INTERFACE_HW_INTF_CONFIG_MAP_ENABLE_TRUE,
                    hw_intf_config_enable)) {
            hw_enable = true;
        }
    }

    return hw_enable;
}

static bool
ovsdb_if_intf_get_pluggable(const struct ovsrec_interface *intf)
{
    bool pluggable = false;
    const char *hw_intf_info_pluggable =
        smap_get(&intf->hw_intf_info, INTERFACE_HW_INTF_INFO_MAP_PLUGGABLE);

    if (hw_intf_info_pluggable) {
        if (!strcmp(INTERFACE_HW_INTF_INFO_MAP_PLUGGABLE_TRUE, hw_intf_info_pluggable)) {
            pluggable = true;
        }
    }

    return pluggable;
}

static void
ovsdb_if_intf_configure(const struct ovsrec_interface *intf)
{
    struct shash_node *node;
    pm_module_t *module;
    bool hw_enable = false;

    node = shash_find(&ovs_intfs, intf->name);

    if (node == NULL) {
        return;
    }

    module = (pm_module_t *)node->data;
    // Check for changes to hw_enable column.
    hw_enable = ovsdb_if_intf_get_hw_enable(intf);
    if (module->hw_enable != hw_enable) {
        VLOG_DBG("Interface %s hw_enable config changed to %d\n",
                 intf->name, hw_enable);
        module->hw_enable = hw_enable;

        // apply any port enable changes
        module->class->pm_module_enable_set(module, hw_enable);
    }
}

static int
ovsdb_if_intf_create(const struct ovsrec_interface *intf, const pm_subsystem_t *subsystem)
{
    int                            rc = 0;
    pm_module_t                    *module = NULL;
    const YamlPort                 *yaml_port = NULL;
    char                           *instance = intf->name;
    const struct pm_module_class_t *module_class = pm_module_class_get(PLATFORM_TYPE_STR);

    // find the yaml port for this instance
    yaml_port = pm_get_yaml_port(subsystem->name, instance);

    if (NULL == yaml_port) {
        VLOG_WARN("unable to find YAML configuration for intf instance %s",
                  instance);
        goto exit;
    }

    // if the module isn't pluggable, then we don't need to process it
    if (false == ovsdb_if_intf_get_pluggable(intf)) {
        VLOG_DBG("module instance %s is not a pluggable module", instance);
        goto exit;
    }

    // create a new data structure to hold the module info data
    module = module_class->pm_module_alloc();
    ovs_assert(module);
    module->class = module_class;

    // fill in the structure
    module->instance = xstrdup(instance);
    memcpy(&module->uuid, &intf->header_.uuid, sizeof(intf->header_.uuid));
    module->subsystem = subsystem;
    module->module_device = yaml_port;
    rc = module->class->pm_module_construct(module);
    if (rc) {
        VLOG_ERR("Failed to construct module %s", module->instance);
        free(module->instance);
        module->class->pm_module_dealloc(module);
    }

    module->hw_enable = ovsdb_if_intf_get_hw_enable(intf);

    // mark it as absent, first, so it will be processed at least once
    module->present = false;

    // add the module to the ovs_intfs shash, with the instance as the key
    shash_add(&ovs_intfs, module->instance, (void *)module);

    VLOG_DBG("pm_module instance (%s) added", instance);

    // apply initial hw_enable state.
    ovsdb_if_intf_configure(intf);

    rc = module->class->pm_module_reset &&
         module->class->pm_module_reset(module);
    CHECK_RC(rc, "Failed to reset module on interface %s subsystem %s",
             module->instance, subsystem->name);

exit:
    return rc;
}

static void
ovsdb_if_subsys_process(const struct ovsrec_subsystem *ovs_sub)
{
    int i;
    struct shash_node *node;
    pm_subsystem_t *subsystem = NULL;
    const struct pm_subsystem_class_t *subsystem_class = pm_subsystem_class_get(PLATFORM_TYPE_STR);

    node = shash_find(&ovs_subs, ovs_sub->name);
    if (node == NULL) {
        // add the subsystem to the shash
        subsystem = subsystem_class->pm_subsystem_alloc();
        ovs_assert(subsystem);
        subsystem->class = subsystem_class;
        subsystem->name = xstrdup(ovs_sub->name);
        memcpy(&subsystem->uuid, &ovs_sub->header_.uuid, sizeof(ovs_sub->header_.uuid));
        // read the needed YAML system definition files for this subsystem
        if (pm_read_yaml_files(ovs_sub) ||
            subsystem->class->pm_subsystem_construct(subsystem)) {
            VLOG_ERR("Failed to construct subsystem");
            free(subsystem->name);
            subsystem->class->pm_subsystem_dealloc(subsystem);
            return;
        }
        shash_add(&ovs_subs, ovs_sub->name, (void *)subsystem);
    } else {
        subsystem = (pm_subsystem_t *)node->data;
    }

    // make sure that all of the interfaces that are present in the subsystem
    // are created

    for (i = 0; i < ovs_sub->n_interfaces; i++) {
        const struct ovsrec_interface *intf;

        intf = ovs_sub->interfaces[i];
        node = shash_find(&ovs_intfs, intf->name);
        if (node == NULL) {
            ovsdb_if_intf_create(intf, subsystem);
        }
    }
}

void
pm_ovsdb_update(void)
{
    struct ovsdb_idl_txn *txn;
    const struct ovsrec_interface *intf;
    const struct ovsrec_daemon *db_daemon;
    pm_module_t   *module = NULL;
    struct shash_node *node;

    txn = ovsdb_idl_txn_create(idl);

    // Loop through all interfaces and update pluggable module
    // info in the database if necessary.
    SHASH_FOR_EACH(node, &ovs_intfs) {
        struct ovs_module_info *module_info;
        struct ovs_module_dom_info *module_dom_info;
        struct smap pm_info;

        module = (pm_module_t *)node->data;

        // if there's no module, it's probably not pluggable
        if (NULL == module) {
            continue;
        }

        intf = ovsrec_interface_get_for_uuid(idl, &module->uuid);
        if (NULL == intf) {
            VLOG_ERR("No DB entry found for hw interface %s\n",
                     module->instance);
            continue;
        }
        if (false == module->module_info_changed) {
            continue;
        }

        module_info = &module->ovs_module_columns;
        // Set pm_info map
        smap_init(&pm_info);
        if (module_info->cable_length) {
            smap_add(&pm_info, "cable_length", module_info->cable_length);
        }
        if (module_info->cable_technology) {
            smap_add(&pm_info, "cable_technology", module_info->cable_technology);
        }
        if (module_info->connector) {
            smap_add(&pm_info, "connector", module_info->connector);
        }
        if (module_info->connector_status) {
            smap_add(&pm_info, "connector_status", module_info->connector_status);
        }
        if (module_info->supported_speeds) {
            smap_add(&pm_info, "supported_speeds", module_info->supported_speeds);
        }
        if (module_info->max_speed) {
            smap_add(&pm_info, "max_speed", module_info->max_speed);
        }
        if (module_info->power_mode) {
            smap_add(&pm_info, "power_mode", module_info->power_mode);
        }
        if (module_info->vendor_name) {
            smap_add(&pm_info, "vendor_name", module_info->vendor_name);
        }
        if (module_info->vendor_oui) {
            smap_add(&pm_info, "vendor_oui", module_info->vendor_oui);
        }
        if (module_info->vendor_part_number) {
            smap_add(&pm_info, "vendor_part_number",
                     module_info->vendor_part_number);
        }
        if (module_info->vendor_revision) {
            smap_add(&pm_info, "vendor_revision", module_info->vendor_revision);
        }
        if (module_info->vendor_serial_number) {
            smap_add(&pm_info, "vendor_serial_number",
                     module_info->vendor_serial_number);
        }

        // Update diagnostics key values
        module_dom_info = &module->ovs_module_dom_columns;

        if (module_dom_info->temperature) {
            smap_add(&pm_info, "temperature", module_dom_info->temperature);
        }
        if (module_dom_info->temperature_high_alarm) {
            smap_add(&pm_info, "temperature_high_alarm", module_dom_info->temperature_high_alarm);
        }
        if (module_dom_info->temperature_low_alarm) {
            smap_add(&pm_info, "temperature_low_alarm", module_dom_info->temperature_low_alarm);
        }
        if (module_dom_info->temperature_high_warning) {
            smap_add(&pm_info, "temperature_high_warning", module_dom_info->temperature_high_warning);
        }
        if (module_dom_info->temperature_low_warning) {
            smap_add(&pm_info, "temperature_low_warning", module_dom_info->temperature_low_warning);
        }
        if (module_dom_info->temperature_high_alarm_threshold) {
            smap_add(&pm_info, "temperature_high_alarm_threshold", module_dom_info->temperature_high_alarm_threshold);
        }
        if (module_dom_info->temperature_low_alarm_threshold) {
            smap_add(&pm_info, "temperature_low_alarm_threshold", module_dom_info->temperature_low_alarm_threshold);
        }
        if (module_dom_info->temperature_high_warning_threshold) {
            smap_add(&pm_info, "temperature_high_warning_threshold", module_dom_info->temperature_high_warning_threshold);
        }
        if (module_dom_info->temperature_low_warning_threshold) {
            smap_add(&pm_info, "temperature_low_warning_threshold", module_dom_info->temperature_low_warning_threshold);
        }

        if (module_dom_info->vcc) {
            smap_add(&pm_info, "vcc", module_dom_info->vcc);
        }
        if (module_dom_info->vcc_high_alarm) {
            smap_add(&pm_info, "vcc_high_alarm", module_dom_info->vcc_high_alarm);
        }
        if (module_dom_info->vcc_low_alarm) {
            smap_add(&pm_info, "vcc_low_alarm", module_dom_info->vcc_low_alarm);
        }
        if (module_dom_info->vcc_high_warning) {
            smap_add(&pm_info, "vcc_high_warning", module_dom_info->vcc_high_warning);
        }
        if (module_dom_info->vcc_low_warning) {
            smap_add(&pm_info, "vcc_low_warning", module_dom_info->vcc_low_warning);
        }
        if (module_dom_info->vcc_high_alarm_threshold) {
            smap_add(&pm_info, "vcc_high_alarm_threshold", module_dom_info->vcc_high_alarm_threshold);
        }
        if (module_dom_info->vcc_low_alarm_threshold) {
            smap_add(&pm_info, "vcc_low_alarm_threshold", module_dom_info->vcc_low_alarm_threshold);
        }
        if (module_dom_info->vcc_high_warning_threshold) {
            smap_add(&pm_info, "vcc_high_warning_threshold", module_dom_info->vcc_high_warning_threshold);
        }
        if (module_dom_info->vcc_low_warning_threshold) {
            smap_add(&pm_info, "vcc_low_warning_threshold", module_dom_info->vcc_low_warning_threshold);
        }

        if (module_dom_info->tx_bias) {
            smap_add(&pm_info, "tx_bias", module_dom_info->tx_bias);
        }
        if (module_dom_info->tx_bias_high_alarm) {
            smap_add(&pm_info, "tx_bias_high_alarm", module_dom_info->tx_bias_high_alarm);
        }
        if (module_dom_info->tx_bias_low_alarm) {
            smap_add(&pm_info, "tx_bias_low_alarm", module_dom_info->tx_bias_low_alarm);
        }
        if (module_dom_info->tx_bias_high_warning) {
            smap_add(&pm_info, "tx_bias_high_warning", module_dom_info->tx_bias_high_warning);
        }
        if (module_dom_info->tx_bias_low_warning) {
            smap_add(&pm_info, "tx_bias_low_warning", module_dom_info->tx_bias_low_warning);
        }
        if (module_dom_info->tx_bias_high_alarm_threshold) {
            smap_add(&pm_info, "tx_bias_high_alarm_threshold", module_dom_info->tx_bias_high_alarm_threshold);
        }
        if (module_dom_info->tx_bias_low_alarm_threshold) {
            smap_add(&pm_info, "tx_bias_low_alarm_threshold", module_dom_info->tx_bias_low_alarm_threshold);
        }
        if (module_dom_info->tx_bias_high_warning_threshold) {
            smap_add(&pm_info, "tx_bias_high_warning_threshold", module_dom_info->tx_bias_high_warning_threshold);
        }
        if (module_dom_info->tx_bias_low_warning_threshold) {
            smap_add(&pm_info, "tx_bias_low_warning_threshold", module_dom_info->tx_bias_low_warning_threshold);
        }

        if (module_dom_info->rx_power) {
            smap_add(&pm_info, "rx_power", module_dom_info->rx_power);
        }
        if (module_dom_info->rx_power_high_alarm) {
            smap_add(&pm_info, "rx_power_high_alarm", module_dom_info->rx_power_high_alarm);
        }
        if (module_dom_info->rx_power_low_alarm) {
            smap_add(&pm_info, "rx_power_low_alarm", module_dom_info->rx_power_low_alarm);
        }
        if (module_dom_info->rx_power_high_warning) {
            smap_add(&pm_info, "rx_power_high_warning", module_dom_info->rx_power_high_warning);
        }
        if (module_dom_info->rx_power_low_warning) {
            smap_add(&pm_info, "rx_power_low_warning", module_dom_info->rx_power_low_warning);
        }
        if (module_dom_info->rx_power_high_alarm_threshold) {
            smap_add(&pm_info, "rx_power_high_alarm_threshold", module_dom_info->rx_power_high_alarm_threshold);
        }
        if (module_dom_info->rx_power_low_alarm_threshold) {
            smap_add(&pm_info, "rx_power_low_alarm_threshold", module_dom_info->rx_power_low_alarm_threshold);
        }
        if (module_dom_info->rx_power_high_warning_threshold) {
            smap_add(&pm_info, "rx_power_high_warning_threshold", module_dom_info->rx_power_high_warning_threshold);
        }
        if (module_dom_info->rx_power_low_warning_threshold) {
            smap_add(&pm_info, "rx_power_low_warning_threshold", module_dom_info->rx_power_low_warning_threshold);
        }

        if (module_dom_info->tx_power) {
            smap_add(&pm_info, "tx_power", module_dom_info->tx_power);
        }
        if (module_dom_info->rx_power_high_alarm) {
            smap_add(&pm_info, "tx_power_high_alarm", module_dom_info->tx_power_high_alarm);
        }
        if (module_dom_info->tx_power_low_alarm) {
            smap_add(&pm_info, "tx_power_low_alarm", module_dom_info->tx_power_low_alarm);
        }
        if (module_dom_info->tx_power_high_warning) {
            smap_add(&pm_info, "tx_power_high_warning", module_dom_info->tx_power_high_warning);
        }
        if (module_dom_info->tx_power_low_warning) {
            smap_add(&pm_info, "tx_power_low_warning", module_dom_info->tx_power_low_warning);
        }
        if (module_dom_info->tx_power_high_alarm_threshold) {
            smap_add(&pm_info, "tx_power_high_alarm_threshold", module_dom_info->tx_power_high_alarm_threshold);
        }
        if (module_dom_info->tx_power_low_alarm_threshold) {
            smap_add(&pm_info, "tx_power_low_alarm_threshold", module_dom_info->tx_power_low_alarm_threshold);
        }
        if (module_dom_info->tx_power_high_warning_threshold) {
            smap_add(&pm_info, "tx_power_high_warning_threshold", module_dom_info->tx_power_high_warning_threshold);
        }
        if (module_dom_info->tx_power_low_warning_threshold) {
            smap_add(&pm_info, "tx_power_low_warning_threshold", module_dom_info->tx_power_low_warning_threshold);
        }



        if (module_dom_info->tx1_bias) {
            smap_add(&pm_info, "tx1_bias", module_dom_info->tx1_bias);
        }
        if (module_dom_info->rx1_power) {
            smap_add(&pm_info, "rx1_power", module_dom_info->rx1_power);
        }

        if (module_dom_info->tx1_bias_high_alarm) {
            smap_add(&pm_info, "tx1_bias_high_alarm", module_dom_info->tx1_bias_high_alarm);
        }
        if (module_dom_info->tx1_bias_low_alarm) {
            smap_add(&pm_info, "tx1_bias_low_alarm", module_dom_info->tx1_bias_low_alarm);
        }
        if (module_dom_info->tx1_bias_high_warning) {
            smap_add(&pm_info, "tx1_bias_high_warning", module_dom_info->tx1_bias_high_warning);
        }
        if (module_dom_info->tx1_bias_low_warning) {
            smap_add(&pm_info, "tx1_bias_low_warning", module_dom_info->tx1_bias_low_warning);
        }
        if (module_dom_info->tx1_bias_high_alarm_threshold) {
            smap_add(&pm_info, "tx1_bias_high_alarm_threshold", module_dom_info->tx1_bias_high_alarm_threshold);
        }
        if (module_dom_info->tx1_bias_low_alarm_threshold) {
            smap_add(&pm_info, "tx1_bias_low_alarm_threshold", module_dom_info->tx1_bias_low_alarm_threshold);
        }
        if (module_dom_info->tx1_bias_high_warning_threshold) {
            smap_add(&pm_info, "tx1_bias_high_warning_threshold", module_dom_info->tx1_bias_high_warning_threshold);
        }
        if (module_dom_info->tx1_bias_low_warning_threshold) {
            smap_add(&pm_info, "tx1_bias_low_warning_threshold", module_dom_info->tx1_bias_low_warning_threshold);
        }

        if (module_dom_info->rx1_power_high_alarm) {
            smap_add(&pm_info, "rx1_power_high_alarm", module_dom_info->rx1_power_high_alarm);
        }
        if (module_dom_info->rx1_power_low_alarm) {
            smap_add(&pm_info, "rx1_power_low_alarm", module_dom_info->rx1_power_low_alarm);
        }
        if (module_dom_info->rx1_power_high_warning) {
            smap_add(&pm_info, "rx1_power_high_warning", module_dom_info->rx1_power_high_warning);
        }
        if (module_dom_info->rx1_power_low_warning) {
            smap_add(&pm_info, "rx1_power_low_warning", module_dom_info->rx1_power_low_warning);
        }
        if (module_dom_info->rx1_power_high_alarm_threshold) {
            smap_add(&pm_info, "rx1_power_high_alarm_threshold", module_dom_info->rx1_power_high_alarm_threshold);
        }
        if (module_dom_info->rx1_power_low_alarm_threshold) {
            smap_add(&pm_info, "rx1_power_low_alarm_threshold", module_dom_info->rx1_power_low_alarm_threshold);
        }
        if (module_dom_info->rx1_power_high_warning_threshold) {
            smap_add(&pm_info, "rx1_power_high_warning_threshold", module_dom_info->rx1_power_high_warning_threshold);
        }
        if (module_dom_info->rx1_power_low_warning_threshold) {
            smap_add(&pm_info, "rx1_power_low_warning_threshold", module_dom_info->rx1_power_low_warning_threshold);
        }



        if (module_dom_info->tx2_bias) {
            smap_add(&pm_info, "tx2_bias", module_dom_info->tx2_bias);
        }
        if (module_dom_info->rx2_power) {
            smap_add(&pm_info, "rx2_power", module_dom_info->rx2_power);
        }

        if (module_dom_info->tx2_bias_high_alarm) {
            smap_add(&pm_info, "tx2_bias_high_alarm", module_dom_info->tx2_bias_high_alarm);
        }
        if (module_dom_info->tx2_bias_low_alarm) {
            smap_add(&pm_info, "tx2_bias_low_alarm", module_dom_info->tx2_bias_low_alarm);
        }
        if (module_dom_info->tx2_bias_high_warning) {
            smap_add(&pm_info, "tx2_bias_high_warning", module_dom_info->tx2_bias_high_warning);
        }
        if (module_dom_info->tx2_bias_low_warning) {
            smap_add(&pm_info, "tx2_bias_low_warning", module_dom_info->tx2_bias_low_warning);
        }
        if (module_dom_info->tx2_bias_high_alarm_threshold) {
            smap_add(&pm_info, "tx2_bias_high_alarm_threshold", module_dom_info->tx2_bias_high_alarm_threshold);
        }
        if (module_dom_info->tx2_bias_low_alarm_threshold) {
            smap_add(&pm_info, "tx2_bias_low_alarm_threshold", module_dom_info->tx2_bias_low_alarm_threshold);
        }
        if (module_dom_info->tx2_bias_high_warning_threshold) {
            smap_add(&pm_info, "tx2_bias_high_warning_threshold", module_dom_info->tx2_bias_high_warning_threshold);
        }
        if (module_dom_info->tx2_bias_low_warning_threshold) {
            smap_add(&pm_info, "tx2_bias_low_warning_threshold", module_dom_info->tx2_bias_low_warning_threshold);
        }

        if (module_dom_info->rx2_power_high_alarm) {
            smap_add(&pm_info, "rx2_power_high_alarm", module_dom_info->rx2_power_high_alarm);
        }
        if (module_dom_info->rx2_power_low_alarm) {
            smap_add(&pm_info, "rx2_power_low_alarm", module_dom_info->rx2_power_low_alarm);
        }
        if (module_dom_info->rx2_power_high_warning) {
            smap_add(&pm_info, "rx2_power_high_warning", module_dom_info->rx2_power_high_warning);
        }
        if (module_dom_info->rx2_power_low_warning) {
            smap_add(&pm_info, "rx2_power_low_warning", module_dom_info->rx2_power_low_warning);
        }
        if (module_dom_info->rx2_power_high_alarm_threshold) {
            smap_add(&pm_info, "rx2_power_high_alarm_threshold", module_dom_info->rx2_power_high_alarm_threshold);
        }
        if (module_dom_info->rx2_power_low_alarm_threshold) {
            smap_add(&pm_info, "rx2_power_low_alarm_threshold", module_dom_info->rx2_power_low_alarm_threshold);
        }
        if (module_dom_info->rx2_power_high_warning_threshold) {
            smap_add(&pm_info, "rx2_power_high_warning_threshold", module_dom_info->rx2_power_high_warning_threshold);
        }
        if (module_dom_info->rx2_power_low_warning_threshold) {
            smap_add(&pm_info, "rx2_power_low_warning_threshold", module_dom_info->rx2_power_low_warning_threshold);
        }



        if (module_dom_info->tx3_bias) {
            smap_add(&pm_info, "tx3_bias", module_dom_info->tx3_bias);
        }
        if (module_dom_info->rx3_power) {
            smap_add(&pm_info, "rx3_power", module_dom_info->rx3_power);
        }

        if (module_dom_info->tx3_bias_high_alarm) {
            smap_add(&pm_info, "tx3_bias_high_alarm", module_dom_info->tx3_bias_high_alarm);
        }
        if (module_dom_info->tx3_bias_low_alarm) {
            smap_add(&pm_info, "tx3_bias_low_alarm", module_dom_info->tx3_bias_low_alarm);
        }
        if (module_dom_info->tx3_bias_high_warning) {
            smap_add(&pm_info, "tx3_bias_high_warning", module_dom_info->tx3_bias_high_warning);
        }
        if (module_dom_info->tx3_bias_low_warning) {
            smap_add(&pm_info, "tx3_bias_low_warning", module_dom_info->tx3_bias_low_warning);
        }
        if (module_dom_info->tx3_bias_high_alarm_threshold) {
            smap_add(&pm_info, "tx3_bias_high_alarm_threshold", module_dom_info->tx3_bias_high_alarm_threshold);
        }
        if (module_dom_info->tx3_bias_low_alarm_threshold) {
            smap_add(&pm_info, "tx3_bias_low_alarm_threshold", module_dom_info->tx3_bias_low_alarm_threshold);
        }
        if (module_dom_info->tx3_bias_high_warning_threshold) {
            smap_add(&pm_info, "tx3_bias_high_warning_threshold", module_dom_info->tx3_bias_high_warning_threshold);
        }
        if (module_dom_info->tx3_bias_low_warning_threshold) {
            smap_add(&pm_info, "tx3_bias_low_warning_threshold", module_dom_info->tx3_bias_low_warning_threshold);
        }

        if (module_dom_info->rx3_power_high_alarm) {
            smap_add(&pm_info, "rx3_power_high_alarm", module_dom_info->rx3_power_high_alarm);
        }
        if (module_dom_info->rx3_power_low_alarm) {
            smap_add(&pm_info, "rx3_power_low_alarm", module_dom_info->rx3_power_low_alarm);
        }
        if (module_dom_info->rx3_power_high_warning) {
            smap_add(&pm_info, "rx3_power_high_warning", module_dom_info->rx3_power_high_warning);
        }
        if (module_dom_info->rx3_power_low_warning) {
            smap_add(&pm_info, "rx3_power_low_warning", module_dom_info->rx3_power_low_warning);
        }
        if (module_dom_info->rx3_power_high_alarm_threshold) {
            smap_add(&pm_info, "rx3_power_high_alarm_threshold", module_dom_info->rx3_power_high_alarm_threshold);
        }
        if (module_dom_info->rx3_power_low_alarm_threshold) {
            smap_add(&pm_info, "rx3_power_low_alarm_threshold", module_dom_info->rx3_power_low_alarm_threshold);
        }
        if (module_dom_info->rx3_power_high_warning_threshold) {
            smap_add(&pm_info, "rx3_power_high_warning_threshold", module_dom_info->rx3_power_high_warning_threshold);
        }
        if (module_dom_info->rx3_power_low_warning_threshold) {
            smap_add(&pm_info, "rx3_power_low_warning_threshold", module_dom_info->rx3_power_low_warning_threshold);
        }



        if (module_dom_info->tx4_bias) {
            smap_add(&pm_info, "tx4_bias", module_dom_info->tx4_bias);
        }
        if (module_dom_info->rx4_power) {
            smap_add(&pm_info, "rx4_power", module_dom_info->rx4_power);
        }

        if (module_dom_info->tx4_bias_high_alarm) {
            smap_add(&pm_info, "tx4_bias_high_alarm", module_dom_info->tx4_bias_high_alarm);
        }
        if (module_dom_info->tx4_bias_low_alarm) {
            smap_add(&pm_info, "tx4_bias_low_alarm", module_dom_info->tx4_bias_low_alarm);
        }
        if (module_dom_info->tx4_bias_high_warning) {
            smap_add(&pm_info, "tx4_bias_high_warning", module_dom_info->tx4_bias_high_warning);
        }
        if (module_dom_info->tx4_bias_low_warning) {
            smap_add(&pm_info, "tx4_bias_low_warning", module_dom_info->tx4_bias_low_warning);
        }
        if (module_dom_info->tx4_bias_high_alarm_threshold) {
            smap_add(&pm_info, "tx4_bias_high_alarm_threshold", module_dom_info->tx4_bias_high_alarm_threshold);
        }
        if (module_dom_info->tx4_bias_low_alarm_threshold) {
            smap_add(&pm_info, "tx4_bias_low_alarm_threshold", module_dom_info->tx4_bias_low_alarm_threshold);
        }
        if (module_dom_info->tx4_bias_high_warning_threshold) {
            smap_add(&pm_info, "tx4_bias_high_warning_threshold", module_dom_info->tx4_bias_high_warning_threshold);
        }
        if (module_dom_info->tx4_bias_low_warning_threshold) {
            smap_add(&pm_info, "tx4_bias_low_warning_threshold", module_dom_info->tx4_bias_low_warning_threshold);
        }

        if (module_dom_info->rx4_power_high_alarm) {
            smap_add(&pm_info, "rx4_power_high_alarm", module_dom_info->rx4_power_high_alarm);
        }
        if (module_dom_info->rx4_power_low_alarm) {
            smap_add(&pm_info, "rx4_power_low_alarm", module_dom_info->rx4_power_low_alarm);
        }
        if (module_dom_info->rx4_power_high_warning) {
            smap_add(&pm_info, "rx4_power_high_warning", module_dom_info->rx4_power_high_warning);
        }
        if (module_dom_info->rx4_power_low_warning) {
            smap_add(&pm_info, "rx4_power_low_warning", module_dom_info->rx4_power_low_warning);
        }
        if (module_dom_info->rx4_power_high_alarm_threshold) {
            smap_add(&pm_info, "rx4_power_high_alarm_threshold", module_dom_info->rx4_power_high_alarm_threshold);
        }
        if (module_dom_info->rx4_power_low_alarm_threshold) {
            smap_add(&pm_info, "rx4_power_low_alarm_threshold", module_dom_info->rx4_power_low_alarm_threshold);
        }
        if (module_dom_info->rx4_power_high_warning_threshold) {
            smap_add(&pm_info, "rx4_power_high_warning_threshold", module_dom_info->rx4_power_high_warning_threshold);
        }
        if (module_dom_info->rx4_power_low_warning_threshold) {
            smap_add(&pm_info, "rx4_power_low_warning_threshold", module_dom_info->rx4_power_low_warning_threshold);
        }

        ovsrec_interface_set_pm_info(intf, &pm_info);
        smap_destroy(&pm_info);

        // Clear port's module info update status
        module->module_info_changed = false;
    }

    if (!cur_hw_set) {
        OVSREC_DAEMON_FOR_EACH(db_daemon, idl) {
            if (strcmp(db_daemon->name, NAME_IN_DAEMON_TABLE) == 0) {
                ovsrec_daemon_set_cur_hw(db_daemon, (int64_t) 1);
                cur_hw_set = true;
                break;
            }
        }
    }

    ovsdb_idl_txn_commit_block(txn);
    ovsdb_idl_txn_destroy(txn);
}

static void
pmd_free_pm_module(pm_module_t *module)
{
    pm_delete_all_data(module);
    free(module->instance);
    module->class->pm_module_dealloc(module);
}

static int
pm_daemon_subscribe(void)
{
    ovsdb_idl_add_table(idl, &ovsrec_table_daemon);
    ovsdb_idl_add_column(idl, &ovsrec_daemon_col_name);
    ovsdb_idl_add_column(idl, &ovsrec_daemon_col_cur_hw);
    ovsdb_idl_omit_alert(idl, &ovsrec_daemon_col_cur_hw);

    return 0;
}

static int
pm_intf_subscribe(void)
{
    // initialize port data hash
    shash_init(&ovs_intfs);

    ovsdb_idl_add_table(idl, &ovsrec_table_interface);
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_name);
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_pm_info);
    ovsdb_idl_omit_alert(idl, &ovsrec_interface_col_pm_info);

    ovsdb_idl_add_column(idl, &ovsrec_interface_col_hw_intf_config);
    ovsdb_idl_add_column(idl, &ovsrec_interface_col_hw_intf_info);

    return 0;
}

static int
pm_subsystem_subscribe(void)
{
    shash_init(&ovs_subs);

    ovsdb_idl_add_table(idl, &ovsrec_table_subsystem);
    ovsdb_idl_add_column(idl, &ovsrec_subsystem_col_name);
    ovsdb_idl_add_column(idl, &ovsrec_subsystem_col_hw_desc_dir);
    ovsdb_idl_add_column(idl, &ovsrec_subsystem_col_interfaces);

    return 0;
}

static void
pmd_configure(struct ovsdb_idl *idl)
{
    const struct ovsrec_subsystem *subsys;
    const struct ovsrec_interface *intf;

    // Process subsystem entries read on startup.
    OVSREC_SUBSYSTEM_FOR_EACH(subsys, idl) {
        VLOG_DBG("Adding Subsystem %s to pmd data store\n", subsys->name);
        ovsdb_if_subsys_process(subsys);
    }

    // Process interface entries read on startup.
    OVSREC_INTERFACE_FOR_EACH(intf, idl) {
        VLOG_DBG("Adding Interface %s to pmd data store\n", intf->name);
        ovsdb_if_intf_configure(intf);
    }

}

void
pmd_reconfigure(struct ovsdb_idl *idl)
{
    const struct ovsrec_subsystem *subsys;
    const struct ovsrec_interface *intf;
    unsigned int new_idl_seqno = ovsdb_idl_get_seqno(idl);
    pm_module_t *module;
    pm_subsystem_t *subsystem;
    struct shash_node *node;
    struct shash_node *next;

    if (new_idl_seqno == idl_seqno){
        return;
    }

    idl_seqno = new_idl_seqno;

    // Process deleted interfaces.
    SHASH_FOR_EACH_SAFE(node, next, &ovs_intfs) {
        const struct ovsrec_interface *tmp_if;
        module = (pm_module_t *) node->data;

        tmp_if = ovsrec_interface_get_for_uuid(idl, &module->uuid);
        if (NULL == tmp_if) {
            VLOG_DBG("Deleted Interface %s\n", module->instance);
            shash_delete(&ovs_intfs, node);
            pmd_free_pm_module(module);
        }
    }

    // Process deleted subsystems
    SHASH_FOR_EACH_SAFE(node, next, &ovs_subs) {
        const struct ovsrec_subsystem *tmp_sub;
        subsystem = (pm_subsystem_t *) node->data;

        tmp_sub = ovsrec_subsystem_get_for_uuid(idl, &subsystem->uuid);
        if (NULL == tmp_sub) {
            VLOG_DBG("Deleted subsystem %s\n", node->name);
            free(subsystem->name);
            shash_delete(&ovs_subs, node);
            subsystem->class->pm_subsystem_dealloc(subsystem);
            // OPS_TODO: remove config subsystem
        }
    }

    // Process added/deleted subsystems.
    OVSREC_SUBSYSTEM_FOR_EACH(subsys, idl) {
        ovsdb_if_subsys_process(subsys);
    }

    // Process modified interfaces.
    OVSREC_INTERFACE_FOR_EACH(intf, idl) {
        ovsdb_if_intf_configure(intf);
    }
}

static int
pm_update_module_state(pm_module_t *module)
{
    int rc = 0;

    if (NULL == module) {
        goto exit;
    }

    rc = module->class->pm_module_detect(module, &module->present);
    CHECK_RC(rc, "Failed to get presense of module for interface %s subsystem %s",
             module->instance, module->subsystem->name);
    if (!module->present) {
        pm_delete_all_data(module);
        SET_STATIC_STRING(module,
                          connector,
                          OVSREC_INTERFACE_PM_INFO_CONNECTOR_ABSENT);
        SET_STATIC_STRING(module,
                          connector_status,
                          OVSREC_INTERFACE_PM_INFO_CONNECTOR_STATUS_UNSUPPORTED);
        goto exit;
    }

    rc = module->class->pm_module_info_get(module);
    CHECK_RC(rc, "Failed to get presense of module for interface %s subsystem %s",
             module->instance, module->subsystem->name);

exit:
    return rc;
}

int
pm_read_state(void)
{
    struct shash_node *node;
    pm_module_t *module;

    SHASH_FOR_EACH(node, &ovs_intfs) {

        module = (pm_module_t *)node->data;
        pm_update_module_state(module);
    }

    return 0;
}

//
// pm_delete_all_data: mark all attributes as deleted
//                     except for connector, which is always present
//
void
pm_delete_all_data(pm_module_t *module)
{
    DELETE(module, connector_status);
    DELETE_FREE(module, supported_speeds);
    DELETE(module, cable_technology);
    DELETE_FREE(module, cable_length);
    DELETE_FREE(module, max_speed);
    DELETE(module, power_mode);
    DELETE_FREE(module, vendor_name);
    DELETE_FREE(module, vendor_oui);
    DELETE_FREE(module, vendor_part_number);
    DELETE_FREE(module, vendor_revision);
    DELETE_FREE(module, vendor_serial_number);
    DELETE_FREE(module, a0);
    DELETE_FREE(module, a2);
    DELETE_FREE(module, a0_uppers);
}

int
pm_ovsdb_if_init(const char *remote)
{
    idl = ovsdb_idl_create(remote, &ovsrec_idl_class, false, true);

    idl_seqno = ovsdb_idl_get_seqno(idl);
    ovsdb_idl_set_lock(idl, "ops_pmd");
    ovsdb_idl_verify_write_only(idl);

    // Subscribe to subsystem table.
    pm_subsystem_subscribe();

    // Subscribe to interface table.
    pm_intf_subscribe();

    // Subscribe to daemon table.
    pm_daemon_subscribe();

    // Process initial configuration.
    pmd_configure(idl);

    return 0;
}

/**********************************************************************/
/*                               DEBUG                                */
/**********************************************************************/
static void
pm_interface_dump(struct ds *ds, pm_module_t *module)
{
    struct ovs_module_info *module_info;

    module_info = &module->ovs_module_columns;
    ds_put_format(ds, "Pluggable info for Interface %s:\n", module->instance);
    if (module_info->cable_length) {
        ds_put_format(ds, "    cable_length           = %s\n",
                      module_info->cable_length);
    }
    if (module_info->cable_technology) {
        ds_put_format(ds, "    cable_technology       = %s\n",
                      module_info->cable_technology);
    }
    if (module_info->connector) {
        ds_put_format(ds, "    connector              = %s\n",
                      module_info->connector);
    }
    if (module_info->connector_status) {
        ds_put_format(ds, "    connector_status       = %s\n",
                      module_info->connector_status);
    }
    if (module_info->supported_speeds) {
        ds_put_format(ds, "    supported_speeds       = %s\n",
                      module_info->supported_speeds);
    }
    if (module_info->max_speed) {
        ds_put_format(ds, "    max_speed              = %s\n",
                      module_info->max_speed);
    }
    if (module_info->power_mode) {
        ds_put_format(ds, "    power_mode             = %s\n",
                      module_info->power_mode);
    }
    if (module_info->vendor_name) {
        ds_put_format(ds, "    vendor_name            = %s\n",
                      module_info->vendor_name);
    }
    if (module_info->vendor_oui) {
        ds_put_format(ds, "    vendor_oui             = %s\n",
                      module_info->vendor_oui);
    }
    if (module_info->vendor_part_number) {
        ds_put_format(ds, "    vendor_part_number     = %s\n",
                      module_info->vendor_part_number);
    }
    if (module_info->vendor_revision) {
        ds_put_format(ds, "    vendor_revision        = %s\n",
                      module_info->vendor_revision);
    }
    if (module_info->vendor_serial_number) {
        ds_put_format(ds, "    vendor_serial_number   = %s\n",
                      module_info->vendor_serial_number);
    }
}

static void
pm_interfaces_dump(struct ds *ds, int argc, const char *argv[])
{
    struct shash_node *sh_node;
    pm_module_t *module = NULL;

    if (argc > 2) {
        sh_node = shash_find(&ovs_intfs, argv[2]);
        if (NULL != sh_node) {
            module = (pm_module_t *)sh_node->data;
            if (module){
                pm_interface_dump(ds, module);
            }
        }
    } else {
        ds_put_cstr(ds, "================ Interfaces ================\n");

        SHASH_FOR_EACH(sh_node, &ovs_intfs) {
            module = (pm_module_t *)sh_node->data;
            if (module){
                pm_interface_dump(ds, module);
            }
        }
    }
}

/**
 * @details
 * Dumps debug data for entire daemon or for individual component specified
 * on command line.
 */
void
pm_debug_dump(struct ds *ds, int argc, const char *argv[])
{
    const char *table_name = NULL;

    if (argc > 1) {
        table_name = argv[1];

        if (!strcmp(table_name, "interface")) {
            pm_interfaces_dump(ds, argc, argv);
        }
    } else {
        pm_interfaces_dump(ds, 0, NULL);
    }
}
