// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip KSZ8795 switch driver
 *
 * Copyright (C) 2017 Microchip Technology Inc.
 *	Tristram Ha <Tristram.Ha@microchip.com>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/gpio.h>
#include <linux/if_vlan.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_data/microchip-ksz.h>
#include <linux/phy.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/micrel_phy.h>
#include <net/dsa.h>
#include <net/switchdev.h>
#include <linux/phylink.h>

#include "ksz_common.h"
#include "ksz8795_reg.h"
#include "ksz8.h"

static const u8 ksz8795_regs[] = {
	[REG_IND_CTRL_0]		= 0x6E,
	[REG_IND_DATA_8]		= 0x70,
	[REG_IND_DATA_CHECK]		= 0x72,
	[REG_IND_DATA_HI]		= 0x71,
	[REG_IND_DATA_LO]		= 0x75,
	[REG_IND_MIB_CHECK]		= 0x74,
	[REG_IND_BYTE]			= 0xA0,
	[P_FORCE_CTRL]			= 0x0C,
	[P_LINK_STATUS]			= 0x0E,
	[P_LOCAL_CTRL]			= 0x07,
	[P_NEG_RESTART_CTRL]		= 0x0D,
	[P_REMOTE_STATUS]		= 0x08,
	[P_SPEED_STATUS]		= 0x09,
	[S_TAIL_TAG_CTRL]		= 0x0C,
};

static const u32 ksz8795_masks[] = {
	[PORT_802_1P_REMAPPING]		= BIT(7),
	[SW_TAIL_TAG_ENABLE]		= BIT(1),
	[MIB_COUNTER_OVERFLOW]		= BIT(6),
	[MIB_COUNTER_VALID]		= BIT(5),
	[VLAN_TABLE_FID]		= GENMASK(6, 0),
	[VLAN_TABLE_MEMBERSHIP]		= GENMASK(11, 7),
	[VLAN_TABLE_VALID]		= BIT(12),
	[STATIC_MAC_TABLE_VALID]	= BIT(21),
	[STATIC_MAC_TABLE_USE_FID]	= BIT(23),
	[STATIC_MAC_TABLE_FID]		= GENMASK(30, 24),
	[STATIC_MAC_TABLE_OVERRIDE]	= BIT(26),
	[STATIC_MAC_TABLE_FWD_PORTS]	= GENMASK(24, 20),
	[DYNAMIC_MAC_TABLE_ENTRIES_H]	= GENMASK(6, 0),
	[DYNAMIC_MAC_TABLE_MAC_EMPTY]	= BIT(8),
	[DYNAMIC_MAC_TABLE_NOT_READY]	= BIT(7),
	[DYNAMIC_MAC_TABLE_ENTRIES]	= GENMASK(31, 29),
	[DYNAMIC_MAC_TABLE_FID]		= GENMASK(26, 20),
	[DYNAMIC_MAC_TABLE_SRC_PORT]	= GENMASK(26, 24),
	[DYNAMIC_MAC_TABLE_TIMESTAMP]	= GENMASK(28, 27),
};

static const u8 ksz8795_shifts[] = {
	[VLAN_TABLE_MEMBERSHIP_S]	= 7,
	[VLAN_TABLE]			= 16,
	[STATIC_MAC_FWD_PORTS]		= 16,
	[STATIC_MAC_FID]		= 24,
	[DYNAMIC_MAC_ENTRIES_H]		= 3,
	[DYNAMIC_MAC_ENTRIES]		= 29,
	[DYNAMIC_MAC_FID]		= 16,
	[DYNAMIC_MAC_TIMESTAMP]		= 27,
	[DYNAMIC_MAC_SRC_PORT]		= 24,
};

static const u8 ksz8863_regs[] = {
	[REG_IND_CTRL_0]		= 0x79,
	[REG_IND_DATA_8]		= 0x7B,
	[REG_IND_DATA_CHECK]		= 0x7B,
	[REG_IND_DATA_HI]		= 0x7C,
	[REG_IND_DATA_LO]		= 0x80,
	[REG_IND_MIB_CHECK]		= 0x80,
	[P_FORCE_CTRL]			= 0x0C,
	[P_LINK_STATUS]			= 0x0E,
	[P_LOCAL_CTRL]			= 0x0C,
	[P_NEG_RESTART_CTRL]		= 0x0D,
	[P_REMOTE_STATUS]		= 0x0E,
	[P_SPEED_STATUS]		= 0x0F,
	[S_TAIL_TAG_CTRL]		= 0x03,
};

static const u32 ksz8863_masks[] = {
	[PORT_802_1P_REMAPPING]		= BIT(3),
	[SW_TAIL_TAG_ENABLE]		= BIT(6),
	[MIB_COUNTER_OVERFLOW]		= BIT(7),
	[MIB_COUNTER_VALID]		= BIT(6),
	[VLAN_TABLE_FID]		= GENMASK(15, 12),
	[VLAN_TABLE_MEMBERSHIP]		= GENMASK(18, 16),
	[VLAN_TABLE_VALID]		= BIT(19),
	[STATIC_MAC_TABLE_VALID]	= BIT(19),
	[STATIC_MAC_TABLE_USE_FID]	= BIT(21),
	[STATIC_MAC_TABLE_FID]		= GENMASK(29, 26),
	[STATIC_MAC_TABLE_OVERRIDE]	= BIT(20),
	[STATIC_MAC_TABLE_FWD_PORTS]	= GENMASK(18, 16),
	[DYNAMIC_MAC_TABLE_ENTRIES_H]	= GENMASK(5, 0),
	[DYNAMIC_MAC_TABLE_MAC_EMPTY]	= BIT(7),
	[DYNAMIC_MAC_TABLE_NOT_READY]	= BIT(7),
	[DYNAMIC_MAC_TABLE_ENTRIES]	= GENMASK(31, 28),
	[DYNAMIC_MAC_TABLE_FID]		= GENMASK(19, 16),
	[DYNAMIC_MAC_TABLE_SRC_PORT]	= GENMASK(21, 20),
	[DYNAMIC_MAC_TABLE_TIMESTAMP]	= GENMASK(23, 22),
};

static u8 ksz8863_shifts[] = {
	[VLAN_TABLE_MEMBERSHIP_S]	= 16,
	[STATIC_MAC_FWD_PORTS]		= 16,
	[STATIC_MAC_FID]		= 22,
	[DYNAMIC_MAC_ENTRIES_H]		= 3,
	[DYNAMIC_MAC_ENTRIES]		= 24,
	[DYNAMIC_MAC_FID]		= 16,
	[DYNAMIC_MAC_TIMESTAMP]		= 24,
	[DYNAMIC_MAC_SRC_PORT]		= 20,
};

static bool ksz_is_ksz88x3(struct ksz_device *dev)
{
	return dev->chip_id == 0x8830;
}

static void ksz_cfg(struct ksz_device *dev, u32 addr, u8 bits, bool set)
{
	regmap_update_bits(dev->regmap[0], addr, bits, set ? bits : 0);
}

static void ksz_port_cfg(struct ksz_device *dev, int port, int offset, u8 bits,
			 bool set)
{
	regmap_update_bits(dev->regmap[0], PORT_CTRL_ADDR(port, offset),
			   bits, set ? bits : 0);
}

