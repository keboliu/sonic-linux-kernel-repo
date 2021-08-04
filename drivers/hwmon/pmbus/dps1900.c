// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/pmbus.h>
#include "pmbus.h"

enum chips { dps1900, dps200 };

static int dps1900_read_word_data(struct i2c_client *client, int page, int reg)
{
	if (reg >= PMBUS_VIRT_BASE ||
	    reg == PMBUS_OT_WARN_LIMIT ||
	    reg == PMBUS_OT_FAULT_LIMIT ||
	    reg == PMBUS_UT_WARN_LIMIT ||
	    reg == PMBUS_UT_FAULT_LIMIT ||
	    reg == PMBUS_VIN_OV_FAULT_LIMIT ||
	    reg == PMBUS_VIN_OV_WARN_LIMIT ||
	    reg == PMBUS_VIN_UV_WARN_LIMIT ||
	    reg == PMBUS_VIN_UV_FAULT_LIMIT ||
	    reg == PMBUS_VOUT_UV_WARN_LIMIT ||
	    reg == PMBUS_VOUT_OV_WARN_LIMIT ||
	    reg == PMBUS_POUT_OP_FAULT_LIMIT ||
	    reg == PMBUS_POUT_OP_WARN_LIMIT ||
	    reg == PMBUS_PIN_OP_WARN_LIMIT ||
	    reg == PMBUS_IIN_OC_FAULT_LIMIT ||
	    reg == PMBUS_IOUT_UC_FAULT_LIMIT ||
	    reg == PMBUS_POUT_MAX)
		return -ENXIO;
	return pmbus_read_word_data(client, page, reg);
}

static int dps200_read_word_data(struct i2c_client *client, int page, int reg)
{

	if (reg >= PMBUS_VIRT_BASE ||
	    reg == PMBUS_VOUT_OV_FAULT_LIMIT ||
	    reg == PMBUS_VOUT_OV_WARN_LIMIT ||
	    reg == PMBUS_VOUT_UV_WARN_LIMIT ||
	    reg == PMBUS_VOUT_UV_FAULT_LIMIT ||
	    reg == PMBUS_IOUT_OC_LV_FAULT_LIMIT ||
	    reg == PMBUS_IOUT_UC_FAULT_LIMIT ||
	    reg == PMBUS_UT_WARN_LIMIT ||
	    reg == PMBUS_UT_FAULT_LIMIT ||
	    reg == PMBUS_VIN_OV_FAULT_LIMIT ||
	    reg == PMBUS_VIN_OV_WARN_LIMIT ||
	    reg == PMBUS_IIN_OC_FAULT_LIMIT ||
	    reg == PMBUS_POUT_MAX)
		return -ENXIO;

	/*
	 * NOTE: The following field have constant driver value.
	 * If the register are PMBUS_IOUT_OC_WARN_LIMIT or
	 * PMBUS_IOUT_OC_FAULT_LIMIT convert the constant value from linear
	 * to direct format.
	 */
	if (reg == PMBUS_IOUT_OC_WARN_LIMIT)
		return 0xb4;
	else if (reg == PMBUS_IOUT_OC_FAULT_LIMIT)
		return 0xc8;
	else
		return pmbus_read_word_data(client, page, reg);
}

static struct pmbus_driver_info dps1900_info = {
	.pages = 1,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		 | PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT
		 | PMBUS_HAVE_FAN12 | PMBUS_HAVE_STATUS_FAN12
		 | PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2 | PMBUS_HAVE_STATUS_TEMP
		 | PMBUS_HAVE_STATUS_INPUT,
	.read_word_data = dps1900_read_word_data,
};

/*
 * From DPS-200 datasheet the supported sensors format defined as:
 * VOUT: direct mode with one decimal place.
 *
 * Other sensors:
 * IOUT: direct mode with one decimal place.
 * IOUT Limits: linear mode, which difference from IOUT.
 * VIN, IIN, POWER, TEMP, & FAN: linear mode.
 */
static struct pmbus_driver_info dps200_info = {
	.pages = 1,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		 | PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT
		 | PMBUS_HAVE_PIN | PMBUS_HAVE_POUT
		 | PMBUS_HAVE_FAN12 | PMBUS_HAVE_STATUS_FAN12
		 | PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2 | PMBUS_HAVE_TEMP3
		 | PMBUS_HAVE_STATUS_TEMP | PMBUS_HAVE_STATUS_INPUT,
	.format = {
		[PSC_VOLTAGE_OUT] = direct,
		[PSC_CURRENT_OUT] = direct,
	},
	.m = {
		[PSC_VOLTAGE_OUT] = 10,
		[PSC_CURRENT_OUT] = 10,
	},
	.b = {
		[PSC_VOLTAGE_OUT] = 0,
		[PSC_CURRENT_OUT] = 0,
	},
	.R = {
		[PSC_VOLTAGE_OUT] = 0,
		[PSC_CURRENT_OUT] = 0,
	},
	.read_word_data = dps200_read_word_data,
};

static int dps1900_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct pmbus_driver_info *info;

	info = devm_kzalloc(&client->dev, sizeof(struct pmbus_driver_info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	switch (id->driver_data) {
	case dps1900:
		info = &dps1900_info;
		break;
	case dps200:
		info = &dps200_info;
		break;
	default:
		dev_err(&client->dev, "Unsupported device\n");
		return -ENODEV;
	}

	return pmbus_do_probe(client, id, info);
}

static const struct i2c_device_id dps1900_id[] = {
	{"dps1900", dps1900},
	{"dps200", dps200},
	{}
};

MODULE_DEVICE_TABLE(i2c, dps1900_id);

/* This is the driver that will be inserted */
static struct i2c_driver dps1900_driver = {
	.driver = {
		.name = "dps1900",
	},
	.probe = dps1900_probe,
	.remove = pmbus_do_remove,
	.id_table = dps1900_id,
};

module_i2c_driver(dps1900_driver);

MODULE_AUTHOR("Arista Networks");
MODULE_DESCRIPTION("PMBus driver for Delta DPS1900");
MODULE_LICENSE("GPL");
