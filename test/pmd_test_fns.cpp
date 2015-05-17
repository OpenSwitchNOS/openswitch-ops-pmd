/*
 *  Copyright (C) 2015 Hewlett-Packard Development Company, L.P.
 *  All Rights Reserved.
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

// The pmd unit test code.
//

#include <vector>
#include <string>

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include "dal.h"
#include "dal_schema.h"
#include "config-yaml.h"

#define SLEEP_DELAY 2

#define BASE_SUBSYSTEM "base"

#define MODE_NORMAL 0
#define MODE_SIMULATION 1

#define CREATE_CB_IDX   0
#define MODIFY_CB_IDX   1
#define DELETE_CB_IDX   2
#define CB_COUNT        3

#define SFP_RJ45    0
#define SFP_SX      1
#define SCP_LX      2
#define SFP_CX      3
#define SFP_SR      4
#define SFP_LR      5
#define SFP_LRM     6
#define SFP_DAC     7
#define QSFP_CR4    8
#define QSFP_SR4    9
#define QSFP_LR4    10

unsigned char a0_data[][128] = {
    {   // SFP_RJ45
        0x00
    },
    {   // SFP_SX
        0x00
    },
    {   // SFP_LX
        0x00
    },
    {   // SFP_CX
        0x00
    },
    {   // SFP_SR
        0x03, 0x04, 0x07, 0x10, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x06, 0x67, 0x00, 0x00, 0x00,
        0x08, 0x03, 0x00, 0x1e, 0x41, 0x56, 0x41, 0x47,
        0x4f, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x17, 0x6a,
        0x41, 0x46, 0x42, 0x52, 0x2d, 0x37, 0x30, 0x33,
        0x53, 0x44, 0x5a, 0x2d, 0x48, 0x50, 0x31, 0x20,
        0x47, 0x32, 0x2e, 0x33, 0x03, 0x52, 0x00, 0x1b,
        0x00, 0x1a, 0x00, 0x00, 0x41, 0x41, 0x30, 0x39,
        0x33, 0x38, 0x41, 0x30, 0x44, 0x5a, 0x32, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x30, 0x39, 0x30, 0x39,
        0x32, 0x30, 0x20, 0x20, 0x68, 0xf0, 0x03, 0x20,
        0x00, 0x00, 0x00, 0xd0, 0x00, 0x81, 0x45, 0x01,
        0x34, 0x35, 0x35, 0x38, 0x38, 0x35, 0x2d, 0x30,
        0x30, 0x31, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00,
        0xee, 0xca, 0xf4, 0xa1, 0xe5, 0x75, 0x98, 0x2b
    },
    {   // SFP_LR
        0x00
    },
    {   // SFP_LRM
        0x00
    },
    {   // SFP_DAC
        0x03, 0x04, 0x21, 0x01, 0x00, 0x00, 0x04, 0x41,
        0x84, 0x80, 0xd5, 0x00, 0x67, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x01, 0x00, 0x4d, 0x6f, 0x6c, 0x65,
        0x78, 0x20, 0x49, 0x6e, 0x63, 0x2e, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x09, 0x3a,
        0x37, 0x34, 0x37, 0x36, 0x34, 0x39, 0x31, 0x32,
        0x34, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x41, 0x31, 0x20, 0x20, 0x01, 0x00, 0x00, 0x8e,
        0x00, 0x00, 0x00, 0x00, 0x33, 0x30, 0x32, 0x33,
        0x33, 0x30, 0x30, 0x33, 0x39, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x31, 0x33, 0x30, 0x31,
        0x32, 0x33, 0x20, 0x20, 0x00, 0x00, 0x00, 0x11,
        0x48, 0x33, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x09, 0x09, 0x00, 0x01, 0x06,
        0x06, 0x06, 0x06, 0x01, 0x00, 0x02, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xed
    },
    {   // QSFP_CR4
        0x0d, 0x00, 0x21, 0x08, 0x00, 0x00, 0x00, 0x41,
        0x80, 0x80, 0xd5, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x01, 0xa0, 0x4d, 0x6f, 0x6c, 0x65,
        0x78, 0x20, 0x49, 0x6e, 0x63, 0x2e, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x07, 0x00, 0x09, 0x3a,
        0x31, 0x31, 0x31, 0x30, 0x34, 0x30, 0x39, 0x30,
        0x38, 0x33, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x41, 0x30, 0x00, 0x00, 0x00, 0x00, 0x46, 0xd6,
        0x00, 0x00, 0x00, 0x00, 0x32, 0x31, 0x31, 0x37,
        0x33, 0x30, 0x31, 0x31, 0x33, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x31, 0x32, 0x30, 0x34,
        0x32, 0x36, 0x20, 0x20, 0x00, 0x00, 0x00, 0x12,
        0x48, 0x33, 0x43, 0x20, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0xdf, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    },
    {   // QSFP_SR4
        0x0d, 0x00, 0x0c, 0x04, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x05, 0x67, 0x00, 0x00, 0x96,
        0x00, 0x00, 0x00, 0x00, 0x41, 0x56, 0x41, 0x47,
        0x4f, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x17, 0x6a,
        0x41, 0x46, 0x42, 0x52, 0x2d, 0x37, 0x39, 0x45,
        0x45, 0x50, 0x5a, 0x2d, 0x48, 0x50, 0x31, 0x20,
        0x30, 0x31, 0x42, 0x68, 0x07, 0xd0, 0x46, 0x98,
        0x00, 0x00, 0x0f, 0xde, 0x41, 0x54, 0x41, 0x31,
        0x31, 0x34, 0x31, 0x31, 0x30, 0x30, 0x30, 0x30,
        0x30, 0x31, 0x32, 0x20, 0x31, 0x34, 0x30, 0x33,
        0x31, 0x30, 0x20, 0x20, 0x08, 0x00, 0x00, 0x9f,
        0x00, 0x96, 0x45, 0x00, 0xd9, 0x7c, 0xd0, 0x48,
        0xea, 0x49, 0x1a, 0x57, 0x37, 0x34, 0x37, 0x36,
        0x39, 0x38, 0x2d, 0x42, 0x32, 0x31, 0x48, 0x50,
        0x49, 0x01, 0x01, 0x00, 0x00, 0x00, 0x28, 0x12
    },
    {   // QSFP_LR4
        0x00
    }
};

vector<string> global_sfps;
vector<string> global_qsfps;

int module_cb_count[CB_COUNT];

dal_handle_t dal_handle;

static void
clear_callback_counts(void)
{
    memset(module_cb_count, 0, sizeof(module_cb_count));
}

static void
stop_pmd(int mode)
{
    if (MODE_SIMULATION == mode) {
        system("killall pmd");
    } else {
        system("/usr/local/halon/sbin/halonctl stop pmd");
    }
}

static void
start_pmd(int mode)
{
    if (MODE_SIMULATION == mode) {
        system("/usr/local/halon/bin/pmd simulation");
    } else {
        system("/usr/local/halon/sbin/halonctl start pmd");
    }
}

static void
stop_portd(void)
{
    system("/usr/local/halon/sbin/halonctl stop portd");
}

static void
start_portd(void)
{
    system("/usr/local/halon/sbin/halonctl start portd");
}

void
delete_simulation_files(void)
{
    system("/bin/rm /tmp/pmd_*");
}

void
clear_modules(void)
{
    system("/usr/local/halon/bin/dal_cli -c 'delete /port.*/module' > /dev/null");
}

