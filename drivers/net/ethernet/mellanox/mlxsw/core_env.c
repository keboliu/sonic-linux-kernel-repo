// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/sfp.h>

#include "core.h"
#include "core_env.h"
#include "item.h"
#include "reg.h"

#define MLXSW_REG_MCIA_EEPROM_FLAT_MEMORY	BIT(7)
#define MLXSW_ENV_CMIS_PAGE_OFF	0x0d
#define MLXSW_ENV_PAGE_MAP(page) (((page) < \
	MLXSW_REG_MCIA_TH_PAGE_NUM) ? (page) : (page) + \
	MLXSW_ENV_CMIS_PAGE_OFF)

static int mlxsw_env_validate_cable_ident(struct mlxsw_core *core, int id,
					  bool *qsfp, bool *cmis)
{
	char eeprom_tmp[MLXSW_REG_MCIA_EEPROM_SIZE];
	char mcia_pl[MLXSW_REG_MCIA_LEN];
	u8 ident;
	int err;

	mlxsw_reg_mcia_pack(mcia_pl, id, 0, MLXSW_REG_MCIA_PAGE0_LO_OFF, 0, 1,
			    MLXSW_REG_MCIA_I2C_ADDR_LOW);
	err = mlxsw_reg_query(core, MLXSW_REG(mcia), mcia_pl);
	if (err)
		return err;
	mlxsw_reg_mcia_eeprom_memcpy_from(mcia_pl, eeprom_tmp);
	ident = eeprom_tmp[0];
	*cmis = false;
	switch (ident) {
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_SFP: /* fall-through */
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_SFP_DD:
		*qsfp = false;
		break;
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP: /* fall-through */
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP_PLUS: /* fall-through */
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP28:
		*qsfp = true;
		break;
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP_DD: /* fall-through */
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP_PLUS_CMIS:
		*qsfp = true;
		*cmis = true;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
mlxsw_env_query_module_eeprom(struct mlxsw_core *mlxsw_core, int module,
			      u16 offset, u16 size, bool qsfp, bool cmis,
			      void *data, unsigned int *p_read_size)
{
	char eeprom_tmp[MLXSW_REG_MCIA_EEPROM_SIZE];
	char mcia_pl[MLXSW_REG_MCIA_LEN];
	u16 i2c_addr;
	u8 page = 0;
	int status;
	int err;

	size = min_t(u16, size, MLXSW_REG_MCIA_EEPROM_SIZE);

	if (offset < MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH &&
	    offset + size > MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH)
		/* Cross pages read, read until offset 256 in low page */
		size = MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH - offset;

	i2c_addr = MLXSW_REG_MCIA_I2C_ADDR_LOW;
	if (offset >= MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH) {
		if (qsfp) {
			page = MLXSW_REG_MCIA_PAGE_GET(offset);
			offset -= MLXSW_REG_MCIA_EEPROM_UP_PAGE_LENGTH * page;
			if (offset + size > MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH)
				size = MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH -
				       offset;
		} else {
			i2c_addr = MLXSW_REG_MCIA_I2C_ADDR_HIGH;
			offset -= MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH;
		}
	}

	if (cmis)
		page = MLXSW_ENV_PAGE_MAP(page);
	mlxsw_reg_mcia_pack(mcia_pl, module, 0, page, offset, size, i2c_addr);

	err = mlxsw_reg_query(mlxsw_core, MLXSW_REG(mcia), mcia_pl);
	if (err)
		return err;

	status = mlxsw_reg_mcia_status_get(mcia_pl);
	if (status)
		return -EIO;

	mlxsw_reg_mcia_eeprom_memcpy_from(mcia_pl, eeprom_tmp);
	memcpy(data, eeprom_tmp, size);
	*p_read_size = size;

	return 0;
}

int mlxsw_env_module_temp_thresholds_get(struct mlxsw_core *core, int module,
					 int off, int *temp)
{
        unsigned int module_temp, module_crit, module_emerg;
	char eeprom_tmp[MLXSW_REG_MCIA_EEPROM_SIZE];
	union {
		u8 buf[MLXSW_REG_MCIA_TH_ITEM_SIZE];
		u16 temp;
	} temp_thresh;
	char mcia_pl[MLXSW_REG_MCIA_LEN] = {0};
	char mtmp_pl[MLXSW_REG_MTMP_LEN];
	bool qsfp, cmis;
	int page;
	int err;

	mlxsw_reg_mtmp_pack(mtmp_pl, MLXSW_REG_MTMP_MODULE_INDEX_MIN + module,
			    false, false);
	err = mlxsw_reg_query(core, MLXSW_REG(mtmp), mtmp_pl);
	if (err)
		return err;
	mlxsw_reg_mtmp_unpack(mtmp_pl, &module_temp, NULL, &module_crit,
			      &module_emerg, NULL);
	if (!module_temp) {
		*temp = 0;
		return 0;
	}

	/* Validate if threshold reading is available through MTMP register,
	 * otherwise fallback to read through MCIA.
	 */
	if (module_emerg) {
		*temp = off == SFP_TEMP_HIGH_WARN ? module_crit : module_emerg;
		return 0;
	}

	/* Read Free Side Device Temperature Thresholds from page 03h
	 * (MSB at lower byte address).
	 * Bytes:
	 * 128-129 - Temp High Alarm (SFP_TEMP_HIGH_ALARM);
	 * 130-131 - Temp Low Alarm (SFP_TEMP_LOW_ALARM);
	 * 132-133 - Temp High Warning (SFP_TEMP_HIGH_WARN);
	 * 134-135 - Temp Low Warning (SFP_TEMP_LOW_WARN);
	 */

	/* Validate module identifier value. */
	err = mlxsw_env_validate_cable_ident(core, module, &qsfp, &cmis);
	if (err)
		return err;

	if (qsfp) {
		if (cmis)
			page = MLXSW_REG_MCIA_TH_PAGE_CMIS_NUM;
		else
			page = MLXSW_REG_MCIA_TH_PAGE_NUM;
		mlxsw_reg_mcia_pack(mcia_pl, module, 0, page,
				    MLXSW_REG_MCIA_TH_PAGE_OFF + off,
				    MLXSW_REG_MCIA_TH_ITEM_SIZE,
				    MLXSW_REG_MCIA_I2C_ADDR_LOW);
	} else {
		mlxsw_reg_mcia_pack(mcia_pl, module, 0,
				    MLXSW_REG_MCIA_PAGE0_LO,
				    off, MLXSW_REG_MCIA_TH_ITEM_SIZE,
				    MLXSW_REG_MCIA_I2C_ADDR_HIGH);
	}

	err = mlxsw_reg_query(core, MLXSW_REG(mcia), mcia_pl);
	if (err)
		return err;

	mlxsw_reg_mcia_eeprom_memcpy_from(mcia_pl, eeprom_tmp);
	memcpy(temp_thresh.buf, eeprom_tmp, MLXSW_REG_MCIA_TH_ITEM_SIZE);
	*temp = temp_thresh.temp * 1000;

	return 0;
}

int mlxsw_env_get_module_info(struct mlxsw_core *mlxsw_core, int module,
			      struct ethtool_modinfo *modinfo)
{
	u8 module_info[MLXSW_REG_MCIA_EEPROM_MODULE_INFO_SIZE];
	u16 offset = MLXSW_REG_MCIA_EEPROM_MODULE_INFO_SIZE;
	u8 module_rev_id, module_id, diag_mon;
	unsigned int read_size;
	bool unused = false;
	int err;

	err = mlxsw_env_query_module_eeprom(mlxsw_core, module, 0, offset,
					    unused, unused, module_info,
					    &read_size);
	if (err)
		return err;

	if (read_size < offset)
		return -EIO;

	module_rev_id = module_info[MLXSW_REG_MCIA_EEPROM_MODULE_INFO_REV_ID];
	module_id = module_info[MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID];

	switch (module_id) {
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP:
		modinfo->type       = ETH_MODULE_SFF_8436;
		modinfo->eeprom_len = ETH_MODULE_SFF_8436_MAX_LEN;
		break;
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP_PLUS: /* fall-through */
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP28:
		if (module_id == MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP28 ||
		    module_rev_id >=
		    MLXSW_REG_MCIA_EEPROM_MODULE_INFO_REV_ID_8636) {
			modinfo->type       = ETH_MODULE_SFF_8636;
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_MAX_LEN;
		} else {
			modinfo->type       = ETH_MODULE_SFF_8436;
			modinfo->eeprom_len = ETH_MODULE_SFF_8436_MAX_LEN;
		}
		break;
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_SFP: /* fall-through */
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_SFP_DD:
		/* Verify if transceiver provides diagnostic monitoring page */
		err = mlxsw_env_query_module_eeprom(mlxsw_core, module,
						    SFP_DIAGMON, 1, unused,
						    unused, &diag_mon,
						    &read_size);
		if (err)
			return err;

		if (read_size < 1)
			return -EIO;

		modinfo->type       = ETH_MODULE_SFF_8472;
		if (diag_mon)
			modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
		else
			modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN / 2;
		break;
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP_DD: /* fall-through */
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP_PLUS_CMIS:
		modinfo->type       = ETH_MODULE_SFF_8636;
		/* Verify if module's EEPROM is a flat memory. In case of flat
		 * memory only page 00h 0-255 bytes can be read.
		 */
		if (module_info[MLXSW_REG_MCIA_EEPROM_MODULE_INFO_TYPE_ID] &
		    MLXSW_REG_MCIA_EEPROM_FLAT_MEMORY)
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_LEN;
		else
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_MAX_LEN;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(mlxsw_env_get_module_info);

int mlxsw_env_get_module_eeprom(struct net_device *netdev,
				struct mlxsw_core *mlxsw_core, int module,
				struct ethtool_eeprom *ee, u8 *data)
{
	int offset = ee->offset;
	unsigned int read_size;
	bool qsfp, cmis;
	int i = 0;
	int err;

	if (!ee->len)
		return -EINVAL;

	memset(data, 0, ee->len);

	/* Validate module identifier type. */
	err = mlxsw_env_validate_cable_ident(mlxsw_core, module, &qsfp, &cmis);
	if (err)
		return err;

	while (i < ee->len) {
		err = mlxsw_env_query_module_eeprom(mlxsw_core, module, offset,
						    ee->len - i, qsfp, cmis,
						    data + i, &read_size);
		if (err) {
			netdev_err(netdev, "Eeprom query failed\n");
			return err;
		}

		i += read_size;
		offset += read_size;
	}

	return 0;
}
EXPORT_SYMBOL(mlxsw_env_get_module_eeprom);
