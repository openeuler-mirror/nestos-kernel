/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PREZERO_H
#define _LINUX_PREZERO_H

#include <linux/types.h>

enum prezero_flag {
	PREZERO_BUDDY_FLAG,
	PREZERO_PCP_FLAG,
	PREZERO_MAX_FLAG,
};

#ifdef CONFIG_PAGE_PREZERO
DECLARE_STATIC_KEY_FALSE(prezero_enabled_key);
extern unsigned long prezero_enabled_flag;

static inline bool prezero_enabled(void)
{
	return static_branch_unlikely(&prezero_enabled_key);
}

static inline bool prezero_buddy_enabled(void)
{
	return prezero_enabled() &&
		(prezero_enabled_flag & (1 << PREZERO_BUDDY_FLAG));
}

static inline bool prezero_pcp_enabled(void)
{
	return prezero_enabled() &&
		(prezero_enabled_flag & (1 << PREZERO_PCP_FLAG));
}

#else
static inline bool prezero_enabled(void)
{
	return false;
}

static inline bool prezero_buddy_enabled(void)
{
	return false;
}

static inline bool prezero_pcp_enabled(void)
{
	return false;
}
#endif /* CONFIG_PAGE_PREZERO */

#endif /* _LINUX_PREZERO_H */