void
get_enabled(string instance, char mask, char &result)
{
    int fd;
    char enabled;
    string path;

    path = "/tmp/pmd_";
    path += instance;
    path += "_enabled";

    fd = open(path.c_str(), O_RDONLY);

    ASSERT_LE(0, fd);

    ASSERT_EQ(1, read(fd, &enabled, 1));

    close(fd);

    result = (enabled & mask);
}

dal_ret_t
create_cb_fn(dal_element_t *element, void *arg, int caching)
{
    int *counts = (int *)arg;

    counts[CREATE_CB_IDX]++;

    return DAL_RET_SUCCESS(0);
}

dal_ret_t
delete_cb_fn(dal_element_t *element, void *arg, int caching)
{
    int *counts = (int *)arg;

    counts[DELETE_CB_IDX]++;

    return DAL_RET_SUCCESS(0);
}

dal_ret_t
modify_cb_fn(dal_element_t *element, void *arg, void *mods, void *apply_handle)
{
    int *counts = (int *)arg;

    counts[MODIFY_CB_IDX]++;

    return DAL_RET_SUCCESS(0);
}

#define verify_callback_counts(c, m, d) \
    ASSERT_EQ(c, module_cb_count[CREATE_CB_IDX]); \
    ASSERT_EQ(m, module_cb_count[MODIFY_CB_IDX]); \
    ASSERT_EQ(d, module_cb_count[DELETE_CB_IDX]);

