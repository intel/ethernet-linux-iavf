/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2013-2025 Intel Corporation */

#ifndef _IAVF_HELPER_H_
#define _IAVF_HELPER_H_

#include "iavf_alloc.h"

/* prototype */
inline void iavf_destroy_spinlock_d(struct iavf_spinlock *sp);
inline void iavf_acquire_spinlock_d(struct iavf_spinlock *sp);
inline void iavf_release_spinlock_d(struct iavf_spinlock *sp);

/**
 * iavf_init_spinlock_d - OS specific spinlock init for shared code
 * @sp: pointer to a spinlock declared in driver space
 **/
static inline void iavf_init_spinlock_d(struct iavf_spinlock *sp)
{
	mutex_init((struct mutex *)sp);
}

/**
 * iavf_acquire_spinlock_d - OS specific spinlock acquire for shared code
 * @sp: pointer to a spinlock declared in driver space
 **/
inline void iavf_acquire_spinlock_d(struct iavf_spinlock *sp)
{
	mutex_lock((struct mutex *)sp);
}

/**
 * iavf_release_spinlock_d - OS specific spinlock release for shared code
 * @sp: pointer to a spinlock declared in driver space
 **/
inline void iavf_release_spinlock_d(struct iavf_spinlock *sp)
{
	mutex_unlock((struct mutex *)sp);
}

/**
 * iavf_destroy_spinlock_d - OS specific spinlock destroy for shared code
 * @sp: pointer to a spinlock declared in driver space
 **/
inline void iavf_destroy_spinlock_d(struct iavf_spinlock *sp)
{
	mutex_destroy((struct mutex *)sp);
}
#endif /* _IAVF_HELPER_H_ */
