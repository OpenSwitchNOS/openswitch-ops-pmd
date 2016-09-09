/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <sx/sxd/sxd_dpt.h>
#include <sx/sxd/sxd_access_register.h>

#include "pm_interface.h"
#include "plug.h"
#include "pm_dom.h"
#include "openvswitch/vlog.h"

#define SXD_DEVICE_ID    1
#define DEFAULT_ETH_SWID 0

#define CABLE_I2C_ADDR 0x50
#define MCIA_REG_INFO_PAGE_NUM 0
#define BASE_ID_FIELDS_PAGE_NUM_SFP 0
#define BASE_ID_FIELDS_PAGE_NUM_QSFP 0
#define DIAGNOSTIC_FIELDS_PAGE_NUM_SFP 2
#define DIAGNOSTIC_FIELDS_PAGE_NUM_QSFP 0

#define MCIA_DATA_BATCH_SIZE (sizeof(uint32_t) * 12)
#define BASE_ID_FIELDS_SIZE 128
#define DIAGNOSTIC_FIELDS_SIZE 128

#define BASE_ID_FIELDS_ADDR_SFP 0
#define BASE_ID_FIELDS_ADDR_QSFP 128
#define DIAGNOSTIC_FIELDS_ADDR_SFP 0
#define DIAGNOSTIC_FIELDS_ADDR_QSFP 0