static int ksz8_ind_write8(struct ksz_device *dev, u8 table, u16 addr, u8 data)
{
	struct ksz8 *ksz8 = dev->priv;
	const u8 *regs = ksz8->regs;
	u16 ctrl_addr;
	int ret = 0;

	mutex_lock(&dev->alu_mutex);

	ctrl_addr = IND_ACC_TABLE(table) | addr;
	ret = ksz_write8(dev, regs[REG_IND_BYTE], data);
	if (!ret)
		ret = ksz_write16(dev, regs[REG_IND_CTRL_0], ctrl_addr);

	mutex_unlock(&dev->alu_mutex);

	return ret;
}

static int ksz8_reset_switch(struct ksz_device *dev)
{
	if (ksz_is_ksz88x3(dev)) {
		/* reset switch */
		ksz_cfg(dev, KSZ8863_REG_SW_RESET,
			KSZ8863_GLOBAL_SOFTWARE_RESET | KSZ8863_PCS_RESET, true);
		ksz_cfg(dev, KSZ8863_REG_SW_RESET,
			KSZ8863_GLOBAL_SOFTWARE_RESET | KSZ8863_PCS_RESET, false);
	} else {
		/* reset switch */
		ksz_write8(dev, REG_POWER_MANAGEMENT_1,
			   SW_SOFTWARE_POWER_DOWN << SW_POWER_MANAGEMENT_MODE_S);
		ksz_write8(dev, REG_POWER_MANAGEMENT_1, 0);
	}

	return 0;
}

static void ksz8795_set_prio_queue(struct ksz_device *dev, int port, int queue)
{
	u8 hi, lo;

	/* Number of queues can only be 1, 2, or 4. */
	switch (queue) {
	case 4:
	case 3:
		queue = PORT_QUEUE_SPLIT_4;
		break;
	case 2:
		queue = PORT_QUEUE_SPLIT_2;
		break;
	default:
		queue = PORT_QUEUE_SPLIT_1;
	}
	ksz_pread8(dev, port, REG_PORT_CTRL_0, &lo);
	ksz_pread8(dev, port, P_DROP_TAG_CTRL, &hi);
	lo &= ~PORT_QUEUE_SPLIT_L;
	if (queue & PORT_QUEUE_SPLIT_2)
		lo |= PORT_QUEUE_SPLIT_L;
	hi &= ~PORT_QUEUE_SPLIT_H;
	if (queue & PORT_QUEUE_SPLIT_4)
		hi |= PORT_QUEUE_SPLIT_H;
	ksz_pwrite8(dev, port, REG_PORT_CTRL_0, lo);
	ksz_pwrite8(dev, port, P_DROP_TAG_CTRL, hi);

	/* Default is port based for egress rate limit. */
	if (queue != PORT_QUEUE_SPLIT_1)
		ksz_cfg(dev, REG_SW_CTRL_19, SW_OUT_RATE_LIMIT_QUEUE_BASED,
			true);
}

static void ksz8_r_mib_cnt(struct ksz_device *dev, int port, u16 addr, u64 *cnt)
{
	struct ksz8 *ksz8 = dev->priv;
	const u32 *masks;
	const u8 *regs;
	u16 ctrl_addr;
	u32 data;
	u8 check;
	int loop;

	masks = ksz8->masks;
	regs = ksz8->regs;

	ctrl_addr = addr + dev->info->reg_mib_cnt * port;
	ctrl_addr |= IND_ACC_TABLE(TABLE_MIB | TABLE_READ);

	mutex_lock(&dev->alu_mutex);
	ksz_write16(dev, regs[REG_IND_CTRL_0], ctrl_addr);

	/* It is almost guaranteed to always read the valid bit because of
	 * slow SPI speed.
	 */
	for (loop = 2; loop > 0; loop--) {
		ksz_read8(dev, regs[REG_IND_MIB_CHECK], &check);

		if (check & masks[MIB_COUNTER_VALID]) {
			ksz_read32(dev, regs[REG_IND_DATA_LO], &data);
			if (check & masks[MIB_COUNTER_OVERFLOW])
				*cnt += MIB_COUNTER_VALUE + 1;
			*cnt += data & MIB_COUNTER_VALUE;
			break;
		}
	}
	mutex_unlock(&dev->alu_mutex);
}

static void ksz8795_r_mib_pkt(struct ksz_device *dev, int port, u16 addr,
			      u64 *dropped, u64 *cnt)
{
	struct ksz8 *ksz8 = dev->priv;
	const u32 *masks;
	const u8 *regs;
	u16 ctrl_addr;
	u32 data;
	u8 check;
	int loop;

	masks = ksz8->masks;
	regs = ksz8->regs;

	addr -= dev->info->reg_mib_cnt;
	ctrl_addr = (KSZ8795_MIB_TOTAL_RX_1 - KSZ8795_MIB_TOTAL_RX_0) * port;
	ctrl_addr += addr + KSZ8795_MIB_TOTAL_RX_0;
	ctrl_addr |= IND_ACC_TABLE(TABLE_MIB | TABLE_READ);

	mutex_lock(&dev->alu_mutex);
	ksz_write16(dev, regs[REG_IND_CTRL_0], ctrl_addr);

	/* It is almost guaranteed to always read the valid bit because of
	 * slow SPI speed.
	 */
	for (loop = 2; loop > 0; loop--) {
		ksz_read8(dev, regs[REG_IND_MIB_CHECK], &check);

		if (check & masks[MIB_COUNTER_VALID]) {
			ksz_read32(dev, regs[REG_IND_DATA_LO], &data);
			if (addr < 2) {
				u64 total;

				total = check & MIB_TOTAL_BYTES_H;
				total <<= 32;
				*cnt += total;
				*cnt += data;
				if (check & masks[MIB_COUNTER_OVERFLOW]) {
					total = MIB_TOTAL_BYTES_H + 1;
					total <<= 32;
					*cnt += total;
				}
			} else {
				if (check & masks[MIB_COUNTER_OVERFLOW])
					*cnt += MIB_PACKET_DROPPED + 1;
				*cnt += data & MIB_PACKET_DROPPED;
			}
			break;
		}
	}
	mutex_unlock(&dev->alu_mutex);
}

static void ksz8863_r_mib_pkt(struct ksz_device *dev, int port, u16 addr,
			      u64 *dropped, u64 *cnt)
{
	struct ksz8 *ksz8 = dev->priv;
	const u8 *regs = ksz8->regs;
	u32 *last = (u32 *)dropped;
	u16 ctrl_addr;
	u32 data;
	u32 cur;

	addr -= dev->info->reg_mib_cnt;
	ctrl_addr = addr ? KSZ8863_MIB_PACKET_DROPPED_TX_0 :
			   KSZ8863_MIB_PACKET_DROPPED_RX_0;
	ctrl_addr += port;
	ctrl_addr |= IND_ACC_TABLE(TABLE_MIB | TABLE_READ);

	mutex_lock(&dev->alu_mutex);
	ksz_write16(dev, regs[REG_IND_CTRL_0], ctrl_addr);
	ksz_read32(dev, regs[REG_IND_DATA_LO], &data);
	mutex_unlock(&dev->alu_mutex);

	data &= MIB_PACKET_DROPPED;
	cur = last[addr];
	if (data != cur) {
		last[addr] = data;
		if (data < cur)
			data += MIB_PACKET_DROPPED + 1;
		data -= cur;
		*cnt += data;
	}
}

static void ksz8_r_mib_pkt(struct ksz_device *dev, int port, u16 addr,
			   u64 *dropped, u64 *cnt)
{
	if (ksz_is_ksz88x3(dev))
		ksz8863_r_mib_pkt(dev, port, addr, dropped, cnt);
	else
		ksz8795_r_mib_pkt(dev, port, addr, dropped, cnt);
}

