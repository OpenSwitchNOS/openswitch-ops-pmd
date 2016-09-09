/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include "pm_interface.h"
#include "plug.h"
#include "pm_dom.h"
#include "config-yaml.h"
#include "openvswitch/vlog.h"

VLOG_DEFINE_THIS_MODULE(pm_i2c_plugin);

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

static const struct pm_subsystem_class_t i2c_sybsystem_class = {
    .pm_subsystem_alloc     = __subsystem_alloc,
    .pm_subsystem_construct = __subsystem_construct,
    .pm_subsystem_destruct  = __subsystem_destruct,
    .pm_subsystem_dealloc   = __subsystem_dealloc,
};

static const struct pm_module_class_t i2c_module_class = {
    .pm_module_alloc      = __module_alloc,
    .pm_module_construct  = __module_construct,
    .pm_module_destruct   = __module_denstruct,
    .pm_module_dealloc    = __module_dealloc,
    .pm_module_detect     = __module_detect,
    .pm_module_info_get   = __module_info_get,
    .pm_module_enable_set = __module_enable_set,
    .pm_module_reset      = __module_reset,
};

/**
 * Get pmd subsystem class.
 */
const struct pm_subsystem_class_t *
pm_subsystem_class_get(void)
{
    return &i2c_sybsystem_class;
}

/**
 * Get pmd module class.
 */
const struct pm_module_class_t *
pm_module_class_get(void)
{
    return &i2c_module_class;
}

/**
 * Initialize ops-pmd platform support plugin.
 */
void pm_plugin_init(void)
{
}

/**
 * Deinitialize ops-pmd platform support plugin.
 * plugin.
 */
void pm_plugin_deinit(void)
{
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
    return xzalloc(sizeof(pm_subsystem_t));
}

static int
__subsystem_construct(pm_subsystem_t *subsystem_)
{
    return 0;
}

static void
__subsystem_destruct(pm_subsystem_t *subsystem)
{
}

static void
__subsystem_dealloc(pm_subsystem_t *subsystem_)
{
    free(subsystem_);
}

static pm_module_t *
__module_alloc(void)
{
    return xzalloc(sizeof(pm_module_t));
}

static int
__module_construct(pm_module_t *module_)
{
    return 0;
}

static void
__module_denstruct(pm_module_t *module_)
{
}

static void
__module_dealloc(pm_module_t *module_)
{
    free(module_);
}

static int
__module_detect(const pm_module_t *module_, bool *present)
{
    *present = pm_get_presence(module_);

    return 0;
}

static int
__module_info_get(pm_module_t *module_)
{
    return pm_read_module_state(module_);
}

static int
__module_enable_set(pm_module_t *module_, bool enable)
{
    pm_configure_module(module_, enable);

    return 0;
}

static int
__module_reset(pm_module_t *module_)
{
    pm_reset_module(module_);

    return 0;
}