void
subscribe_modules(void)
{
    dal_ret_t rc;

    (void)dal_cache(dal_handle,
                    dal_port_module__elem_type_ptr,
                    DAL_ATTR_COUNT(dal_port_module_t),
                    module_cb_count,
                    create_cb_fn,
                    delete_cb_fn,
                    modify_cb_fn,
                    NULL);
}

static dal_ret_t
port_info_cb_fn(
    dal_element_t *dal_element,
    void *arg __attribute__ ((unused)))
{
    char *instance = dal_element->parsed_path->element_component[0].tuid;

    dal_port_info_t *info = (dal_port_info_t *)dal_element->attributes;

    if (NULL == info->pluggable || false == *info->pluggable) {
        return DAL_RET_SUCCESS(0);
    }

    if (NULL != info->connector) {
        switch (*info->connector) {
            case DAL_PORT_INFO_CONNECTOR_SFP_PLUS:
                global_sfps.push_back(instance);
                break;
            case DAL_PORT_INFO_CONNECTOR_QSFP_PLUS:
                global_qsfps.push_back(instance);
                break;
        }
    }

    return(DAL_RET_SUCCESS(0));
}

void
pmd_test_suite_setup(void)
{
    delete_simulation_files();

    // stop pmd
    stop_pmd(MODE_NORMAL);

    // stop portd
    stop_portd();

    // clear module data from DAL
    clear_modules();

    (void)dal_open(0, NULL, &dal_handle);

    // setup callbacks for /port.*/module elements
    subscribe_modules();

    // query /port.*/info elements
    (void)dal_query(dal_handle,
            dal_port_info__elem_type_ptr,
            DAL_ATTR_COUNT(dal_port_info_t),
            NULL,
            NULL,
            port_info_cb_fn,
            NULL);
}

void
pmd_test_suite_tear_down(void)
{
    // clear module data from DAL
    clear_modules();

    // start pmd in normal mode
    start_pmd(MODE_NORMAL);

    // start portd
    start_portd();
}

void
write_file(string name, unsigned char *data, size_t length)
{
    int fd;
    string filename = "/tmp/pmd_";
    filename += name;

    fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);

    ASSERT_LE(0, fd);

    ASSERT_EQ(length, write(fd, data, length));

    close(fd);
}

void
create_module(string name, int type)
{
    unsigned char *data;
    unsigned char present = 1;

    data = a0_data[type];

    write_file(name + "_a0", data, 128);
    write_file(name, &present, 1);
}

void
create_modules(vector<string> &list, size_t count, size_t step, int type)
{
    size_t idx;
    size_t number;

    for (number = 0, idx = 0;
            idx < list.size() && number < count;
            number += 1, idx += step) {
        create_module(list[idx], type);
    }
}

