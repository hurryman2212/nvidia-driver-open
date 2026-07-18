/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 Jihong Min. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>

#include "hwmon-hotspot.h"
#include "hwmon-rm.h"

/*
 * GB20x exposes six hotspot readings in this BAR0 block. Bit 30 marks a
 * reading valid, bits 15:3 hold its magnitude in 1/32 degree Celsius, and bit
 * 16 marks a negative value. The hwmon channel reports the hottest valid one.
 */
static const u32 hotspot_registers[] = {
	0x00ad0a90, 0x00ad0a94, 0x00ad0a98, 0x00ad0a9c, 0x00ad0aa0, 0x00ad0aa4,
};

static int read_registers(struct nvhwmon_gpu *gpu, s32 *temp_mc)
{
	NV2080_CTRL_GPU_EXEC_REG_OPS_PARAMS params = { 0 };
	NV2080_CTRL_GPU_REG_OP reg_ops[ARRAY_SIZE(hotspot_registers)] = { 0 };
	bool found = false;
	s32 maximum = S32_MIN;
	u32 status;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(reg_ops); i++) {
		reg_ops[i].regOp = NV2080_CTRL_GPU_REG_OP_READ_32;
		reg_ops[i].regType = NV2080_CTRL_GPU_REG_OP_TYPE_GLOBAL;
		reg_ops[i].regOffset = hotspot_registers[i];
	}

	/* Retain usable readings if only part of the register block is present. */
	params.bNonTransactional = NV_TRUE;
	params.regOpCount = ARRAY_SIZE(reg_ops);
	params.regOps = NV_PTR_TO_NvP64(reg_ops);

	status = nvhwmon_rm_control(gpu->h_client, gpu->h_subdevice,
				    NV2080_CTRL_CMD_GPU_EXEC_REG_OPS, &params,
				    sizeof(params));
	if (status != NVOS_STATUS_SUCCESS)
		return nvhwmon_rm_status_to_errno(status);

	for (i = 0; i < ARRAY_SIZE(reg_ops); i++) {
		s32 temperature;
		u32 raw;

		if (reg_ops[i].regStatus !=
		    NV2080_CTRL_GPU_REG_OP_STATUS_SUCCESS)
			continue;

		raw = reg_ops[i].regValueLo;
		if (!(raw & BIT(30)))
			continue;

		temperature = (raw >> 3) & 0x1fffU;
		if (raw & BIT(16))
			temperature = -temperature;

		maximum =
			max(maximum, DIV_ROUND_CLOSEST(temperature * 1000, 32));
		found = true;
	}

	if (!found)
		return -ENODATA;

	*temp_mc = maximum;
	return 0;
}

static int refresh(struct nvhwmon_gpu *gpu)
{
	struct nvhwmon_hotspot_cache *cache = &gpu->hotspot_cache;
	s32 temp_mc;
	int ret;

	if (cache->expires && time_before(jiffies, cache->expires))
		return cache->valid ? 0 : -ENODATA;

	ret = read_registers(gpu, &temp_mc);
	cache->valid = !ret;
	if (!ret)
		cache->temp_mc = temp_mc;
	/* Cache good samples for one second; retry failed reads after 100 ms. */
	cache->expires = jiffies + msecs_to_jiffies(ret ? 100 : 1000);

	return ret;
}

void nvhwmon_hotspot_probe(struct nvhwmon_gpu *gpu)
{
	NV2080_CTRL_MC_GET_ARCH_INFO_PARAMS params = { 0 };
	u32 status;

	status = nvhwmon_rm_control(gpu->h_client, gpu->h_subdevice,
				    NV2080_CTRL_CMD_MC_GET_ARCH_INFO, &params,
				    sizeof(params));
	if (status != NVOS_STATUS_SUCCESS ||
	    params.architecture != NV2080_CTRL_MC_ARCH_INFO_ARCHITECTURE_GB200)
		return;

	if (refresh(gpu))
		return;

	gpu->hotspot_cache.supported = true;
}

bool nvhwmon_hotspot_has_sensor(const struct nvhwmon_gpu *gpu)
{
	return gpu->hotspot_cache.supported;
}

int nvhwmon_hotspot_read_temp(struct nvhwmon_gpu *gpu, long *val)
{
	int ret;

	if (!nvhwmon_hotspot_has_sensor(gpu))
		return -EINVAL;

	ret = refresh(gpu);
	if (ret)
		return ret;

	*val = gpu->hotspot_cache.temp_mc;
	return 0;
}
