#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/pmbus.h>
#include "pmbus.h"

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

static struct pmbus_driver_info dps1900_info = {
	.pages = 1,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
	         | PMBUS_HAVE_IIN | PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT
	         | PMBUS_HAVE_FAN12 | PMBUS_HAVE_STATUS_FAN12
	         | PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2 | PMBUS_HAVE_STATUS_TEMP
	         | PMBUS_HAVE_STATUS_INPUT,
	.read_word_data = dps1900_read_word_data,
};

static int dps1900_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	return pmbus_do_probe(client, id, &dps1900_info);
}

static const struct i2c_device_id dps1900_id[] = {
	{"dps1900", 0},
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