#define SXD_ERROR_LOG_EXIT(status, msg, args...) \
    do {                                         \
        if (status != SXD_STATUS_SUCCESS) {      \
            VLOG_ERR(msg, ##args);               \
            goto exit;                           \
        }                                        \
    } while (0);

VLOG_DEFINE_THIS_MODULE(pm_mlnx_plugin);

struct mlnx_subsystem {
    pm_subsystem_t up;
    sxd_dev_id_t dev_id;
    sxd_swid_t swid;
};

struct mlnx_module {
    pm_module_t up;
    sxd_port_mod_id_t module_id;
};

static pm_subsystem_t *__subsystem_alloc(void);
static int __subsystem_construct(pm_subsystem_t *);
static void __subsystem_destruct(pm_subsystem_t *);
static void __subsystem_dealloc(pm_subsystem_t *);

static pm_module_t * __module_alloc(void);
static int __module_construct(pm_module_t *);
static void __module_denstruct(pm_module_t *);
static void __module_dealloc(pm_module_t *);
static int __module_detect(const pm_module_t *, bool *);
static int __module_info_get(pm_module_t *);
static int __module_enable_set(pm_module_t *, bool);
static int __module_reset(pm_module_t *);

static sxd_status_t __mcia_reg_get(const struct mlnx_module *module,
                                   uint8_t i2c_adrr,
                                   uint8_t page_number,
                                   uint16_t int_address,
                                   uint16_t size,
                                   struct ku_mcia_reg* reg);
static sxd_status_t __mcia_buf_get(const struct mlnx_module *module,
                                   uint8_t i2c_adrr,
                                   uint8_t page_number,
                                   uint16_t int_address,
                                   uint16_t size,
                                   uint8_t *buf);

static const struct pm_subsystem_class_t mlnx_sybsystem_class = {
    .pm_subsystem_alloc     = __subsystem_alloc,
    .pm_subsystem_construct = __subsystem_construct,
    .pm_subsystem_destruct  = __subsystem_destruct,
    .pm_subsystem_dealloc   = __subsystem_dealloc,
};

static const struct pm_module_class_t mlnx_module_class = {
    .pm_module_alloc      = __module_alloc,
    .pm_module_construct  = __module_construct,
    .pm_module_destruct   = __module_denstruct,
    .pm_module_dealloc    = __module_dealloc,
    .pm_module_detect     = __module_detect,
    .pm_module_info_get   = __module_info_get,
    .pm_module_enable_set = __module_enable_set,
    .pm_module_reset      = __module_reset,
};

static inline struct mlnx_subsystem *
mlnx_subsystem_cast(const pm_subsystem_t *subsystem_)
{
    ovs_assert(subsystem_);

    return CONTAINER_OF(subsystem_, struct mlnx_subsystem, up);
}

static inline struct mlnx_module *
mlnx_module_cast(const pm_module_t *module_)
{
    ovs_assert(module_);

    return CONTAINER_OF(module_, struct mlnx_module, up);
}

/**
 * Get pmd subsystem class.
 */
const struct pm_subsystem_class_t *
pm_subsystem_class_get(void)
{
    return &mlnx_sybsystem_class;
}

/**
 * Get pmd module class.
 */
const struct pm_module_class_t *
pm_module_class_get(void)
{
    return &mlnx_module_class;
}

/**
 * Initialize ops-pmd platform support plugin.
 */
void pm_plugin_init(void)
{
    sxd_status_t status = SXD_STATUS_SUCCESS;

    status = sxd_access_reg_init(0, NULL, SX_VERBOSITY_LEVEL_INFO);
    SXD_ERROR_LOG_EXIT(status, "Failed to initialize access register.");

    VLOG_INFO("Connected to mlnx sxd sdk.");

exit:
    return;
}

/**
 * Deinitialize ops-pmd platform support plugin.
 * plugin.
 */
void pm_plugin_deinit(void)
{
    sxd_status_t status = SXD_STATUS_SUCCESS;

    status = sxd_access_reg_deinit();
    SXD_ERROR_LOG_EXIT(status, "Failed to deinitialize access register");

exit:
    return;
}

void
pm_plugin_run(void)
{
}

void
pm_plugin_wait(void)
{
}

static pm_subsystem_t *
__subsystem_alloc(void)
{
    struct mlnx_subsystem *subsystem = xzalloc(sizeof(struct mlnx_subsystem));

    return &subsystem->up;
}

static int
__subsystem_construct(pm_subsystem_t *subsystem_)
{
    struct mlnx_subsystem *subsystem = mlnx_subsystem_cast(subsystem_);

    subsystem->swid = DEFAULT_ETH_SWID;
    subsystem->dev_id = SXD_DEVICE_ID;

    return 0;
}

static void
__subsystem_destruct(pm_subsystem_t *subsystem)
{
}

static void
__subsystem_dealloc(pm_subsystem_t *subsystem_)
{
    struct mlnx_subsystem *subsystem = mlnx_subsystem_cast(subsystem_);

    free(subsystem);
}

static pm_module_t *
__module_alloc(void)
{
    struct mlnx_module *module = xzalloc(sizeof(struct mlnx_module));

    return &module->up;
}

static int
__module_construct(pm_module_t *module_)
{
    struct mlnx_module *module = mlnx_module_cast(module_);

    /* Module id is equal to port label id - 1. */
    module->module_id = atoi(module_->instance) - 1;

    return 0;
}

static void
__module_denstruct(pm_module_t *module_)
{
}

static void
__module_dealloc(pm_module_t *module_)
{
    struct mlnx_module *module = mlnx_module_cast(module_);

    free(module);
}

static int
__module_detect(const pm_module_t *module_, bool *present)
{
    struct mlnx_module *module = mlnx_module_cast(module_);
    struct mlnx_subsystem *subsystem = mlnx_subsystem_cast(module_->subsystem);
    sxd_reg_meta_t reg_meta = { };
    struct ku_pmaos_reg pmaos_reg_data = { };
    sxd_status_t status = SXD_STATUS_SUCCESS;

    memset(&pmaos_reg_data, 0, sizeof(struct ku_pmaos_reg));
    memset(&reg_meta, 0, sizeof(sxd_reg_meta_t));

    reg_meta.access_cmd = SXD_ACCESS_CMD_GET;
    reg_meta.swid = subsystem->swid;
    reg_meta.dev_id = subsystem->dev_id;
    pmaos_reg_data.module = module->module_id;
    status = sxd_access_reg_pmaos(&pmaos_reg_data,
                                  &reg_meta,
                                  1,
                                  NULL,
                                  NULL);
    SXD_ERROR_LOG_EXIT(status, "Failed to get module presence.");

    *present = (pmaos_reg_data.oper_status == SXD_PORT_MODULE_STATUS_PLUGGED);

exit:
    return status == SXD_STATUS_SUCCESS ? 0 : -1;
}

static int
__module_info_get(pm_module_t *module_)
{
    uint32_t dword = 0;
    int cable_identifier_value = 0;
    sxd_status_t status = SXD_STATUS_SUCCESS;
    uint8_t base_id_fields[BASE_ID_FIELDS_SIZE] = { };
    uint8_t diagnostic_fields[DIAGNOSTIC_FIELDS_SIZE] = { };
    struct mlnx_module *module = mlnx_module_cast(module_);
    uint8_t base_page_number = 0, dom_page_number = 0;
    uint16_t base_int_address = 0, dom_int_address = 0;

    status = __mcia_buf_get(module,
                            CABLE_I2C_ADDR,
                            MCIA_REG_INFO_PAGE_NUM,
                            0,
                            sizeof(dword),
                            (uint8_t *)&dword);
    SXD_ERROR_LOG_EXIT(status, "Failed to get module info");

    cable_identifier_value = dword & 0xff;

    switch(cable_identifier_value) {
        case QSFP_CABLE_ID_VALUE:
        case QSFP_PLUS_CABLE_ID_VALUE:
        case QSFP28_CABLE_ID_VALUE:
            base_page_number = BASE_ID_FIELDS_PAGE_NUM_QSFP;
            base_int_address = BASE_ID_FIELDS_ADDR_QSFP;
            dom_page_number = DIAGNOSTIC_FIELDS_PAGE_NUM_QSFP;
            dom_int_address = DIAGNOSTIC_FIELDS_ADDR_QSFP;
            break;
        case SFP_OR_SFP_PLUS_OR_SFP28_CABLE_ID_VALUE:
            base_page_number = BASE_ID_FIELDS_PAGE_NUM_SFP;
            base_int_address = BASE_ID_FIELDS_ADDR_SFP;
            dom_page_number = DIAGNOSTIC_FIELDS_PAGE_NUM_SFP;
            dom_int_address = DIAGNOSTIC_FIELDS_ADDR_SFP;
            break;
        default:
            VLOG_ERR("Cannot get module %d info, unsupported cable type %x",
                     module->module_id, cable_identifier_value);
            status = SXD_STATUS_ERROR;
            goto exit;
    }

    status = __mcia_buf_get(module,
                            CABLE_I2C_ADDR,
                            base_page_number,
                            base_int_address,
                            BASE_ID_FIELDS_SIZE,
                            base_id_fields);
    SXD_ERROR_LOG_EXIT(status, "Failed to read base id fields");
    if (a2_read_available((pm_sfp_serial_id_t *)base_id_fields)) {
        status = __mcia_buf_get(module,
                                CABLE_I2C_ADDR,
                                dom_page_number,
                                dom_int_address,
                                DIAGNOSTIC_FIELDS_SIZE,
                                diagnostic_fields);
        SXD_ERROR_LOG_EXIT(status, "Failed to read diagnostic fields");
    }

    status = pm_parse((pm_sfp_serial_id_t *)base_id_fields,
                      (pm_sfp_dom_t *)diagnostic_fields,
                      module_) ?
                      SXD_STATUS_ERROR :
                      SXD_STATUS_SUCCESS;
    SXD_ERROR_LOG_EXIT(status, "Failed to parse module %d info fields",
                       module->module_id);

exit:
    return status == SXD_STATUS_SUCCESS ? 0 : -1;
}

static int
__module_enable_set(pm_module_t *module_, bool enable)
{
    struct mlnx_module *module = mlnx_module_cast(module_);
    struct mlnx_subsystem *subsystem = mlnx_subsystem_cast(module_->subsystem);
    sxd_reg_meta_t reg_meta = { };
    struct ku_pmaos_reg pmaos_reg_data = { };
    sxd_status_t status = SXD_STATUS_SUCCESS;

    memset(&pmaos_reg_data, 0, sizeof(struct ku_pmaos_reg));
    memset(&reg_meta, 0, sizeof(sxd_reg_meta_t));

    reg_meta.access_cmd = SXD_ACCESS_CMD_SET;
    reg_meta.swid = subsystem->swid;
    reg_meta.dev_id = subsystem->dev_id;
    pmaos_reg_data.module = module->module_id;
    pmaos_reg_data.admin_status = enable;
    pmaos_reg_data.ase = 1;
    status = sxd_access_reg_pmaos(&pmaos_reg_data,
                                  &reg_meta,
                                  1,
                                  NULL,
                                  NULL);
    SXD_ERROR_LOG_EXIT(status, "Failed to get module presence.");

exit:
    return status == SXD_STATUS_SUCCESS ? 0 : -1;
}

static int
__module_reset(pm_module_t *module_)
{
    return __module_enable_set(module_, false) ||
           __module_enable_set(module_, true);
}

static sxd_status_t
__mcia_reg_get(const struct mlnx_module *module,
               uint8_t i2c_adrr,
               uint8_t page_number,
               uint16_t int_address,
               uint16_t size,
               struct ku_mcia_reg* reg)
{
    sxd_reg_meta_t reg_meta = { };
    sxd_status_t status = SXD_STATUS_SUCCESS;
    struct mlnx_subsystem *subsystem = mlnx_subsystem_cast(module->up.subsystem);

    memset(reg, 0, sizeof(struct ku_mcia_reg));
    reg_meta.access_cmd = SXD_ACCESS_CMD_GET;
    reg_meta.dev_id = subsystem->dev_id;
    reg_meta.swid = subsystem->swid;
    reg->i2c_device_address = i2c_adrr;
    reg->page_number = page_number;
    reg->device_address = int_address;
    reg->size = size;
    reg->module = module->module_id;

    status = sxd_access_reg_mcia(reg, &reg_meta, 1, NULL, NULL);
    SXD_ERROR_LOG_EXIT(status, "Failed read mcia register i2c addr %x"
                       "page number %x offset %x size %d",
                       i2c_adrr,
                       page_number,
                       int_address,
                       size);

    reg->dword_0 = ntohl(reg->dword_0);
    reg->dword_1 = ntohl(reg->dword_1);
    reg->dword_2 = ntohl(reg->dword_2);
    reg->dword_3 = ntohl(reg->dword_3);
    reg->dword_4 = ntohl(reg->dword_4);
    reg->dword_5 = ntohl(reg->dword_5);
    reg->dword_6 = ntohl(reg->dword_6);
    reg->dword_7 = ntohl(reg->dword_7);
    reg->dword_8 = ntohl(reg->dword_8);
    reg->dword_9 = ntohl(reg->dword_9);
    reg->dword_10 = ntohl(reg->dword_10);
    reg->dword_11 = ntohl(reg->dword_11);

exit:
    return status;
}

static sxd_status_t
__mcia_buf_get(const struct mlnx_module *module,
               uint8_t i2c_adrr,
               uint8_t page_number,
               uint16_t int_address,
               uint16_t size,
               uint8_t *buf)
{
    struct ku_mcia_reg  reg = { };
    sxd_status_t status = SXD_STATUS_SUCCESS;
    uint16_t tail_size = size % MCIA_DATA_BATCH_SIZE;
    int i = 0, batch_count = size / MCIA_DATA_BATCH_SIZE;


    for (i = 0; i < batch_count; i++) {
        status = __mcia_reg_get(module,
                                i2c_adrr, page_number,
                                int_address + (MCIA_DATA_BATCH_SIZE * i),
                                MCIA_DATA_BATCH_SIZE, &reg);
        SXD_ERROR_LOG_EXIT(status, "Read mcia batch.");
        memcpy(buf + (MCIA_DATA_BATCH_SIZE * i), (uint8_t *)&reg.dword_0,
               MCIA_DATA_BATCH_SIZE);
    }

    if (tail_size == 0) {
        goto exit;
    }

    status = __mcia_reg_get(module, i2c_adrr,
                            page_number,
                            int_address + (MCIA_DATA_BATCH_SIZE * batch_count),
                            tail_size,
                            &reg);
    SXD_ERROR_LOG_EXIT(status, "Read mcia batch.");
    memcpy(buf + (MCIA_DATA_BATCH_SIZE * batch_count), (uint8_t *)&reg.dword_0,
           tail_size);

exit:
    return status;
}
