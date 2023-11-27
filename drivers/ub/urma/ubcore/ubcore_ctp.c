// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * Description: ubcore ctp implementation
 * Author: Xu Zhicong
 * Create: 2023-10-12
 * Note:
 * History: 2023-10-12: Create file
 */

#include <linux/slab.h>
#include "ubcore_log.h"
#include "ubcore_hash_table.h"
#include "ubcore_ctp.h"

struct ubcore_ctp *ubcore_create_ctp(struct ubcore_device *dev, struct ubcore_ctp_cfg *cfg)
{
	struct ubcore_ctp *ctp;
	int ret;

	if (dev->ops == NULL || dev->ops->create_ctp == NULL)
		return NULL;

	ctp = dev->ops->create_ctp(dev, cfg, NULL);
	if (ctp == NULL) {
		ubcore_log_err("Failed to create ctp");
		return NULL;
	}
	ctp->ub_dev = dev;
	ctp->ctp_cfg = *cfg;
	atomic_set(&ctp->use_cnt, 1);

	ret = ubcore_hash_table_find_add(&dev->ht[UBCORE_HT_CTP], &ctp->hnode, ctp->ctpn);
	if (ret != 0) {
		(void)dev->ops->destroy_ctp(ctp);
		ctp = NULL;
		ubcore_log_err("Failed to add ctp to the ctp table");
		return ctp;
	}

	ubcore_log_info("Success to create ctp, ctp_idx %u", ctp->ctpn);
	return ctp;
}

int ubcore_destroy_ctp(struct ubcore_ctp *ctp)
{
	struct ubcore_device *dev = ctp->ub_dev;
	uint32_t ctp_idx = ctp->ctpn;
	int ret;

	if (dev->ops == NULL || dev->ops->destroy_ctp == NULL)
		return -EINVAL;

	if (atomic_dec_return(&ctp->use_cnt) > 0) {
		ubcore_log_err("ctp in use");
		return -EBUSY;
	}

	ubcore_hash_table_remove(&dev->ht[UBCORE_HT_CTP], &ctp->hnode);

	ret = dev->ops->destroy_ctp(ctp);
	if (ret != 0) {
		(void)ubcore_hash_table_find_add(&dev->ht[UBCORE_HT_CTP], &ctp->hnode, ctp->ctpn);
		/* inc ctp use cnt? */
		ubcore_log_err("Failed to destroy ctp");
		return ret;
	}

	ubcore_log_info("Success to destroy ctp, ctp_idx %u", ctp_idx);
	return ret;
}

struct ubcore_ctp *ubcore_find_ctp(struct ubcore_device *dev, uint32_t idx)
{
	return ubcore_hash_table_lookup(&dev->ht[UBCORE_HT_CTP], idx, &idx);
}
