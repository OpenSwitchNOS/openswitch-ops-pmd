/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef _PM_INTERFACE_H_
#define _PM_INTERFACE_H_

#include <stdbool.h>
#include "pmd.h"

struct pm_subsystem_class_t {
    /**
     * Allocation of pm subsystem on adding to ovsdb. Implementation should
     * define its own struct that contains parent pm_subsystem_t, and
     * return pointer to parent.
     *
     * @return pointer to allocated subsystem.
     */
    pm_subsystem_t *(*pm_subsystem_alloc)(void);

    /**
     * Construction of pm subsystem on adding to ovsdb. Implementation should
     * initialize all fields in derived structure from pm_subsystem_t.
     *
     * @param[out] subsystem - pointer to subsystem.
     *
     * @return 0     on success.
     * @return errno on failure.
     */
    int (*pm_subsystem_construct)(pm_subsystem_t *subsystem);

    /**
     * Destruction of pm subsystem on removing from ovsdb. Implementation
     * should deinitialize all fields in derived structure from pm_subsystem_t.
     *
     * @param[in] subsystem - pointer to subsystem.
     */
    void (*pm_subsystem_destruct)(pm_subsystem_t *subsystem);

    /**
     * Deallocation of pm subsystem on removing from ovsdb. Implementation
     * should free memory from derived structure.
     *
     * @param[in] subsystem - pointer to subsystem.
     */
    void (*pm_subsystem_dealloc)(pm_subsystem_t *subsystem);
};

struct pm_module_class_t {
    /**
     * Allocation of pm module on adding interface to ovsdb. Implementation should
     * define its own struct that contains parent pm_module_t, and
     * return pointer to parent.
     *
     * @return pointer to allocated module.
     */
    pm_module_t *(*pm_module_alloc)(void);

    /**
     * Construction of pm module on adding to ovsdb. Implementation should
     * initialize all fields in derived structure from pm_module_t.
     *
     * @param[out] module - pointer to module.
     *
     * @return 0     on success.
     * @return errno on failure.
     */
    int (*pm_module_construct)(pm_module_t * module);

    /**
     * Destruction of pm port on removing from ovsdb. Implementation
     * should deinitialize all fields in derived structure from pm_module_t.
     *
     * @param[in] module - pointer to module.
     */
    void (*pm_module_destruct)(pm_module_t * module);

    /**
     * Deallocation of pm module on removing from ovsdb. Implementation
     * should free memory from derived structure.
     *
     * @param[in] module - pointer to module.
     */
    void (*pm_module_dealloc)(pm_module_t * module);

    /**
     * Get presence of pluggable module.
     *
     * @param[in] module   - pointer to module.
     * @param[out] present - boolean value to be filled.
     *
     * @return true if module is present, false otherwise.
     */
    int (*pm_module_detect)(const pm_module_t * module, bool *present);

    /**
     * Get monitoring information of pluggable module.
     *
     * @param[out] module - pointer to module.
     *
     * @return 0 on success or errno value otherwise.
     */
    int (*pm_module_info_get)(pm_module_t *module);

    /**
     * Enabe/disable pluggable module.
     *
     * @param[in] module - pointer to module.
     * @param[in] enable - boolean value that indicates if transceiver should be enabled.
     *
     * @return 0 on success or errno value otherwise
     */
    int (*pm_module_enable_set)(pm_module_t * module, bool enable);

    /**
     * Reset pluggable module.
     *
     * @param[in] module - pointer to module.
     *
     * @return 0 on success or errno value otherwise
     */
    int (*pm_module_reset)(pm_module_t * module);
};

#endif /* _PM_INTERFACE_H_ */
