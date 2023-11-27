/* SPDX-License-Identifier: GPL-2.0 */
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
 * Description: ubcore utp header
 * Author: Ji Lei
 * Create: 2023-08-03
 * Note:
 * History: 2023-08-03: Create file
 */

#ifndef UBCORE_UTP_H
#define UBCORE_UTP_H

#include <urma/ubcore_types.h>

struct ubcore_utp *ubcore_create_utp(struct ubcore_device *dev, struct ubcore_utp_cfg *cfg);
int ubcore_destroy_utp(struct ubcore_utp *utp);
struct ubcore_utp *ubcore_find_utp(struct ubcore_device *dev, uint32_t idx);
#endif