static void ksz8_freeze_mib(struct ksz_device *dev, int port, bool freeze)
{
	if (ksz_is_ksz88x3(dev))
		return;

	/* enable the port for flush/freeze function */
	if (freeze)
		ksz_cfg(dev, REG_SW_CTRL_6, BIT(port), true);
	ksz_cfg(dev, REG_SW_CTRL_6, SW_MIB_COUNTER_FREEZE, freeze);

	/* disable the port after freeze is done */
	if (!freeze)
		ksz_cfg(dev, REG_SW_CTRL_6, BIT(port), false);
}

static void ksz8_port_init_cnt(struct ksz_device *dev, int port)
{
	struct ksz_port_mib *mib = &dev->ports[port].mib;
	u64 *dropped;

	if (!ksz_is_ksz88x3(dev)) {
		/* flush all enabled port MIB counters */
		ksz_cfg(dev, REG_SW_CTRL_6, BIT(port), true);
		ksz_cfg(dev, REG_SW_CTRL_6, SW_MIB_COUNTER_FLUSH, true);
		ksz_cfg(dev, REG_SW_CTRL_6, BIT(port), false);
	}

	mib->cnt_ptr = 0;

	/* Some ports may not have MIB counters before SWITCH_COUNTER_NUM. */
	while (mib->cnt_ptr < dev->info->reg_mib_cnt) {
		dev->dev_ops->r_mib_cnt(dev, port, mib->cnt_ptr,
					&mib->counters[mib->cnt_ptr]);
		++mib->cnt_ptr;
	}

	/* last one in storage */
	dropped = &mib->counters[dev->info->mib_cnt];

	/* Some ports may not have MIB counters after SWITCH_COUNTER_NUM. */
	while (mib->cnt_ptr < dev->info->mib_cnt) {
		dev->dev_ops->r_mib_pkt(dev, port, mib->cnt_ptr,
					dropped, &mib->counters[mib->cnt_ptr]);
		++mib->cnt_ptr;
	}
}

static void ksz8_r_table(struct ksz_device *dev, int table, u16 addr, u64 *data)
{
	struct ksz8 *ksz8 = dev->priv;
	const u8 *regs = ksz8->regs;
	u16 ctrl_addr;

	ctrl_addr = IND_ACC_TABLE(table | TABLE_READ) | addr;

	mutex_lock(&dev->alu_mutex);
	ksz_write16(dev, regs[REG_IND_CTRL_0], ctrl_addr);
	ksz_read64(dev, regs[REG_IND_DATA_HI], data);
	mutex_unlock(&dev->alu_mutex);
}

static void ksz8_w_table(struct ksz_device *dev, int table, u16 addr, u64 data)
{
	struct ksz8 *ksz8 = dev->priv;
	const u8 *regs = ksz8->regs;
	u16 ctrl_addr;

	ctrl_addr = IND_ACC_TABLE(table) | addr;

	mutex_lock(&dev->alu_mutex);
	ksz_write64(dev, regs[REG_IND_DATA_HI], data);
	ksz_write16(dev, regs[REG_IND_CTRL_0], ctrl_addr);
	mutex_unlock(&dev->alu_mutex);
}

static int ksz8_valid_dyn_entry(struct ksz_device *dev, u8 *data)
{
	struct ksz8 *ksz8 = dev->priv;
	int timeout = 100;
	const u32 *masks;
	const u8 *regs;

	masks = ksz8->masks;
	regs = ksz8->regs;

	do {
		ksz_read8(dev, regs[REG_IND_DATA_CHECK], data);
		timeout--;
	} while ((*data & masks[DYNAMIC_MAC_TABLE_NOT_READY]) && timeout);

	/* Entry is not ready for accessing. */
	if (*data & masks[DYNAMIC_MAC_TABLE_NOT_READY]) {
		return -EAGAIN;
	/* Entry is ready for accessing. */
	} else {
		ksz_read8(dev, regs[REG_IND_DATA_8], data);

		/* There is no valid entry in the table. */
		if (*data & masks[DYNAMIC_MAC_TABLE_MAC_EMPTY])
			return -ENXIO;
	}
	return 0;
}

static int ksz8_r_dyn_mac_table(struct ksz_device *dev, u16 addr,
				u8 *mac_addr, u8 *fid, u8 *src_port,
				u8 *timestamp, u16 *entries)
{
	struct ksz8 *ksz8 = dev->priv;
	u32 data_hi, data_lo;
	const u8 *shifts;
	const u32 *masks;
	const u8 *regs;
	u16 ctrl_addr;
	u8 data;
	int rc;

	shifts = ksz8->shifts;
	masks = ksz8->masks;
	regs = ksz8->regs;

	ctrl_addr = IND_ACC_TABLE(TABLE_DYNAMIC_MAC | TABLE_READ) | addr;

	mutex_lock(&dev->alu_mutex);
	ksz_write16(dev, regs[REG_IND_CTRL_0], ctrl_addr);

	rc = ksz8_valid_dyn_entry(dev, &data);
	if (rc == -EAGAIN) {
		if (addr == 0)
			*entries = 0;
	} else if (rc == -ENXIO) {
		*entries = 0;
	/* At least one valid entry in the table. */
	} else {
		u64 buf = 0;
		int cnt;

		ksz_read64(dev, regs[REG_IND_DATA_HI], &buf);
		data_hi = (u32)(buf >> 32);
		data_lo = (u32)buf;

		/* Check out how many valid entry in the table. */
		cnt = data & masks[DYNAMIC_MAC_TABLE_ENTRIES_H];
		cnt <<= shifts[DYNAMIC_MAC_ENTRIES_H];
		cnt |= (data_hi & masks[DYNAMIC_MAC_TABLE_ENTRIES]) >>
			shifts[DYNAMIC_MAC_ENTRIES];
		*entries = cnt + 1;

		*fid = (data_hi & masks[DYNAMIC_MAC_TABLE_FID]) >>
			shifts[DYNAMIC_MAC_FID];
		*src_port = (data_hi & masks[DYNAMIC_MAC_TABLE_SRC_PORT]) >>
			shifts[DYNAMIC_MAC_SRC_PORT];
		*timestamp = (data_hi & masks[DYNAMIC_MAC_TABLE_TIMESTAMP]) >>
			shifts[DYNAMIC_MAC_TIMESTAMP];

		mac_addr[5] = (u8)data_lo;
		mac_addr[4] = (u8)(data_lo >> 8);
		mac_addr[3] = (u8)(data_lo >> 16);
		mac_addr[2] = (u8)(data_lo >> 24);

		mac_addr[1] = (u8)data_hi;
		mac_addr[0] = (u8)(data_hi >> 8);
		rc = 0;
	}
	mutex_unlock(&dev->alu_mutex);

	return rc;
}

static int ksz8_r_sta_mac_table(struct ksz_device *dev, u16 addr,
				struct alu_struct *alu)
{
	struct ksz8 *ksz8 = dev->priv;
	u32 data_hi, data_lo;
	const u8 *shifts;
	const u32 *masks;
	u64 data;

	shifts = ksz8->shifts;
	masks = ksz8->masks;

