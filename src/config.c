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
 * Source file for pluggable module config-yaml interface functions.
 ***************************************************************************/

#include <string.h>

#include <vswitch-idl.h>
#include <openswitch-idl.h>

#include "config-yaml.h"
#include "pmd.h"

#define YAML_DEVICES    "devices.yaml"
#define YAML_PORTS      "ports.yaml"

VLOG_DEFINE_THIS_MODULE(config);

YamlConfigHandle global_yaml_handle;
extern struct shash ovs_intfs;


void
pm_config_init(void)
{
    global_yaml_handle = yaml_new_config_handle();
}

/*
 * pm_get_yaml_port: find a matching port by instance name
 *
 * input: subsystem name, instance string
 *
 * output: pointer to matching YamlPort object
 */
const YamlPort *
pm_get_yaml_port(const char *subsystem, const char *instance)
{
    size_t          count;
    size_t          idx;
    const YamlPort *yaml_port;

    count = yaml_get_port_count(global_yaml_handle, subsystem);

    // lookup port in YAML data
    for (idx = 0; idx < count; idx++) {
        yaml_port = yaml_get_port(global_yaml_handle, subsystem, idx);

        // find matching instance
        if (strcmp(yaml_port->name, instance) == 0) {
            return(yaml_port);
        }
    }

    return(NULL);
}

/*
 * pm_create_a2_devices: create implied a2 devices for sfpp modules
 *
 * input: none
 *
 * output: none
 */
void
pm_create_a2_devices(void)
{
    const YamlPort      *yaml_port;
    const YamlDevice    *a0_device;
    YamlDevice          a2_device;
    char                *new_name;
    int                 name_len;
    int                 rc;
    struct shash_node   *node;

    SHASH_FOR_EACH(node, &ovs_intfs) {
        pm_port_t *port;

        port = (pm_port_t *)node->data;

        yaml_port = pm_get_yaml_port(port->subsystem, port->instance);

        // if the port isn't pluggable, skip it
        if (false == yaml_port->pluggable) {
            continue;
        }

        // if the port isn't SFPP, skip it
        if (strcmp(yaml_port->connector, SFPP) != 0) {
            continue;
        }

        // find the matching a0 device for the port
        a0_device = yaml_find_device(global_yaml_handle, port->subsystem, yaml_port->module_eeprom);

        if (NULL == a0_device) {
            VLOG_WARN("Unable to find eeprom device for SFP+ port: %s",
                      yaml_port->name);
            continue;
        }

        // construct a new YamlDevice

        // allocate the new name
        name_len = strlen(a0_device->name) + 5;

        // create the A2 name as the old device name with "_A2" appended
        new_name = (char *)malloc(name_len);
        strncpy(new_name, a0_device->name, name_len);
        strncat(new_name, "_dom", name_len);

        // fill in the device data, which mostly matches the a0 device
        a2_device.name = new_name;
        a2_device.bus = a0_device->bus;
        a2_device.dev_type = a0_device->dev_type;
        a2_device.address = PM_SFP_A2_I2C_ADDRESS;
        a2_device.pre = a0_device->pre;
        a2_device.post = a0_device->post;

        // add the new device entry into the yaml data
        rc = yaml_add_device(global_yaml_handle, port->subsystem, new_name, &a2_device);

        if (0 != rc) {
            VLOG_ERR("Unable to add A2 device for SFP+ port: %s",
                     yaml_port->name);
            // continue execution, A2 data will not be available for port
        }

        free(new_name);
    }
}

/*
 * pm_read_yaml_files: read the relevant system files for pluggable modules
 *
 * input: ovsrec_subsystem
 *
 * output: 0 on success, non-zero on failure
 */
int
pm_read_yaml_files(const struct ovsrec_subsystem *subsys)
{
    int rc;

    rc = yaml_add_subsystem(global_yaml_handle, subsys->name, subsys->hw_desc_dir);

    if (0 != rc) {
        VLOG_ERR("yaml_add_subsystem failed");
        goto end;
    }

    // read devices
    rc = yaml_parse_devices(global_yaml_handle, subsys->name);

    if (0 != rc) {
        VLOG_ERR("yaml_parse_devices failed: %s", subsys->hw_desc_dir);
        goto end;
    }

    // read ports
    rc = yaml_parse_ports(global_yaml_handle, subsys->name);

    if (0 != rc) {
        VLOG_ERR("yaml_parse_ports failed: %s", subsys->hw_desc_dir);
        goto end;
    }

    // create extra a2 devices for SFPP ports
    // pm_create_a2_devices();

    // send i2c initialization commands
    yaml_init_devices(global_yaml_handle, subsys->name);

end:
    // could try to clean up yaml handle on error, but the application is
    // going to abort, so there's not much point to it.

    return rc;
}