void
pmd_test_clean_startup(void)
{
    size_t idx;
    string name;
    dal_element_t *element;
    dal_port_module_t *module_instance;
    dal_ret_t rc;
    dal_link_t *module_name;

    // create pluggable module simulations
    create_modules(global_sfps, global_sfps.size() / 2, 1, SFP_DAC);
    create_modules(global_qsfps, global_qsfps.size() / 2, 1, QSFP_CR4);

    // start pmd
    start_pmd(MODE_SIMULATION);

    // sleep
    sleep(SLEEP_DELAY);

    verify_callback_counts(global_qsfps.size() + global_sfps.size(), 0, 0);

    // verify that the first N/2 ports is "present", and the second N/2 ports
    // are "absent"
    for (idx = 0; idx < global_sfps.size()/2; idx++) {
        name = "/port.";
        name += global_sfps[idx];
        name += "/module";

        dal_alloc_link(name.c_str(), &module_name);

        rc = dal_lock(dal_handle,
            dal_port_module__elem_type_ptr,
            module_name,
            &element);

        free(module_name);

        ASSERT_EQ(0, rc);

        module_instance = (dal_port_module_t *)element->attributes;

        EXPECT_FALSE(NULL == module_instance);
        EXPECT_FALSE(NULL == module_instance->connector);

        ASSERT_EQ(DAL_PORT_MODULE_CONNECTOR_SFP_DAC, *module_instance->connector);

        dal_unlock(dal_handle, element);
    }

    for (idx = global_sfps.size()/2; idx < global_sfps.size(); idx++) {
        name = "/port.";
        name += global_sfps[idx];
        name += "/module";

        dal_alloc_link(name.c_str(), &module_name);

        rc = dal_lock(dal_handle,
            dal_port_module__elem_type_ptr,
            module_name,
            &element);

        free(module_name);

        ASSERT_EQ(0, rc);

        module_instance = (dal_port_module_t *)element->attributes;

        EXPECT_FALSE(NULL == module_instance);
        EXPECT_FALSE(NULL == module_instance->connector);

        ASSERT_EQ(DAL_PORT_MODULE_CONNECTOR_ABSENT, *module_instance->connector);
        dal_unlock(dal_handle, element);
    }

    // verify that the first N/2 ports are "present", and the second N/2 ports
    // are "absent"
    for (idx = 0; idx < global_qsfps.size()/2; idx++) {
        name = "/port.";
        name += global_qsfps[idx];
        name += "/module";

        dal_alloc_link(name.c_str(), &module_name);

        rc = dal_lock(dal_handle,
            dal_port_module__elem_type_ptr,
            module_name,
            &element);

        free(module_name);

        ASSERT_EQ(0, rc);

        module_instance = (dal_port_module_t *)element->attributes;

        EXPECT_FALSE(NULL == module_instance);
        EXPECT_FALSE(NULL == module_instance->connector);

        ASSERT_EQ(DAL_PORT_MODULE_CONNECTOR_QSFP_CR4, *module_instance->connector);
        dal_unlock(dal_handle, element);
    }

    for (idx = global_qsfps.size()/2; idx < global_qsfps.size(); idx++) {
        name = "/port.";
        name += global_qsfps[idx];
        name += "/module";

        dal_alloc_link(name.c_str(), &module_name);

        rc = dal_lock(dal_handle,
            dal_port_module__elem_type_ptr,
            module_name,
            &element);

        free(module_name);

        ASSERT_EQ(0, rc);

        module_instance = (dal_port_module_t *)element->attributes;

        EXPECT_FALSE(NULL == module_instance);
        EXPECT_FALSE(NULL == module_instance->connector);

        ASSERT_EQ(DAL_PORT_MODULE_CONNECTOR_ABSENT, *module_instance->connector);
        dal_unlock(dal_handle, element);
    }

    // stop pmd
    stop_pmd(MODE_SIMULATION);

    // clear module data from DAL
    clear_modules();

    // delete all simulation files
    delete_simulation_files();

    clear_callback_counts();
}

void
pmd_test_clean_restart(void)
{
    // create pluggable module simulations
    create_modules(global_sfps, global_sfps.size() / 2, 1, SFP_DAC);
    create_modules(global_qsfps, global_qsfps.size() / 2, 1, QSFP_CR4);

    // start pmd
    start_pmd(MODE_SIMULATION);

    // sleep
    sleep(SLEEP_DELAY);

    verify_callback_counts(global_qsfps.size() + global_sfps.size(), 0, 0);

    stop_pmd(MODE_SIMULATION);

    clear_callback_counts();

    start_pmd(MODE_SIMULATION);

    sleep(SLEEP_DELAY);

    verify_callback_counts(0, 0, 0);

    // stop pmd
    stop_pmd(MODE_SIMULATION);

    // clear module data from DAL
    clear_modules();

    // delete all simulation files
    delete_simulation_files();

    clear_callback_counts();
}