	ksz8_r_table(dev, TABLE_STATIC_MAC, addr, &data);
	data_hi = data >> 32;
	data_lo = (u32)data;
	if (data_hi & (masks[STATIC_MAC_TABLE_VALID] |
			masks[STATIC_MAC_TABLE_OVERRIDE])) {
		alu->mac[5] = (u8)data_lo;
		alu->mac[4] = (u8)(data_lo >> 8);
		alu->mac[3] = (u8)(data_lo >> 16);
		alu->mac[2] = (u8)(data_lo >> 24);
		alu->mac[1] = (u8)data_hi;
		alu->mac[0] = (u8)(data_hi >> 8);
		alu->port_forward =
			(data_hi & masks[STATIC_MAC_TABLE_FWD_PORTS]) >>
				shifts[STATIC_MAC_FWD_PORTS];
		alu->is_override =
			(data_hi & masks[STATIC_MAC_TABLE_OVERRIDE]) ? 1 : 0;
		data_hi >>= 1;
		alu->is_static = true;
		alu->is_use_fid =
			(data_hi & masks[STATIC_MAC_TABLE_USE_FID]) ? 1 : 0;
		alu->fid = (data_hi & masks[STATIC_MAC_TABLE_FID]) >>
				shifts[STATIC_MAC_FID];
		return 0;
	}
	return -ENXIO;
}

static void ksz8_w_sta_mac_table(struct ksz_device *dev, u16 addr,
				 struct alu_struct *alu)
{
	struct ksz8 *ksz8 = dev->priv;
	u32 data_hi, data_lo;
	const u8 *shifts;
	const u32 *masks;
	u64 data;

	shifts = ksz8->shifts;
	masks = ksz8->masks;

	data_lo = ((u32)alu->mac[2] << 24) |
		((u32)alu->mac[3] << 16) |
		((u32)alu->mac[4] << 8) | alu->mac[5];
	data_hi = ((u32)alu->mac[0] << 8) | alu->mac[1];
	data_hi |= (u32)alu->port_forward << shifts[STATIC_MAC_FWD_PORTS];

	if (alu->is_override)
		data_hi |= masks[STATIC_MAC_TABLE_OVERRIDE];
	if (alu->is_use_fid) {
		data_hi |= masks[STATIC_MAC_TABLE_USE_FID];
		data_hi |= (u32)alu->fid << shifts[STATIC_MAC_FID];
	}
	if (alu->is_static)
		data_hi |= masks[STATIC_MAC_TABLE_VALID];
	else
		data_hi &= ~masks[STATIC_MAC_TABLE_OVERRIDE];

	data = (u64)data_hi << 32 | data_lo;
	ksz8_w_table(dev, TABLE_STATIC_MAC, addr, data);
}

static void ksz8_from_vlan(struct ksz_device *dev, u32 vlan, u8 *fid,
			   u8 *member, u8 *valid)
{
	struct ksz8 *ksz8 = dev->priv;
	const u8 *shifts;
	const u32 *masks;

	shifts = ksz8->shifts;
	masks = ksz8->masks;

	*fid = vlan & masks[VLAN_TABLE_FID];
	*member = (vlan & masks[VLAN_TABLE_MEMBERSHIP]) >>
			shifts[VLAN_TABLE_MEMBERSHIP_S];
	*valid = !!(vlan & masks[VLAN_TABLE_VALID]);
}

static void ksz8_to_vlan(struct ksz_device *dev, u8 fid, u8 member, u8 valid,
			 u16 *vlan)
{
	struct ksz8 *ksz8 = dev->priv;
	const u8 *shifts;
	const u32 *masks;

	shifts = ksz8->shifts;
	masks = ksz8->masks;

	*vlan = fid;
	*vlan |= (u16)member << shifts[VLAN_TABLE_MEMBERSHIP_S];
	if (valid)
		*vlan |= masks[VLAN_TABLE_VALID];
}

static void ksz8_r_vlan_entries(struct ksz_device *dev, u16 addr)
{
	struct ksz8 *ksz8 = dev->priv;
	const u8 *shifts;
	u64 data;
	int i;

	shifts = ksz8->shifts;

	ksz8_r_table(dev, TABLE_VLAN, addr, &data);
	addr *= 4;
	for (i = 0; i < 4; i++) {
		dev->vlan_cache[addr + i].table[0] = (u16)data;
		data >>= shifts[VLAN_TABLE];
	}
}

static void ksz8_r_vlan_table(struct ksz_device *dev, u16 vid, u16 *vlan)
{
	int index;
	u16 *data;
	u16 addr;
	u64 buf;

	data = (u16 *)&buf;
	addr = vid / 4;
	index = vid & 3;
	ksz8_r_table(dev, TABLE_VLAN, addr, &buf);
	*vlan = data[index];
}

static void ksz8_w_vlan_table(struct ksz_device *dev, u16 vid, u16 vlan)
{
	int index;
	u16 *data;
	u16 addr;
	u64 buf;

	data = (u16 *)&buf;
	addr = vid / 4;
	index = vid & 3;
	ksz8_r_table(dev, TABLE_VLAN, addr, &buf);
	data[index] = vlan;
	dev->vlan_cache[vid].table[0] = vlan;
	ksz8_w_table(dev, TABLE_VLAN, addr, buf);
}

