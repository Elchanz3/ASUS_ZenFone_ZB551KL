/* Copyright (c) 2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/bitops.h>
#include <linux/types.h>

struct ktd2151_regulator {
    struct regulator_init_data  * init_data;
    struct regulator_dev  * rdev;
    struct device_node    * node;
    struct regulator_desc   rdesc;
    struct ktd2151_chip   * chip;
    enum   of_gpio_flags    gpio_flags;
    const char  * name;
    u8        vol_reg;
    u8        dischg_bit_pos;
    bool      dischg_en;
    u8        ctrl_reg;
    int       en_gpio;
    bool      is_enabled;
    int       curr_uV;
    u8        vol_set_val;
    bool      vol_set_postpone;
};

struct ktd2151_chip {
    struct ktd2151_regulator  * vreg;
    struct regulator  * i2c_pwr;
    struct regmap     * regmap;
    struct device     * dev;
    u8        num_regulators;
    u8        ctrl_dischg_reg;
    u8        regoffset_cfg;
    u8        regoffset_cfg_bit_pos;
    u8        ctrl_dischg_val;
    bool      ctrl_dischg_cfg_postpone;
    bool      en_gpio_lpm;
};

#define   KTD2151_REG_VPOS              (0x00)  /* VPOS Positive Voltage Output Setting Register */
#define   KTD2151_REG_VNEG              (0x01)  /* VNEG Negative Voltage Output Setting Register */
#define   KTD2151_REG_CTRL              (0x03)  /* Control Register 1 */

/* For VPOS and VNEG register */
#define   KTD2151_VOLTAGE_MASK          (0x1F)  /* Bit4-0 */
/* For Control register */
#define   KTD2151_DISCHARGE_NEG_BIT     (0)     /* Discharge OUTN */
#define   KTD2151_DISCHARGE_POS_BIT     (1)     /* Discharge OUTP */
#define   KTD2151_REGOFFSET_BIT         (6)     /* REG Offset Control */

#define   KTD2151_VOLTAGE_MIN           (4000000) /* -4.0V/4.0V */
#define   KTD2151_VOLTAGE_MAX           (6300000) /* -6.3V/6.3V */
#define   KTD2151_VOLTAGE_STEP          (100000)  /* 0.1V */
#define   KTD2151_VOLTAGE_LEVELS        ((KTD2151_VOLTAGE_MAX - KTD2151_VOLTAGE_MIN) \
                                         / KTD2151_VOLTAGE_STEP + 1)

#define   KTD2151_NEG_CURR_LIMIT_UA     (80000)   /* 80mA */
#define   KTD2151_POS_CURR_LIMIT_UA     (200000)  /* 200mA */

#define   I2C_VOLTAGE_LEVEL             (1800000) /* 1.8V */

uint8_t buff[] = {0x00,0x0e,};
uint8_t buff_1[] = {0x01,0x0e,};

enum {
    KTD2151_POSITIVE_BOOST = 0,
    KTD2151_NEGATIVE_BOOST,
};



/*****************************************************************
**
******************************************************************/
static struct regmap_config   ktd2151_regmap_config = {
    .reg_bits = 8,
    .val_bits = 8,
};

static int ktd2151_regulator_probe(struct i2c_client *client,
        const struct i2c_device_id *id)
{
struct ktd2151_chip     * chip;

int rec=0;

pr_err("%s ++\n",__func__);
    chip = devm_kzalloc( &client->dev, sizeof( struct ktd2151_chip ), GFP_KERNEL);
    if( !chip )
    {
      pr_err("memory allocation failed for ktd2151_chip\n");
      return -ENOMEM;
    }

    chip->regmap = devm_regmap_init_i2c( client, &ktd2151_regmap_config );
    if( IS_ERR( chip->regmap ))
    {
      pr_err("Init regmap failed for ktd2151, rc = %ld\n", PTR_ERR( chip->regmap ));
      return  PTR_ERR( chip->regmap );
    }
    chip->dev = &client->dev;

    i2c_set_clientdata( client, chip );

	rec=i2c_master_send(client,buff,  sizeof(buff));
	if (rec !=sizeof(buff) ){
		pr_err("[%s]i2c_master_send buff failed\n",__func__);
		return -EINVAL;
	}

	rec=i2c_master_send(client,buff_1,  sizeof(buff_1));
	if (rec !=sizeof(buff_1) ){
		pr_err("[%s]i2c_master_send buff_1 failed\n",__func__);
                return -EINVAL;
	}

pr_err("%s --\n",__func__);
    return 0;
}

/*****************************************************************
**
******************************************************************/
static int ktd2151_regulator_remove(struct i2c_client *client)
{

    return 0;
}

/*****************************************************************
**
******************************************************************/
static struct of_device_id  ktd2151_match_table[] = {
    { .compatible = "ktd,ktd2151", },
    {},
};
MODULE_DEVICE_TABLE( of, ktd2151_match_table );


static const struct i2c_device_id   ktd2151_id[] = {
    { "ktd2151", -1 },
    { },
};

/*****************************************************************
**
******************************************************************/
static int ktd2151_suspend(struct device *dev)
{

    return 0;
}

/*****************************************************************
**
******************************************************************/
static int ktd2151_resume(struct device *dev)
{

    return 0;
}

/*****************************************************************
**
******************************************************************/
static const struct dev_pm_ops ktd2151_pm_ops = {
    .resume   = ktd2151_resume,
    .suspend  = ktd2151_suspend,
};

static struct i2c_driver ktd2151_regulator_driver = {
    .driver = {
        .name     = "ktd2151",
        .owner    = THIS_MODULE,
        .of_match_table = ktd2151_match_table,
        .pm       = &ktd2151_pm_ops,
    },
    .probe        = ktd2151_regulator_probe,
    .remove       = ktd2151_regulator_remove,
    .id_table     = ktd2151_id,
};

/*****************************************************************
**
******************************************************************/
static int __init ktd2151_init(void)
{
    pr_info("ktd2151_init...\n");

    return i2c_add_driver( &ktd2151_regulator_driver );
}
subsys_initcall( ktd2151_init );

static void __exit ktd2151_exit(void)
{
    i2c_del_driver( &ktd2151_regulator_driver );
}
module_exit( ktd2151_exit );

/*****************************************************************
**
******************************************************************/
MODULE_DESCRIPTION("KINETIC KTD2151 regulator driver");
MODULE_LICENSE("GPL v2");