void
pmd_test_mismatch_startup(void)
{
    // create bogus port module instance
    system("/usr/local/halon/bin/dal_cli -c 'create /port.NOTREAL/module connector=SFP_RJ45' > /dev/null");

    // start pmd
    start_pmd(MODE_SIMULATION);

    sleep(SLEEP_DELAY);

    // make sure that the bogus module got deleated
    verify_callback_counts(global_sfps.size() + global_qsfps.size() + 1, 0, 1);

    // cleanup
    stop_pmd(MODE_SIMULATION);

    // cleanup
    clear_modules();

    // cleanup
    clear_callback_counts();
}

void
pmd_test_change_startup(void)
{
    int count = 0;

    // create pluggable module simulations
    if (global_sfps.size() != 0) {
        count++;
        create_modules(global_sfps, 1, 1, SFP_SR);
    }
    if (global_qsfps.size() != 0) {
        count++;
        create_modules(global_qsfps, 1, 1, QSFP_CR4);
    }

    // start pmd
    start_pmd(MODE_SIMULATION);

    // sleep
    sleep(SLEEP_DELAY);

    verify_callback_counts(global_qsfps.size() + global_sfps.size(), 0, 0);

    clear_callback_counts();

    // HALON_TODO: verify that the first port is SFP_SR, QSFP_CR4

    stop_pmd(MODE_SIMULATION);

    delete_simulation_files();

    if (global_sfps.size() != 0) {
        create_modules(global_sfps, 1, 1, SFP_DAC);
    }
    if (global_qsfps.size() != 0) {
        create_modules(global_qsfps, 1, 1, QSFP_SR4);
    }

    start_pmd(MODE_SIMULATION);

    sleep(SLEEP_DELAY);

    verify_callback_counts(0, count, 0);

    // HALON_TODO: verify that the first port is SFP_DAC, QSFP_SR4

    stop_pmd(MODE_SIMULATION);

    delete_simulation_files();

    clear_modules();

    clear_callback_counts();
}

void
pmd_test_hot_plug(void)
{
    int count = 0;

    // create pluggable module simulations
    if (global_sfps.size() != 0) {
        count++;
        create_modules(global_sfps, 1, 1, SFP_SR);
    }
    if (global_qsfps.size() != 0) {
        count++;
        create_modules(global_qsfps, 1, 1, QSFP_CR4);
    }


    // start pmd
    start_pmd(MODE_SIMULATION);

    // sleep
    sleep(SLEEP_DELAY);

    verify_callback_counts(global_qsfps.size() + global_sfps.size(), 0, 0);

    clear_callback_counts();

    delete_simulation_files();

    sleep(SLEEP_DELAY);

    verify_callback_counts(0, count, 0);

    clear_callback_counts();

    if (global_sfps.size() != 0) {
        create_modules(global_sfps, 1, 1, SFP_DAC);
    }
    if (global_qsfps.size() != 0) {
        create_modules(global_qsfps, 1, 1, QSFP_SR4);
    }

    sleep(SLEEP_DELAY);

    verify_callback_counts(0, count, 0);

    stop_pmd(MODE_SIMULATION);

    clear_modules();

    clear_callback_counts();

    delete_simulation_files();
}

void
pmd_test_enable_disable(void)
{
    string cmd;
    string instance;
    char enabled;
    char mask = 0;

    if (global_sfps.size() != 0) {
        create_modules(global_sfps, 1, 1, SFP_SR);
        instance = global_sfps[0];
        mask = 0x0f;
    } else if (global_qsfps.size() != 0) {
        create_modules(global_qsfps, 1, 1, QSFP_CR4);
        instance = global_qsfps[0];
        mask = 0x0f;
    } else {
        return;
    }

    // HALON_TODO: test enabling a QSFP split port

    // set port to disabled (in case it isn't currently)
    cmd = "/usr/local/halon/bin/dal_cli -c 'set /port.";
    cmd += instance;
    cmd += "/hw_config enable=False' > /dev/null";

    system(cmd.c_str());

    start_pmd(MODE_SIMULATION);

    sleep(SLEEP_DELAY);

    // note: enabled value is negative logic (bit set means disabled)
    get_enabled(instance, mask, enabled);

    ASSERT_EQ((int)mask, (int)enabled);

    cmd = "/usr/local/halon/bin/dal_cli -c 'set /port.";
    cmd += instance;
    cmd += "/hw_config enable=True' > /dev/null";

    system(cmd.c_str());

    sleep(SLEEP_DELAY);

    // note: enabled value is negative logic (bit set means disabled)
    get_enabled(instance, mask, enabled);

    ASSERT_EQ((int)0, (int)enabled);

    stop_pmd(MODE_SIMULATION);

    clear_modules();

    clear_callback_counts();

    delete_simulation_files();
}