static void ksz8_r_phy(struct ksz_device *dev, u16 phy, u16 reg, u16 *val)
{
	struct ksz8 *ksz8 = dev->priv;
	u8 restart, speed, ctrl, link;
	const u8 *regs = ksz8->regs;
	int processed = true;
	u8 val1, val2;
	u16 data = 0;
	u8 p = phy;

	switch (reg) {
	case MII_BMCR:
		ksz_pread8(dev, p, regs[P_NEG_RESTART_CTRL], &restart);
		ksz_pread8(dev, p, regs[P_SPEED_STATUS], &speed);
		ksz_pread8(dev, p, regs[P_FORCE_CTRL], &ctrl);
		if (restart & PORT_PHY_LOOPBACK)
			data |= BMCR_LOOPBACK;
		if (ctrl & PORT_FORCE_100_MBIT)
			data |= BMCR_SPEED100;
		if (ksz_is_ksz88x3(dev)) {
			if ((ctrl & PORT_AUTO_NEG_ENABLE))
				data |= BMCR_ANENABLE;
		} else {
			if (!(ctrl & PORT_AUTO_NEG_DISABLE))
				data |= BMCR_ANENABLE;
		}
		if (restart & PORT_POWER_DOWN)
			data |= BMCR_PDOWN;
		if (restart & PORT_AUTO_NEG_RESTART)
			data |= BMCR_ANRESTART;
		if (ctrl & PORT_FORCE_FULL_DUPLEX)
			data |= BMCR_FULLDPLX;
		if (speed & PORT_HP_MDIX)
			data |= KSZ886X_BMCR_HP_MDIX;
		if (restart & PORT_FORCE_MDIX)
			data |= KSZ886X_BMCR_FORCE_MDI;
		if (restart & PORT_AUTO_MDIX_DISABLE)
			data |= KSZ886X_BMCR_DISABLE_AUTO_MDIX;
		if (restart & PORT_TX_DISABLE)
			data |= KSZ886X_BMCR_DISABLE_TRANSMIT;
		if (restart & PORT_LED_OFF)
			data |= KSZ886X_BMCR_DISABLE_LED;
		break;
	case MII_BMSR:
		ksz_pread8(dev, p, regs[P_LINK_STATUS], &link);
		data = BMSR_100FULL |
		       BMSR_100HALF |
		       BMSR_10FULL |
		       BMSR_10HALF |
		       BMSR_ANEGCAPABLE;
		if (link & PORT_AUTO_NEG_COMPLETE)
			data |= BMSR_ANEGCOMPLETE;
		if (link & PORT_STAT_LINK_GOOD)
			data |= BMSR_LSTATUS;
		break;
	case MII_PHYSID1:
		data = KSZ8795_ID_HI;
		break;
	case MII_PHYSID2:
		if (ksz_is_ksz88x3(dev))
			data = KSZ8863_ID_LO;
		else
			data = KSZ8795_ID_LO;
		break;
	case MII_ADVERTISE:
		ksz_pread8(dev, p, regs[P_LOCAL_CTRL], &ctrl);
		data = ADVERTISE_CSMA;
		if (ctrl & PORT_AUTO_NEG_SYM_PAUSE)
			data |= ADVERTISE_PAUSE_CAP;
		if (ctrl & PORT_AUTO_NEG_100BTX_FD)
			data |= ADVERTISE_100FULL;
		if (ctrl & PORT_AUTO_NEG_100BTX)
			data |= ADVERTISE_100HALF;
		if (ctrl & PORT_AUTO_NEG_10BT_FD)
			data |= ADVERTISE_10FULL;
		if (ctrl & PORT_AUTO_NEG_10BT)
			data |= ADVERTISE_10HALF;
		break;
	case MII_LPA:
		ksz_pread8(dev, p, regs[P_REMOTE_STATUS], &link);
		data = LPA_SLCT;
		if (link & PORT_REMOTE_SYM_PAUSE)
			data |= LPA_PAUSE_CAP;
		if (link & PORT_REMOTE_100BTX_FD)
			data |= LPA_100FULL;
		if (link & PORT_REMOTE_100BTX)
			data |= LPA_100HALF;
		if (link & PORT_REMOTE_10BT_FD)
			data |= LPA_10FULL;
		if (link & PORT_REMOTE_10BT)
			data |= LPA_10HALF;
		if (data & ~LPA_SLCT)
			data |= LPA_LPACK;
		break;
	case PHY_REG_LINK_MD:
		ksz_pread8(dev, p, REG_PORT_LINK_MD_CTRL, &val1);
		ksz_pread8(dev, p, REG_PORT_LINK_MD_RESULT, &val2);
		if (val1 & PORT_START_CABLE_DIAG)
			data |= PHY_START_CABLE_DIAG;

		if (val1 & PORT_CABLE_10M_SHORT)
			data |= PHY_CABLE_10M_SHORT;

		data |= FIELD_PREP(PHY_CABLE_DIAG_RESULT_M,
				FIELD_GET(PORT_CABLE_DIAG_RESULT_M, val1));

		data |= FIELD_PREP(PHY_CABLE_FAULT_COUNTER_M,
				(FIELD_GET(PORT_CABLE_FAULT_COUNTER_H, val1) << 8) |
				FIELD_GET(PORT_CABLE_FAULT_COUNTER_L, val2));
		break;
	case PHY_REG_PHY_CTRL:
		ksz_pread8(dev, p, regs[P_LINK_STATUS], &link);
		if (link & PORT_MDIX_STATUS)
			data |= KSZ886X_CTRL_MDIX_STAT;
		break;
	default:
		processed = false;
		break;
	}
	if (processed)
		*val = data;
}

static void ksz8_w_phy(struct ksz_device *dev, u16 phy, u16 reg, u16 val)
{
	struct ksz8 *ksz8 = dev->priv;
	u8 restart, speed, ctrl, data;
	const u8 *regs = ksz8->regs;
	u8 p = phy;

	switch (reg) {
	case MII_BMCR:

		/* Do not support PHY reset function. */
		if (val & BMCR_RESET)
			break;
		ksz_pread8(dev, p, regs[P_SPEED_STATUS], &speed);
		data = speed;
		if (val & KSZ886X_BMCR_HP_MDIX)
			data |= PORT_HP_MDIX;
		else
			data &= ~PORT_HP_MDIX;
		if (data != speed)
			ksz_pwrite8(dev, p, regs[P_SPEED_STATUS], data);
		ksz_pread8(dev, p, regs[P_FORCE_CTRL], &ctrl);
		data = ctrl;
		if (ksz_is_ksz88x3(dev)) {
			if ((val & BMCR_ANENABLE))
				data |= PORT_AUTO_NEG_ENABLE;
			else
				data &= ~PORT_AUTO_NEG_ENABLE;
		} else {
			if (!(val & BMCR_ANENABLE))
				data |= PORT_AUTO_NEG_DISABLE;
			else
				data &= ~PORT_AUTO_NEG_DISABLE;

			/* Fiber port does not support auto-negotiation. */
			if (dev->ports[p].fiber)
				data |= PORT_AUTO_NEG_DISABLE;
		}

		if (val & BMCR_SPEED100)
			data |= PORT_FORCE_100_MBIT;
		else
			data &= ~PORT_FORCE_100_MBIT;
		if (val & BMCR_FULLDPLX)
			data |= PORT_FORCE_FULL_DUPLEX;
		else
			data &= ~PORT_FORCE_FULL_DUPLEX;
		if (data != ctrl)
			ksz_pwrite8(dev, p, regs[P_FORCE_CTRL], data);
		ksz_pread8(dev, p, regs[P_NEG_RESTART_CTRL], &restart);
		data = restart;
		if (val & KSZ886X_BMCR_DISABLE_LED)
			data |= PORT_LED_OFF;
		else
			data &= ~PORT_LED_OFF;
		if (val & KSZ886X_BMCR_DISABLE_TRANSMIT)
			data |= PORT_TX_DISABLE;
		else
			data &= ~PORT_TX_DISABLE;
		if (val & BMCR_ANRESTART)
			data |= PORT_AUTO_NEG_RESTART;
		else
			data &= ~(PORT_AUTO_NEG_RESTART);
		if (val & BMCR_PDOWN)
			data |= PORT_POWER_DOWN;
		else
			data &= ~PORT_POWER_DOWN;
		if (val & KSZ886X_BMCR_DISABLE_AUTO_MDIX)
			data |= PORT_AUTO_MDIX_DISABLE;
		else
			data &= ~PORT_AUTO_MDIX_DISABLE;
		if (val & KSZ886X_BMCR_FORCE_MDI)
			data |= PORT_FORCE_MDIX;
		else
			data &= ~PORT_FORCE_MDIX;
		if (val & BMCR_LOOPBACK)
			data |= PORT_PHY_LOOPBACK;
		else
			data &= ~PORT_PHY_LOOPBACK;
		if (data != restart)
			ksz_pwrite8(dev, p, regs[P_NEG_RESTART_CTRL], data);
		break;
	case MII_ADVERTISE:
		ksz_pread8(dev, p, regs[P_LOCAL_CTRL], &ctrl);
		data = ctrl;
		data &= ~(PORT_AUTO_NEG_SYM_PAUSE |
			  PORT_AUTO_NEG_100BTX_FD |
			  PORT_AUTO_NEG_100BTX |
			  PORT_AUTO_NEG_10BT_FD |
			  PORT_AUTO_NEG_10BT);
		if (val & ADVERTISE_PAUSE_CAP)
			data |= PORT_AUTO_NEG_SYM_PAUSE;
		if (val & ADVERTISE_100FULL)
			data |= PORT_AUTO_NEG_100BTX_FD;
		if (val & ADVERTISE_100HALF)
			data |= PORT_AUTO_NEG_100BTX;
		if (val & ADVERTISE_10FULL)
			data |= PORT_AUTO_NEG_10BT_FD;
		if (val & ADVERTISE_10HALF)
			data |= PORT_AUTO_NEG_10BT;
		if (data != ctrl)
			ksz_pwrite8(dev, p, regs[P_LOCAL_CTRL], data);
		break;
	case PHY_REG_LINK_MD:
		if (val & PHY_START_CABLE_DIAG)
			ksz_port_cfg(dev, p, REG_PORT_LINK_MD_CTRL, PORT_START_CABLE_DIAG, true);
		break;
	default:
		break;
	}
}

static u32 ksz8_sw_get_phy_flags(struct dsa_switch *ds, int port)
{
	/* Silicon Errata Sheet (DS80000830A):
	 * Port 1 does not work with LinkMD Cable-Testing.
	 * Port 1 does not respond to received PAUSE control frames.
	 */
	if (!port)
		return MICREL_KSZ8_P1_ERRATA;

	return 0;
}

static void ksz8_cfg_port_member(struct ksz_device *dev, int port, u8 member)
{
	u8 data;

	ksz_pread8(dev, port, P_MIRROR_CTRL, &data);
	data &= ~PORT_VLAN_MEMBERSHIP;
	data |= (member & dev->port_mask);
	ksz_pwrite8(dev, port, P_MIRROR_CTRL, data);
}

static void ksz8_port_stp_state_set(struct dsa_switch *ds, int port, u8 state)
{
	ksz_port_stp_state_set(ds, port, state, P_STP_CTRL);
}

static void ksz8_flush_dyn_mac_table(struct ksz_device *dev, int port)
{
	u8 learn[DSA_MAX_PORTS];
	int first, index, cnt;
	struct ksz_port *p;

	if ((uint)port < dev->info->port_cnt) {
		first = port;
		cnt = port + 1;
	} else {
		/* Flush all ports. */
		first = 0;
		cnt = dev->info->port_cnt;
	}
	for (index = first; index < cnt; index++) {
		p = &dev->ports[index];
		if (!p->on)
			continue;
		ksz_pread8(dev, index, P_STP_CTRL, &learn[index]);
		if (!(learn[index] & PORT_LEARN_DISABLE))
			ksz_pwrite8(dev, index, P_STP_CTRL,
				    learn[index] | PORT_LEARN_DISABLE);
	}
	ksz_cfg(dev, S_FLUSH_TABLE_CTRL, SW_FLUSH_DYN_MAC_TABLE, true);
	for (index = first; index < cnt; index++) {
		p = &dev->ports[index];
		if (!p->on)
			continue;
		if (!(learn[index] & PORT_LEARN_DISABLE))
			ksz_pwrite8(dev, index, P_STP_CTRL, learn[index]);
	}
}

static int ksz8_port_vlan_filtering(struct ksz_device *dev, int port, bool flag,
				    struct netlink_ext_ack *extack)
{
	if (ksz_is_ksz88x3(dev))
		return -ENOTSUPP;

	/* Discard packets with VID not enabled on the switch */
	ksz_cfg(dev, S_MIRROR_CTRL, SW_VLAN_ENABLE, flag);

	/* Discard packets with VID not enabled on the ingress port */
	for (port = 0; port < dev->phy_port_cnt; ++port)
		ksz_port_cfg(dev, port, REG_PORT_CTRL_2, PORT_INGRESS_FILTER,
			     flag);

	return 0;
}

static void ksz8_port_enable_pvid(struct ksz_device *dev, int port, bool state)
{
	if (ksz_is_ksz88x3(dev)) {
		ksz_cfg(dev, REG_SW_INSERT_SRC_PVID,
			0x03 << (4 - 2 * port), state);
	} else {
		ksz_pwrite8(dev, port, REG_PORT_CTRL_12, state ? 0x0f : 0x00);
	}
}

static int ksz8_port_vlan_add(struct ksz_device *dev, int port,
			      const struct switchdev_obj_port_vlan *vlan,
			      struct netlink_ext_ack *extack)
{
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	struct ksz_port *p = &dev->ports[port];
	u16 data, new_pvid = 0;
	u8 fid, member, valid;

	if (ksz_is_ksz88x3(dev))
		return -ENOTSUPP;

	/* If a VLAN is added with untagged flag different from the
	 * port's Remove Tag flag, we need to change the latter.
	 * Ignore VID 0, which is always untagged.
	 * Ignore CPU port, which will always be tagged.
	 */
	if (untagged != p->remove_tag && vlan->vid != 0 &&
	    port != dev->cpu_port) {
		unsigned int vid;

		/* Reject attempts to add a VLAN that requires the
		 * Remove Tag flag to be changed, unless there are no
		 * other VLANs currently configured.
		 */
		for (vid = 1; vid < dev->info->num_vlans; ++vid) {
			/* Skip the VID we are going to add or reconfigure */
			if (vid == vlan->vid)
				continue;

			ksz8_from_vlan(dev, dev->vlan_cache[vid].table[0],
				       &fid, &member, &valid);
			if (valid && (member & BIT(port)))
				return -EINVAL;
		}

		ksz_port_cfg(dev, port, P_TAG_CTRL, PORT_REMOVE_TAG, untagged);
		p->remove_tag = untagged;
	}

	ksz8_r_vlan_table(dev, vlan->vid, &data);
	ksz8_from_vlan(dev, data, &fid, &member, &valid);

	/* First time to setup the VLAN entry. */
	if (!valid) {
		/* Need to find a way to map VID to FID. */
		fid = 1;
		valid = 1;
	}
	member |= BIT(port);

	ksz8_to_vlan(dev, fid, member, valid, &data);
	ksz8_w_vlan_table(dev, vlan->vid, data);

	/* change PVID */
	if (vlan->flags & BRIDGE_VLAN_INFO_PVID)
		new_pvid = vlan->vid;

	if (new_pvid) {
		u16 vid;

		ksz_pread16(dev, port, REG_PORT_CTRL_VID, &vid);
		vid &= ~VLAN_VID_MASK;
		vid |= new_pvid;
		ksz_pwrite16(dev, port, REG_PORT_CTRL_VID, vid);

		ksz8_port_enable_pvid(dev, port, true);
	}

	return 0;
}

static int ksz8_port_vlan_del(struct ksz_device *dev, int port,
			      const struct switchdev_obj_port_vlan *vlan)
{
	u16 data, pvid;
	u8 fid, member, valid;

	if (ksz_is_ksz88x3(dev))
		return -ENOTSUPP;

	ksz_pread16(dev, port, REG_PORT_CTRL_VID, &pvid);
	pvid = pvid & 0xFFF;

	ksz8_r_vlan_table(dev, vlan->vid, &data);
	ksz8_from_vlan(dev, data, &fid, &member, &valid);

	member &= ~BIT(port);

	/* Invalidate the entry if no more member. */
	if (!member) {
		fid = 0;
		valid = 0;
	}

	ksz8_to_vlan(dev, fid, member, valid, &data);
	ksz8_w_vlan_table(dev, vlan->vid, data);

	if (pvid == vlan->vid)
		ksz8_port_enable_pvid(dev, port, false);

	return 0;
}

static int ksz8_port_mirror_add(struct ksz_device *dev, int port,
				struct dsa_mall_mirror_tc_entry *mirror,
				bool ingress, struct netlink_ext_ack *extack)
{
	if (ingress) {
		ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_RX, true);
		dev->mirror_rx |= BIT(port);
	} else {
		ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_TX, true);
		dev->mirror_tx |= BIT(port);
	}

	ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_SNIFFER, false);

	/* configure mirror port */
	if (dev->mirror_rx || dev->mirror_tx)
		ksz_port_cfg(dev, mirror->to_local_port, P_MIRROR_CTRL,
			     PORT_MIRROR_SNIFFER, true);

	return 0;
}