void
pmd_test_invalid_cksum(void)
{
    string instance;
    dal_enum_t connector;
    dal_link_t *module_name;
    string module;
    dal_port_module_t *module_instance;
    dal_ret_t rc;
    dal_element_t *element;

    if (global_sfps.size() != 0) {
        a0_data[SFP_SR][0] = ~a0_data[SFP_SR][0];
        create_modules(global_sfps, 1, 1, SFP_SR);
        instance = global_sfps[0];
        connector = DAL_PORT_MODULE_CONNECTOR_SFP_SR;
    } else if (global_qsfps.size() != 0) {
        a0_data[QSFP_CR4][0] = ~a0_data[QSFP_CR4][0];
        create_modules(global_qsfps, 1, 1, QSFP_CR4);
        instance = global_qsfps[0];
        connector = DAL_PORT_MODULE_CONNECTOR_QSFP_CR4;
    } else {
        return;
    }

    module = "/port.";
    module += instance;
    module += "/module";

    dal_alloc_link(module.c_str(), &module_name);

    start_pmd(MODE_SIMULATION);

    sleep(SLEEP_DELAY);

    verify_callback_counts(global_qsfps.size() + global_sfps.size(), 0, 0);

    rc = dal_lock(dal_handle,
        dal_port_module__elem_type_ptr,
        module_name,
        &element);

    ASSERT_EQ(0, rc);

    module_instance = (dal_port_module_t *)element->attributes;

    EXPECT_FALSE(NULL == module_instance);
    EXPECT_FALSE(NULL == module_instance->connector);

    // verify that connector is "unknown"
    ASSERT_EQ(
        DAL_PORT_MODULE_CONNECTOR_UNKNOWN,
        *module_instance->connector);

    EXPECT_FALSE(NULL == module_instance->connector_status);

    ASSERT_EQ(
        DAL_PORT_MODULE_CONNECTOR_STATUS_UNRECOGNIZED,
        *module_instance->connector_status);

    dal_unlock(dal_handle, element);

    stop_pmd(MODE_SIMULATION);

    clear_callback_counts();

    delete_simulation_files();

    if (global_sfps.size() != 0) {
        a0_data[SFP_SR][0] = ~a0_data[SFP_SR][0];
        create_modules(global_sfps, 1, 1, SFP_SR);
    } else  {
        a0_data[QSFP_CR4][0] = ~a0_data[QSFP_CR4][0];
        create_modules(global_qsfps, 1, 1, QSFP_CR4);
    }

    start_pmd(MODE_SIMULATION);

    sleep(SLEEP_DELAY);

    verify_callback_counts(0, 1, 0);

    rc = dal_lock(dal_handle,
        dal_port_module__elem_type_ptr,
        module_name,
        &element);

    ASSERT_EQ(0, rc);

    // HALON_TODO: verify that connector is as specified
    // HALON_TODO: verify that connector_status is supported
    module_instance = (dal_port_module_t *)element->attributes;

    EXPECT_FALSE(NULL == module_instance);
    EXPECT_FALSE(NULL == module_instance->connector);

    // verify that connector is as specified
    ASSERT_EQ(connector, *module_instance->connector);

    // verify that connector_status is "supported"
    EXPECT_FALSE(NULL == module_instance->connector_status);

    ASSERT_EQ(
        DAL_PORT_MODULE_CONNECTOR_STATUS_SUPPORTED,
        *module_instance->connector_status);

    dal_unlock(dal_handle, element);

    stop_pmd(MODE_SIMULATION);

    clear_modules();

    clear_callback_counts();

    delete_simulation_files();
}