static void ksz8_port_mirror_del(struct ksz_device *dev, int port,
				 struct dsa_mall_mirror_tc_entry *mirror)
{
	u8 data;

	if (mirror->ingress) {
		ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_RX, false);
		dev->mirror_rx &= ~BIT(port);
	} else {
		ksz_port_cfg(dev, port, P_MIRROR_CTRL, PORT_MIRROR_TX, false);
		dev->mirror_tx &= ~BIT(port);
	}

	ksz_pread8(dev, port, P_MIRROR_CTRL, &data);

	if (!dev->mirror_rx && !dev->mirror_tx)
		ksz_port_cfg(dev, mirror->to_local_port, P_MIRROR_CTRL,
			     PORT_MIRROR_SNIFFER, false);
}

static void ksz8795_cpu_interface_select(struct ksz_device *dev, int port)
{
	struct ksz_port *p = &dev->ports[port];
	u8 data8;

	if (!p->interface && dev->compat_interface) {
		dev_warn(dev->dev,
			 "Using legacy switch \"phy-mode\" property, because it is missing on port %d node. "
			 "Please update your device tree.\n",
			 port);
		p->interface = dev->compat_interface;
	}

	/* Configure MII interface for proper network communication. */
	ksz_read8(dev, REG_PORT_5_CTRL_6, &data8);
	data8 &= ~PORT_INTERFACE_TYPE;
	data8 &= ~PORT_GMII_1GPS_MODE;
	switch (p->interface) {
	case PHY_INTERFACE_MODE_MII:
		p->phydev.speed = SPEED_100;
		break;
	case PHY_INTERFACE_MODE_RMII:
		data8 |= PORT_INTERFACE_RMII;
		p->phydev.speed = SPEED_100;
		break;
	case PHY_INTERFACE_MODE_GMII:
		data8 |= PORT_GMII_1GPS_MODE;
		data8 |= PORT_INTERFACE_GMII;
		p->phydev.speed = SPEED_1000;
		break;
	default:
		data8 &= ~PORT_RGMII_ID_IN_ENABLE;
		data8 &= ~PORT_RGMII_ID_OUT_ENABLE;
		if (p->interface == PHY_INTERFACE_MODE_RGMII_ID ||
		    p->interface == PHY_INTERFACE_MODE_RGMII_RXID)
			data8 |= PORT_RGMII_ID_IN_ENABLE;
		if (p->interface == PHY_INTERFACE_MODE_RGMII_ID ||
		    p->interface == PHY_INTERFACE_MODE_RGMII_TXID)
			data8 |= PORT_RGMII_ID_OUT_ENABLE;
		data8 |= PORT_GMII_1GPS_MODE;
		data8 |= PORT_INTERFACE_RGMII;
		p->phydev.speed = SPEED_1000;
		break;
	}
	ksz_write8(dev, REG_PORT_5_CTRL_6, data8);
	p->phydev.duplex = 1;
}

static void ksz8_port_setup(struct ksz_device *dev, int port, bool cpu_port)
{
	struct dsa_switch *ds = dev->ds;
	struct ksz8 *ksz8 = dev->priv;
	const u32 *masks;
	u8 member;

	masks = ksz8->masks;

	/* enable broadcast storm limit */
	ksz_port_cfg(dev, port, P_BCAST_STORM_CTRL, PORT_BROADCAST_STORM, true);

	if (!ksz_is_ksz88x3(dev))
		ksz8795_set_prio_queue(dev, port, 4);

	/* disable DiffServ priority */
	ksz_port_cfg(dev, port, P_PRIO_CTRL, PORT_DIFFSERV_ENABLE, false);

	/* replace priority */
	ksz_port_cfg(dev, port, P_802_1P_CTRL,
		     masks[PORT_802_1P_REMAPPING], false);

	/* enable 802.1p priority */
	ksz_port_cfg(dev, port, P_PRIO_CTRL, PORT_802_1P_ENABLE, true);

	if (cpu_port) {
		if (!ksz_is_ksz88x3(dev))
			ksz8795_cpu_interface_select(dev, port);

		member = dsa_user_ports(ds);
	} else {
		member = BIT(dsa_upstream_port(ds, port));
	}

	ksz8_cfg_port_member(dev, port, member);
}

static void ksz8_config_cpu_port(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	struct ksz8 *ksz8 = dev->priv;
	const u8 *regs = ksz8->regs;
	struct ksz_port *p;
	const u32 *masks;
	u8 remote;
	int i;

	masks = ksz8->masks;

	/* Switch marks the maximum frame with extra byte as oversize. */
	ksz_cfg(dev, REG_SW_CTRL_2, SW_LEGAL_PACKET_DISABLE, true);
	ksz_cfg(dev, regs[S_TAIL_TAG_CTRL], masks[SW_TAIL_TAG_ENABLE], true);

	p = &dev->ports[dev->cpu_port];
	p->on = 1;

	ksz8_port_setup(dev, dev->cpu_port, true);

	for (i = 0; i < dev->phy_port_cnt; i++) {
		p = &dev->ports[i];

		ksz8_port_stp_state_set(ds, i, BR_STATE_DISABLED);

		/* Last port may be disabled. */
		if (i == dev->phy_port_cnt)
			break;
		p->on = 1;
		p->phy = 1;
	}
	for (i = 0; i < dev->phy_port_cnt; i++) {
		p = &dev->ports[i];
		if (!p->on)
			continue;
		if (!ksz_is_ksz88x3(dev)) {
			ksz_pread8(dev, i, regs[P_REMOTE_STATUS], &remote);
			if (remote & KSZ8_PORT_FIBER_MODE)
				p->fiber = 1;
		}
		if (p->fiber)
			ksz_port_cfg(dev, i, P_STP_CTRL, PORT_FORCE_FLOW_CTRL,
				     true);
		else
			ksz_port_cfg(dev, i, P_STP_CTRL, PORT_FORCE_FLOW_CTRL,
				     false);
	}
}

static int ksz8_handle_global_errata(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	int ret = 0;

	/* KSZ87xx Errata DS80000687C.
	 * Module 2: Link drops with some EEE link partners.
	 *   An issue with the EEE next page exchange between the
	 *   KSZ879x/KSZ877x/KSZ876x and some EEE link partners may result in
	 *   the link dropping.
	 */
	if (dev->info->ksz87xx_eee_link_erratum)
		ret = ksz8_ind_write8(dev, TABLE_EEE, REG_IND_EEE_GLOB2_HI, 0);

	return ret;
}

static int ksz8_setup(struct dsa_switch *ds)
{
	struct ksz_device *dev = ds->priv;
	struct alu_struct alu;
	int i, ret = 0;

	dev->vlan_cache = devm_kcalloc(dev->dev, sizeof(struct vlan_table),
				       dev->info->num_vlans, GFP_KERNEL);
	if (!dev->vlan_cache)
		return -ENOMEM;

	ret = ksz8_reset_switch(dev);
	if (ret) {
		dev_err(ds->dev, "failed to reset switch\n");
		return ret;
	}

	ksz_cfg(dev, S_REPLACE_VID_CTRL, SW_FLOW_CTRL, true);

	/* Enable automatic fast aging when link changed detected. */
	ksz_cfg(dev, S_LINK_AGING_CTRL, SW_LINK_AUTO_AGING, true);

	/* Enable aggressive back off algorithm in half duplex mode. */
	regmap_update_bits(dev->regmap[0], REG_SW_CTRL_1,
			   SW_AGGR_BACKOFF, SW_AGGR_BACKOFF);

	/*
	 * Make sure unicast VLAN boundary is set as default and
	 * enable no excessive collision drop.
	 */
	regmap_update_bits(dev->regmap[0], REG_SW_CTRL_2,
			   UNICAST_VLAN_BOUNDARY | NO_EXC_COLLISION_DROP,
			   UNICAST_VLAN_BOUNDARY | NO_EXC_COLLISION_DROP);

	ksz8_config_cpu_port(ds);

	ksz_cfg(dev, REG_SW_CTRL_2, MULTICAST_STORM_DISABLE, true);

	ksz_cfg(dev, S_REPLACE_VID_CTRL, SW_REPLACE_VID, false);

	ksz_cfg(dev, S_MIRROR_CTRL, SW_MIRROR_RX_TX, false);

	if (!ksz_is_ksz88x3(dev))
		ksz_cfg(dev, REG_SW_CTRL_19, SW_INS_TAG_ENABLE, true);

	/* set broadcast storm protection 10% rate */
	regmap_update_bits(dev->regmap[1], S_REPLACE_VID_CTRL,
			   BROADCAST_STORM_RATE,
			   (BROADCAST_STORM_VALUE *
			   BROADCAST_STORM_PROT_RATE) / 100);

	for (i = 0; i < (dev->info->num_vlans / 4); i++)
		ksz8_r_vlan_entries(dev, i);

	/* Setup STP address for STP operation. */
	memset(&alu, 0, sizeof(alu));
	ether_addr_copy(alu.mac, eth_stp_addr);
	alu.is_static = true;
	alu.is_override = true;
	alu.port_forward = dev->info->cpu_ports;

	ksz8_w_sta_mac_table(dev, 0, &alu);

	ksz_init_mib_timer(dev);

	ds->configure_vlan_while_not_filtering = false;

	return ksz8_handle_global_errata(ds);
}

static void ksz8_get_caps(struct ksz_device *dev, int port,
			  struct phylink_config *config)
{
	config->mac_capabilities = MAC_10 | MAC_100;

	/* Silicon Errata Sheet (DS80000830A):
	 * "Port 1 does not respond to received flow control PAUSE frames"
	 * So, disable Pause support on "Port 1" (port == 0) for all ksz88x3
	 * switches.
	 */
	if (!ksz_is_ksz88x3(dev) || port)
		config->mac_capabilities |= MAC_SYM_PAUSE;

	/* Asym pause is not supported on KSZ8863 and KSZ8873 */
	if (!ksz_is_ksz88x3(dev))
		config->mac_capabilities |= MAC_ASYM_PAUSE;
}

static const struct dsa_switch_ops ksz8_switch_ops = {
	.get_tag_protocol	= ksz_get_tag_protocol,
	.get_phy_flags		= ksz8_sw_get_phy_flags,
	.setup			= ksz8_setup,
	.phy_read		= ksz_phy_read16,
	.phy_write		= ksz_phy_write16,
	.phylink_get_caps	= ksz_phylink_get_caps,
	.phylink_mac_link_down	= ksz_mac_link_down,
	.port_enable		= ksz_enable_port,
	.get_strings		= ksz_get_strings,
	.get_ethtool_stats	= ksz_get_ethtool_stats,
	.get_sset_count		= ksz_sset_count,
	.port_bridge_join	= ksz_port_bridge_join,
	.port_bridge_leave	= ksz_port_bridge_leave,
	.port_stp_state_set	= ksz8_port_stp_state_set,
	.port_fast_age		= ksz_port_fast_age,
	.port_vlan_filtering	= ksz_port_vlan_filtering,
	.port_vlan_add		= ksz_port_vlan_add,
	.port_vlan_del		= ksz_port_vlan_del,
	.port_fdb_dump		= ksz_port_fdb_dump,
	.port_mdb_add           = ksz_port_mdb_add,
	.port_mdb_del           = ksz_port_mdb_del,
	.port_mirror_add	= ksz_port_mirror_add,
	.port_mirror_del	= ksz_port_mirror_del,
};

static u32 ksz8_get_port_addr(int port, int offset)
{
	return PORT_CTRL_ADDR(port, offset);
}

static int ksz8_switch_init(struct ksz_device *dev)
{
	struct ksz8 *ksz8 = dev->priv;

	dev->ds->ops = &ksz8_switch_ops;

	dev->cpu_port = fls(dev->info->cpu_ports) - 1;
	dev->phy_port_cnt = dev->info->port_cnt - 1;
	dev->port_mask = (BIT(dev->phy_port_cnt) - 1) | dev->info->cpu_ports;

	if (ksz_is_ksz88x3(dev)) {
		ksz8->regs = ksz8863_regs;
		ksz8->masks = ksz8863_masks;
		ksz8->shifts = ksz8863_shifts;
	} else {
		ksz8->regs = ksz8795_regs;
		ksz8->masks = ksz8795_masks;
		ksz8->shifts = ksz8795_shifts;
	}

	/* We rely on software untagging on the CPU port, so that we
	 * can support both tagged and untagged VLANs
	 */
	dev->ds->untag_bridge_pvid = true;

	/* VLAN filtering is partly controlled by the global VLAN
	 * Enable flag
	 */
	dev->ds->vlan_filtering_is_global = true;

	return 0;
}

static void ksz8_switch_exit(struct ksz_device *dev)
{
	ksz8_reset_switch(dev);
}

static const struct ksz_dev_ops ksz8_dev_ops = {
	.get_port_addr = ksz8_get_port_addr,
	.cfg_port_member = ksz8_cfg_port_member,
	.flush_dyn_mac_table = ksz8_flush_dyn_mac_table,
	.port_setup = ksz8_port_setup,
	.r_phy = ksz8_r_phy,
	.w_phy = ksz8_w_phy,
	.r_dyn_mac_table = ksz8_r_dyn_mac_table,
	.r_sta_mac_table = ksz8_r_sta_mac_table,
	.w_sta_mac_table = ksz8_w_sta_mac_table,
	.r_mib_cnt = ksz8_r_mib_cnt,
	.r_mib_pkt = ksz8_r_mib_pkt,
	.freeze_mib = ksz8_freeze_mib,
	.port_init_cnt = ksz8_port_init_cnt,
	.vlan_filtering = ksz8_port_vlan_filtering,
	.vlan_add = ksz8_port_vlan_add,
	.vlan_del = ksz8_port_vlan_del,
	.mirror_add = ksz8_port_mirror_add,
	.mirror_del = ksz8_port_mirror_del,
	.get_caps = ksz8_get_caps,
	.shutdown = ksz8_reset_switch,
	.init = ksz8_switch_init,
	.exit = ksz8_switch_exit,
};

int ksz8_switch_register(struct ksz_device *dev)
{
	return ksz_switch_register(dev, &ksz8_dev_ops);
}
EXPORT_SYMBOL(ksz8_switch_register);

MODULE_AUTHOR("Tristram Ha <Tristram.Ha@microchip.com>");
MODULE_DESCRIPTION("Microchip KSZ8795 Series Switch DSA Driver");
MODULE_LICENSE("GPL");
