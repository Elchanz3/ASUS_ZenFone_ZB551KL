/* Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/export.h>
#include <linux/gpio.h>
#include <linux/proc_fs.h>
#include "msm_led_flash.h"

#define FLASH_NAME "ti,lm3644tt"

//#define CONFIG_MSMB_LM3644TT_DEBUG
#undef CDBG
#ifdef CONFIG_MSMB_LM3644TT_DEBUG
#define CDBG(fmt, args...) pr_err(fmt, ##args)
#else
#define CDBG(fmt, args...) do { } while (0)
#endif

#define CHIP_ID_LM3644TT	0x04

#define REG_ENABLE		0x01
#define REG_IVFM		0x02
#define REG_1_FLASH_CR	0x03
#define REG_2_FLASH_CR	0x04
#define REG_1_TORCH_CR	0x05
#define REG_2_TORCH_CR	0x06
#define REG_BOOST		0x07
#define REG_TIMING		0x08
#define REG_TEMP		0x09
#define REG_FLAG1		0x0A
#define REG_FLAG2		0x0B
#define REG_DEV_ID		0x0C
#define REG_LAST_FLASH	0x0D

#define GPIO_EN	110
#define GPIO_TX	111
#define GPIO_STROBE	107
#define GPIO_TORCH	108

#define LED_OFF_VAL	255
#define MAX_FLASH_CR		127 // 1500mA
#define MAX_TORCH_CR		127 // 360mA
#define TO_BE_DEFINED	0

#define DEFAULT_FLASH_VAL		0x0C
#define DEFAULT_TORCH_VAL		0x08

#define PROC_ENTRY_ASUS_FLASH  "driver/asus_flash_brightness"
#define PROC_ENTRY_FLASH_LED1  "driver/flash_led1"
#define PROC_ENTRY_FLASH_LED2  "driver/flash_led2"
#define PROC_ENTRY_THERMAL_LEVEL "driver/thermal_level"	//ASUS_BSP Bryant: Porting file node for update thermal level.

static struct msm_led_flash_ctrl_t fctrl;
static struct i2c_driver lm3644tt_i2c_driver;

#define MAX_CT_ENTRY	11
static u8 g_flash_cr_map[MAX_CT_ENTRY][2] = {
	{LED_OFF_VAL, 84},
	{18, 70},
	{16, 67},
	{25, 59},
	{18, 70},
	{50, 84},   // for very dark
	{50, 33},
	{59, 25},
	{60, 28},
	{60, 28}, // {76, 8},
	{84, LED_OFF_VAL},
};
static u8 g_torch_cr_map[MAX_CT_ENTRY][2] = {
	{LED_OFF_VAL, 71},
	{20, 28},   // 136.353mA for D50, D65, D75 and Lab Flurescent
	{14, 56},
	{21, 49},
	{20, 28},   // for CWF and TL84
	{20, 28},   // for very dark
	{42, 27},
	{40,  8}, // for video
	{10, 10},   // for U30, Inca and Hori.
	{10, 10}, // {63, 6},
	{71, LED_OFF_VAL},
};

u8 g_ct_lv = 0;
u8 g_f1 = TO_BE_DEFINED;
u8 g_f2 = TO_BE_DEFINED;
u8 g_t1 = TO_BE_DEFINED;
u8 g_t2 = TO_BE_DEFINED;
u8 g_chip_en = 0;
u8 g_led1_en = 0;
u8 g_led2_en = 0;

static struct proc_dir_entry *proc_asus;
static struct proc_dir_entry *proc_led1;
static struct proc_dir_entry *proc_led2;
static struct proc_dir_entry *proc_thermal;	//ASUS_BSP Bryant: Porting file node for update thermal level.

static struct msm_camera_i2c_reg_array lm3644tt_init_array[] = {
	{REG_ENABLE,	0x00},
};

static struct msm_camera_i2c_reg_array lm3644tt_off_array[] = {
	{REG_ENABLE,	0x00},
};

static struct msm_camera_i2c_reg_array lm3644tt_release_array[] = {
};

static struct msm_camera_i2c_reg_array lm3644tt_low_array[] = {
	{REG_1_TORCH_CR,	TO_BE_DEFINED},
	{REG_2_TORCH_CR,	TO_BE_DEFINED},
	{REG_TIMING,		0x0B}, // Time of timeout, Torch no ramp, Flash=800ms
	{REG_ENABLE,		DEFAULT_TORCH_VAL + 0x03}, // Torch On
};
static struct msm_camera_i2c_reg_array lm3644tt_low_write_array[] = {
	{REG_1_TORCH_CR,	TO_BE_DEFINED},
	{REG_2_TORCH_CR,	TO_BE_DEFINED},
	{REG_TIMING,		0x0B}, // Time of timeout, Torch no ramp, Flash=800ms
	{REG_ENABLE,		DEFAULT_TORCH_VAL + 0x03}, // Torch On
};

static struct msm_camera_i2c_reg_array lm3644tt_high_array[] = {
	{REG_1_FLASH_CR,	TO_BE_DEFINED},
	{REG_2_FLASH_CR,	TO_BE_DEFINED},
	{REG_TIMING,		0x0B}, // Time of timeout, Torch no ramp, Flash=800ms
	{REG_ENABLE,		DEFAULT_FLASH_VAL + 0x03}, // Flash On
};
static struct msm_camera_i2c_reg_array lm3644tt_high_write_array[] = {
	{REG_1_FLASH_CR,	TO_BE_DEFINED},
	{REG_2_FLASH_CR,	TO_BE_DEFINED},
	{REG_TIMING,		0x0B}, // Time of timeout, Torch no ramp, Flash=800ms
	{REG_ENABLE,		DEFAULT_FLASH_VAL + 0x03}, // Flash On
};

static void lm3644tt_chip_en(u8 en)
{
	if (en == g_chip_en) return;

	g_chip_en = en;
	if (g_chip_en == 0)
	{
		// Pull Down these pins for LM3644TT exits
		gpio_set_value(GPIO_TORCH, 0);
		gpio_set_value(GPIO_STROBE, 0);
		gpio_set_value(GPIO_EN, 0);
		CDBG("[%s] Turn OFF chip \n", __func__);
	} else {
		// Pull High these pins for LM3644TT works
		gpio_set_value(GPIO_EN, g_chip_en);
		gpio_set_value(GPIO_STROBE, g_led1_en);
		gpio_set_value(GPIO_TORCH, g_led2_en);
		CDBG("[%s] Turn ON chip, LED1=%d, LED2=%d \n", __func__, g_led1_en, g_led2_en);
	}
}

static inline u8 lm3644tt_remove_1st_bit_of_current(u8 cr) {
	return cr >= 128 ? cr - 128 : cr;
}

// Attributes
static u8 DecideCurrent(u8 off_val, u8 max_val, u8 val) {
	if (val == off_val) return off_val;
	if (val >= max_val) return max_val;
	return val;
}
static ssize_t lm3644tt_show_ct_lv(struct device *dev, struct device_attribute *attr, char *buf)
{
	int rc = 0;
	CDBG("[%s] BEGIN g_flash=%d \n", __func__, g_ct_lv);
	rc = scnprintf(buf, PAGE_SIZE, "%d\n", g_ct_lv);

	CDBG("[%s] END \n", __func__);
	return rc;
}
static ssize_t lm3644tt_save_ct_lv(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u8 input_value;
	CDBG("[%s] BEGIN Buff=%s \n", __func__, buf);
	if (sscanf(buf, "%hhu", &input_value) != 1) {
		dev_err(dev, "Input value error \n");
		return -EINVAL;
	}

	g_ct_lv = input_value;
	fctrl.reg_setting->high_setting->reg_setting[0].reg_data = g_flash_cr_map[g_ct_lv][0];
	fctrl.reg_setting->high_setting->reg_setting[1].reg_data = g_flash_cr_map[g_ct_lv][1];
	fctrl.reg_setting->low_setting->reg_setting[0].reg_data = g_torch_cr_map[g_ct_lv][0];
	fctrl.reg_setting->low_setting->reg_setting[1].reg_data = g_torch_cr_map[g_ct_lv][1];

	CDBG("[%s] END \n", __func__);
	return count;
}
static DEVICE_ATTR(ct_lv, 0664, lm3644tt_show_ct_lv, lm3644tt_save_ct_lv);

static ssize_t lm3644tt_show_f1(struct device *dev, struct device_attribute *attr, char *buf)
{
	int rc = 0;
	CDBG("[%s] BEGIN g_f1=%d \n", __func__, g_f1);
	rc = scnprintf(buf, PAGE_SIZE, "%d\n", g_f1);

	CDBG("[%s] END \n", __func__);
	return rc;
}
static ssize_t lm3644tt_save_f1(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u8 input_value;
	CDBG("[%s] BEGIN Buff=%s \n", __func__, buf);
	if (sscanf(buf, "%hhu", &input_value) != 1) {
		dev_err(dev, "Input value error \n");
		return -EINVAL;
	}
	g_f1 = DecideCurrent(LED_OFF_VAL ,MAX_FLASH_CR , input_value);
	fctrl.reg_setting->high_setting->reg_setting[0].reg_data = g_f1;

	CDBG("[%s] END \n", __func__);
	return count;
}
static DEVICE_ATTR(f1, 0664, lm3644tt_show_f1, lm3644tt_save_f1);

static ssize_t lm3644tt_show_f2(struct device *dev, struct device_attribute *attr, char *buf)
{
	int rc = 0;
	CDBG("[%s] BEGIN g_f2=%d \n", __func__, g_f2);
	rc = scnprintf(buf, PAGE_SIZE, "%d\n", g_f2);

	CDBG("[%s] END \n", __func__);
	return rc;
}
static ssize_t lm3644tt_save_f2(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u8 input_value;
	CDBG("[%s] BEGIN Buff=%s \n", __func__, buf);
	if (sscanf(buf, "%hhu", &input_value) != 1) {
		dev_err(dev, "Input value error \n");
		return -EINVAL;
	}
	g_f2 =  DecideCurrent(LED_OFF_VAL ,MAX_FLASH_CR , input_value);
	fctrl.reg_setting->high_setting->reg_setting[1].reg_data = g_f2;

	CDBG("[%s] END \n", __func__);
	return count;
}
static DEVICE_ATTR(f2, 0664, lm3644tt_show_f2, lm3644tt_save_f2);

static ssize_t lm3644tt_show_flash(struct device *dev, struct device_attribute *attr, char *buf)
{
	int rc = 0;
	CDBG("[%s] BEGIN \n", __func__	);
	rc = scnprintf(buf, PAGE_SIZE, "%d\n", 0);

	fctrl.func_tbl->flash_led_high(&fctrl);

	CDBG("[%s] END \n", __func__);
	return rc;
}
static ssize_t lm3644tt_save_flash(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u8 input_value;
	CDBG("[%s] BEGIN Buff=%s \n", __func__, buf);
	if (sscanf(buf, "%hhu", &input_value) != 1) {
		dev_err(dev, "Input value error \n");
		return -EINVAL;
	}
	if (input_value >= 1) {
		fctrl.func_tbl->flash_led_high(&fctrl);
	} else {
		fctrl.func_tbl->flash_led_off(&fctrl);
	}

	CDBG("[%s] END \n", __func__);
	return count;
}
static DEVICE_ATTR(flash, 0664, lm3644tt_show_flash, lm3644tt_save_flash);

static ssize_t lm3644tt_show_t1(struct device *dev, struct device_attribute *attr, char *buf)
{
	int rc = 0;
	CDBG("[%s] BEGIN g_t1=%d \n", __func__, g_t1);
	rc = scnprintf(buf, PAGE_SIZE, "%d\n", g_t1);

	CDBG("[%s] END \n", __func__);
	return rc;
}
static ssize_t lm3644tt_save_t1(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u8 input_value;
	CDBG("[%s] BEGIN Buff=%s \n", __func__, buf);
	if (sscanf(buf, "%hhu", &input_value) != 1) {
		dev_err(dev, "Input value error \n");
		return -EINVAL;
	}
	g_t1 = DecideCurrent(LED_OFF_VAL, MAX_TORCH_CR, input_value);
	fctrl.reg_setting->low_setting->reg_setting[0].reg_data = g_t1;

	CDBG("[%s] END \n", __func__);
	return count;
}
static DEVICE_ATTR(t1, 0664, lm3644tt_show_t1, lm3644tt_save_t1);

static ssize_t lm3644tt_show_t2(struct device *dev, struct device_attribute *attr, char *buf)
{
	int rc = 0;
	CDBG("[%s] BEGIN g_t2=%d \n", __func__, g_t2);
	rc = scnprintf(buf, PAGE_SIZE, "%d\n", g_t2);

	CDBG("[%s] END \n", __func__);
	return rc;
}
static ssize_t lm3644tt_save_t2(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u8 input_value;
	CDBG("[%s] BEGIN Buff=%s \n", __func__, buf);
	if (sscanf(buf, "%hhu", &input_value) != 1) {
		dev_err(dev, "Input value error \n");
		return -EINVAL;
	}
	g_t2 = DecideCurrent(LED_OFF_VAL, MAX_TORCH_CR, input_value);
	fctrl.reg_setting->low_setting->reg_setting[1].reg_data = g_t2;

	CDBG("[%s] END \n", __func__);
	return count;
}
static DEVICE_ATTR(t2, 0664, lm3644tt_show_t2, lm3644tt_save_t2);

static ssize_t lm3644tt_show_torch(struct device *dev, struct device_attribute *attr, char *buf)
{
	int rc = 0;
	CDBG("[%s] BEGIN \n", __func__);
	rc = scnprintf(buf, PAGE_SIZE, "%d\n", 0);

	fctrl.func_tbl->flash_led_low(&fctrl);

	CDBG("[%s] END \n", __func__);
	return rc;
}
static ssize_t lm3644tt_save_torch(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u8 input_value;
	CDBG("[%s] BEGIN Buff=%s \n", __func__, buf);
	if (sscanf(buf, "%hhu", &input_value) != 1) {
		dev_err(dev, "Input value error \n");
		return -EINVAL;
	}
	if (input_value >= 1) {
		fctrl.func_tbl->flash_led_low(&fctrl);
	} else {
		fctrl.func_tbl->flash_led_off(&fctrl);
	}

	CDBG("[%s] END \n", __func__);
	return count;
}
static DEVICE_ATTR(torch, 0664, lm3644tt_show_torch, lm3644tt_save_torch);

static ssize_t lm3644tt_show_id(struct device *dev, struct device_attribute *attr, char *buf)
{
	int rc = 0;
	int ret = 0;
	uint16_t read_data = 255;

	CDBG("[%s] BEGIN \n", __func__);

	lm3644tt_chip_en(1);
	ret = fctrl.flash_i2c_client->i2c_func_tbl->i2c_read(fctrl.flash_i2c_client, REG_DEV_ID, &read_data, MSM_CAMERA_I2C_BYTE_DATA);

	rc = scnprintf(buf, PAGE_SIZE, "%d\n", read_data);

	CDBG("[%s] END read_data=%d ret=%d \n", __func__, read_data, ret);
	return rc;
}
static ssize_t lm3644tt_save_id(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u8 input_value;
	CDBG("[%s] BEGIN Buff=%s \n", __func__, buf);
	if (sscanf(buf, "%hhu", &input_value) != 1) {
		dev_err(dev, "Input value error \n");
		return -EINVAL;
	}

	CDBG("[%s] END \n", __func__);
	return count;
}
static DEVICE_ATTR(id, 0664, lm3644tt_show_id, lm3644tt_save_id);

static struct device_attribute *lm3644tt_class_attrs[] = {
	&dev_attr_ct_lv,
	&dev_attr_f1,
	&dev_attr_f2,
	&dev_attr_flash,
	&dev_attr_t1,
	&dev_attr_t2,
	&dev_attr_torch,
	&dev_attr_id,
};

static int lm3644tt_create_attr(struct device *dev)
{
	int idx, err = 0;
	int num = (int)(sizeof(lm3644tt_class_attrs)/sizeof(lm3644tt_class_attrs[0]));

	CDBG("[%s] BEGIN \n", __func__);
	if (!dev)
		return -EINVAL;

	for (idx = 0; idx < num; idx++)
	{
		if ((err = device_create_file(dev, lm3644tt_class_attrs[idx])))
		{
			//PK_DBG("device_create_file (%s) = %d\n", flashlight_class_attrs[idx]->attr.name, err);
			break;
		}
	}

	CDBG("[%s] END ERR=%d \n", __func__, err);
	return err;
}

u8 g_asus_input_value;
static int flash_lm3644tt_read_asus (char *page, char **start, off_t off, int count,
	int *eof, void *data_unused)
{
	int len = 0;
	CDBG("[%s] BEGIN \n", __func__);

	len = scnprintf(page, 1024, "%d\n", g_asus_input_value);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	 len -= off;
	 if (len > count) len = count;
	 if (len < 0) len = 0;

	CDBG("[%s] END page=%s \n", __func__, page);
	return len;
}
static int flash_lm3644tt_write_asus (struct file *file, const char __user *buffer,
       unsigned long count, void *data)
{
	char buf[] = "0x00000000";
	unsigned long len = min((unsigned long)sizeof(buf) - 1, count);
	u32 torch_cr;

	CDBG("[%s] BEGIN Buff=%s \n", __func__, buffer);
	if (copy_from_user(buf, buffer, len)) return count;
	buf[len] = 0;
	if (sscanf(buf, "%hhu", &g_asus_input_value) != 1) {
		return -EINVAL;
	}

	// input_value == 0: Turn off the LED
	if (g_asus_input_value <= 0) torch_cr = LED_OFF_VAL;
	else if (g_asus_input_value >= 200) torch_cr = MAX_TORCH_CR;
	else {
		torch_cr = (g_asus_input_value * 128) / 200;
	}

	if (torch_cr == LED_OFF_VAL) fctrl.func_tbl->flash_led_off(&fctrl);
	else {
		fctrl.reg_setting->low_setting->reg_setting[0].reg_data = torch_cr;
		fctrl.reg_setting->low_setting->reg_setting[1].reg_data = torch_cr;
		fctrl.func_tbl->flash_led_low(&fctrl);
	}

	CDBG("[%s] END input_value=%d, torch_cr=%d\n", __func__, g_asus_input_value, torch_cr);
	return strnlen(buf, count);;
}
static int flash_lm3644tt_read_led1 (char *page, char **start, off_t off, int count,
	int *eof, void *data_unused)
{
	CDBG("[%s] BEGIN \n", __func__);

	CDBG("[%s] END \n", __func__);
	return 0;
}
static int flash_lm3644tt_write_led1 (struct file *file, const char __user *buffer,
       unsigned long count, void *data)
{
	char buf[] = "0x00000000";
	unsigned long len = min((unsigned long)sizeof(buf) - 1, count);
	u8 input_value;

	CDBG("[%s] BEGIN Buff=%s \n", __func__, buffer);
	if (copy_from_user(buf, buffer, len)) return count;
	buf[len] = 0;
	if (sscanf(buf, "%hhu", &input_value) != 1) {
		return -EINVAL;
	}

	fctrl.reg_setting->low_setting->reg_setting[1].reg_data = LED_OFF_VAL;
	if (input_value <= 0) {
		fctrl.reg_setting->low_setting->reg_setting[0].reg_data = LED_OFF_VAL;
		fctrl.func_tbl->flash_led_off(&fctrl);
	}
	else {
		fctrl.reg_setting->low_setting->reg_setting[0].reg_data = g_torch_cr_map[0][5];
		fctrl.func_tbl->flash_led_low(&fctrl);
	}

	CDBG("[%s] END input_value=%d \n", __func__, input_value);
	return strnlen(buf, count);;
}
static int flash_lm3644tt_read_led2 (char *page, char **start, off_t off, int count,
	int *eof, void *data_unused)
{
	CDBG("[%s] BEGIN \n", __func__);

	CDBG("[%s] END \n", __func__);
	return 0;
}
static int flash_lm3644tt_write_led2 (struct file *file, const char __user *buffer,
       unsigned long count, void *data)
{
	char buf[] = "0x00000000";
	unsigned long len = min((unsigned long)sizeof(buf) - 1, count);
	u8 input_value;

	CDBG("[%s] BEGIN Buff=%s \n", __func__, buffer);
	if (copy_from_user(buf, buffer, len)) return count;
	buf[len] = 0;
	if (sscanf(buf, "%hhu", &input_value) != 1) {
		return -EINVAL;
	}

	fctrl.reg_setting->low_setting->reg_setting[0].reg_data = LED_OFF_VAL;
	if (input_value <= 0) {
		fctrl.reg_setting->low_setting->reg_setting[1].reg_data = LED_OFF_VAL;
		fctrl.func_tbl->flash_led_off(&fctrl);
	}
	else {
		fctrl.reg_setting->low_setting->reg_setting[1].reg_data = g_torch_cr_map[1][5];
		fctrl.func_tbl->flash_led_low(&fctrl);
	}

	CDBG("[%s] END input_value=%d \n", __func__, input_value);
	return strnlen(buf, count);;
}

//ASUS_BSP Bryant: Porting file node for update thermal level. +++
int thermal_value = 0;
static int thermal_eng_read_level (char *page, char **start, off_t off, int count,
	int *eof, void *data_unused)
{
	int len = 0;
	len = scnprintf(page, 5, "%d\n", thermal_value);

	if (len <= off+count) *eof = 1;
	*start = page + off;
	 len -= off;
	 if (len > count) len = count;
	 if (len < 0) len = 0;
	CDBG("[%s]END page=%s, thermal_value=%d \n", __func__, page, thermal_value);

	return len;
}
static int thermal_eng_write_level (struct file *file, const char __user *buffer,
       unsigned long count, void *data)
{
	char buf[] = "0x00000000";
	unsigned long len = min((unsigned long)sizeof(buf) - 1, count);

	CDBG("[%s] BEGIN Buff=%s count=%lu \n", __func__, buffer, count);
	//It's mean error, return count.
	if (copy_from_user(buf, buffer, len)) return count;

	if (sscanf(buf, "%d", &thermal_value) != 1) {
		return -EINVAL;
	}
	CDBG("[%s]END thermal_value=%d \n", __func__, thermal_value);

	return strnlen(buf, len);
}
//ASUS_BSP Bryant: Porting file node for update thermal level. ---

static void __exit msm_flash_lm3644tt_i2c_remove(void)
{
	i2c_del_driver(&lm3644tt_i2c_driver);
	if (proc_asus) remove_proc_entry(PROC_ENTRY_ASUS_FLASH, NULL);
	if (proc_led1) remove_proc_entry(PROC_ENTRY_FLASH_LED1, NULL);
	if (proc_led2) remove_proc_entry(PROC_ENTRY_FLASH_LED2, NULL);
	if (proc_thermal) remove_proc_entry(PROC_ENTRY_FLASH_LED2, NULL);	//ASUS_BSP Bryant: Porting file node for update thermal level.

	return;
}

static const struct of_device_id lm3644tt_i2c_trigger_dt_match[] = {
	{.compatible = FLASH_NAME},
	{}
};

MODULE_DEVICE_TABLE(of, lm3644tt_i2c_trigger_dt_match);

static const struct i2c_device_id lm3644tt_i2c_id[] = {
	{FLASH_NAME, (kernel_ulong_t)&fctrl},
	{ }
};

static void lm3644tt_init_default_value(void)
{
	CDBG("[%s] BEGIN \n", __func__);

	g_f1 = g_flash_cr_map[0][5];
	g_f2 = g_flash_cr_map[1][5];
	g_t1 = g_torch_cr_map[0][5];
	g_t2 = g_torch_cr_map[1][5];
	lm3644tt_high_write_array[0].reg_data = g_f1;
	lm3644tt_high_write_array[1].reg_data = g_f2;
	lm3644tt_low_write_array[0].reg_data = g_t1;
	lm3644tt_low_write_array[1].reg_data = g_t2;

	CDBG("[%s] END \n", __func__);
}

static int msm_flash_lm3644tt_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	if (!id) {
		pr_err("msm_flash_lm3644tt_i2c_probe: id is NULL \n");
		id = lm3644tt_i2c_id;
	}

	lm3644tt_create_attr(&(client->dev));
	lm3644tt_init_default_value();

	return msm_flash_i2c_probe(client, id);
}

static struct i2c_driver lm3644tt_i2c_driver = {
	.id_table = lm3644tt_i2c_id,
	.probe  = msm_flash_lm3644tt_i2c_probe,
	.remove = __exit_p(msm_flash_lm3644tt_i2c_remove),
	.driver = {
		.name = FLASH_NAME,
		.owner = THIS_MODULE,
		.of_match_table = lm3644tt_i2c_trigger_dt_match,
	},
};

static int __init msm_flash_lm3644tt_i2c_add_driver(void)
{
	if ((proc_asus = create_proc_entry(PROC_ENTRY_ASUS_FLASH, S_IRUGO | S_IWUGO, NULL))) {
		proc_asus->read_proc = flash_lm3644tt_read_asus;
		proc_asus->write_proc = flash_lm3644tt_write_asus;
	}
	if ((proc_led1 = create_proc_entry(PROC_ENTRY_FLASH_LED1, S_IRUGO | S_IWUGO, NULL))) {
		proc_led1->read_proc = flash_lm3644tt_read_led1;
		proc_led1->write_proc = flash_lm3644tt_write_led1;
	}
	if ((proc_led2 = create_proc_entry(PROC_ENTRY_FLASH_LED2, S_IRUGO | S_IWUGO, NULL))) {
		proc_led2->read_proc = flash_lm3644tt_read_led2;
		proc_led2->write_proc = flash_lm3644tt_write_led2;
	}
	//ASUS_BSP Bryant: Porting file node for update thermal level. +++
	if ((proc_thermal = create_proc_entry(PROC_ENTRY_THERMAL_LEVEL, S_IRUGO | S_IWUGO, NULL))) {
		proc_thermal->read_proc = thermal_eng_read_level;
		proc_thermal->write_proc = thermal_eng_write_level;
	}
	//ASUS_BSP Bryant: Porting file node for update thermal level. ---

	return i2c_add_driver(&lm3644tt_i2c_driver);
}

static struct msm_camera_i2c_client lm3644tt_i2c_client = {
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
};

static struct msm_camera_i2c_reg_setting lm3644tt_init_setting = {
	.reg_setting = lm3644tt_init_array,
	.size = ARRAY_SIZE(lm3644tt_init_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_camera_i2c_reg_setting lm3644tt_off_setting = {
	.reg_setting = lm3644tt_off_array,
	.size = ARRAY_SIZE(lm3644tt_off_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_camera_i2c_reg_setting lm3644tt_release_setting = {
	.reg_setting = lm3644tt_release_array,
	.size = ARRAY_SIZE(lm3644tt_release_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_camera_i2c_reg_setting lm3644tt_low_setting = {
	.reg_setting = lm3644tt_low_array,
	.size = ARRAY_SIZE(lm3644tt_low_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_camera_i2c_reg_setting lm3644tt_high_setting = {
	.reg_setting = lm3644tt_high_array,
	.size = ARRAY_SIZE(lm3644tt_high_array),
	.addr_type = MSM_CAMERA_I2C_BYTE_ADDR,
	.data_type = MSM_CAMERA_I2C_BYTE_DATA,
	.delay = 0,
};

static struct msm_led_flash_reg_t lm3644tt_regs = {
	.init_setting = &lm3644tt_init_setting,
	.off_setting = &lm3644tt_off_setting,
	.low_setting = &lm3644tt_low_setting,
	.high_setting = &lm3644tt_high_setting,
	.release_setting = &lm3644tt_release_setting,
};

int lm3644tt_flash_led_init(struct msm_led_flash_ctrl_t *fctrl)
{
	int rc = 0;

	CDBG("[%s] BEGIN \n", __func__);

	lm3644tt_chip_en(1);
	if (fctrl->flash_i2c_client && fctrl->reg_setting) {
		rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write_table(
			fctrl->flash_i2c_client,
			fctrl->reg_setting->init_setting);
		if (rc < 0)
			pr_err("%s:%d failed\n", __func__, __LINE__);
	}
	lm3644tt_chip_en(0);

	CDBG("[%s] END rc=%d \n", __func__, rc);
	return rc;
}

int lm3644tt_flash_led_release(struct msm_led_flash_ctrl_t *fctrl)
{
	CDBG("[%s] BEGIN \n", __func__);

	lm3644tt_chip_en(0);

	CDBG("[%s] END \n", __func__);
	return 0;
}

int lm3644tt_flash_led_off(struct msm_led_flash_ctrl_t *fctrl)
{
	int rc = 0;

	CDBG("[%s] BEGIN \n", __func__);

	if (!fctrl) {
		pr_err("%s:%d fctrl NULL\n", __func__, __LINE__);
		return -EINVAL;
	}
	lm3644tt_chip_en(1);
	if (fctrl->flash_i2c_client && fctrl->reg_setting) {
		rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write_table(
			fctrl->flash_i2c_client,
			fctrl->reg_setting->off_setting);
		if (rc < 0)
			pr_err("%s:%d failed\n", __func__, __LINE__);
	}
	lm3644tt_chip_en(0);

	CDBG("[%s] END rc=%d \n", __func__, rc);
	return rc;
}

int lm3644tt_flash_led_low(struct msm_led_flash_ctrl_t *fctrl)
{
	int rc = 0;
	uint16_t enable_val = DEFAULT_TORCH_VAL;

	CDBG("[%s] BEGIN, enable_val=0x%x \n", __func__, enable_val);

	if (!fctrl) {
		pr_err("%s:%d fctrl NULL\n", __func__, __LINE__);
		return -EINVAL;
	}
	if (!fctrl->reg_setting) {
		pr_err("%s:%d fctrl->reg_setting NULL\n", __func__, __LINE__);
		return -EINVAL;
	}

	// Turn Each LED on/off if it was set as LED_OFF_VAL
	g_led1_en = fctrl->reg_setting->low_setting->reg_setting[0].reg_data == LED_OFF_VAL ? 0 : 1;
	g_led2_en = fctrl->reg_setting->low_setting->reg_setting[1].reg_data == LED_OFF_VAL ? 0 : 1;
	enable_val += g_led1_en * 1 + g_led2_en * 2;
	fctrl->reg_setting->low_setting->reg_setting[3].reg_data = enable_val;

	lm3644tt_low_write_array[0].reg_data = lm3644tt_remove_1st_bit_of_current(fctrl->reg_setting->low_setting->reg_setting[0].reg_data);
	lm3644tt_low_write_array[1].reg_data = lm3644tt_remove_1st_bit_of_current(fctrl->reg_setting->low_setting->reg_setting[1].reg_data);
	lm3644tt_low_write_array[3].reg_data = enable_val;
	fctrl->reg_setting->low_setting->reg_setting = lm3644tt_low_write_array;
//	CDBG("[%s] write 0x%x to 0x%x \n", __func__, fctrl->reg_setting->low_setting->reg_setting[2].reg_data, fctrl->reg_setting->low_setting->reg_setting[2].reg_addr);
	CDBG("joseph [%s] low[0].reg_data=%d, low[1].reg_data=%d\n",  __func__, fctrl->reg_setting->low_setting->reg_setting[0].reg_data,  fctrl->reg_setting->low_setting->reg_setting[1].reg_data);

	lm3644tt_chip_en(1);
	if (fctrl->flash_i2c_client && fctrl->reg_setting) {
		rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write_table(
			fctrl->flash_i2c_client,
			fctrl->reg_setting->low_setting);
		if (rc < 0)
			pr_err("%s:%d failed\n", __func__, __LINE__);
	}
	fctrl->reg_setting->low_setting->reg_setting = lm3644tt_low_array;

	CDBG("[%s] END rc=%d \n", __func__, rc);
	return rc;
}

int lm3644tt_flash_led_high(struct msm_led_flash_ctrl_t *fctrl)
{
	int rc = 0;
	uint16_t enable_val = DEFAULT_FLASH_VAL;

	CDBG("[%s] BEGIN, enable_val=0x%x \n", __func__, enable_val);
	if (!fctrl) {
		pr_err("%s:%d fctrl NULL\n", __func__, __LINE__);
		return -EINVAL;
	}
	if (!fctrl->reg_setting) {
		pr_err("%s:%d fctrl->reg_setting NULL\n", __func__, __LINE__);
		return -EINVAL;
	}

	// Turn Each LED on/off if it was set as LED_OFF_VAL
	g_led1_en = fctrl->reg_setting->high_setting->reg_setting[0].reg_data == LED_OFF_VAL ? 0 : 1;
	g_led2_en = fctrl->reg_setting->high_setting->reg_setting[1].reg_data == LED_OFF_VAL ? 0 : 1;
	enable_val += g_led1_en * 1 + g_led2_en * 2;
	fctrl->reg_setting->high_setting->reg_setting[3].reg_data = enable_val;

	lm3644tt_high_write_array[0].reg_data = lm3644tt_remove_1st_bit_of_current(fctrl->reg_setting->high_setting->reg_setting[0].reg_data);
	lm3644tt_high_write_array[1].reg_data = lm3644tt_remove_1st_bit_of_current(fctrl->reg_setting->high_setting->reg_setting[1].reg_data);
	lm3644tt_high_write_array[3].reg_data = enable_val;
	fctrl->reg_setting->high_setting->reg_setting = lm3644tt_high_write_array;
//	CDBG("[%s] write 0x%x to 0x%x \n", __func__, fctrl->reg_setting->high_setting->reg_setting[2].reg_data, fctrl->reg_setting->high_setting->reg_setting[2].reg_addr);
    CDBG("joseph [%s] high[0].reg_data=%d, high[1].reg_data=%d\n",  __func__, fctrl->reg_setting->high_setting->reg_setting[0].reg_data,  fctrl->reg_setting->high_setting->reg_setting[1].reg_data);

	lm3644tt_chip_en(1);
	if (fctrl->flash_i2c_client && fctrl->reg_setting) {
		rc = fctrl->flash_i2c_client->i2c_func_tbl->i2c_write_table(
			fctrl->flash_i2c_client,
			fctrl->reg_setting->high_setting);
		if (rc < 0)
			pr_err("%s:%d failed\n", __func__, __LINE__);
	}
	fctrl->reg_setting->high_setting->reg_setting = lm3644tt_high_array;

	CDBG("[%s] END rc=%d \n", __func__, rc);
	return rc;
}

static struct msm_flash_fn_t lm3644tt_func_tbl = {
	.flash_get_subdev_id = msm_led_i2c_trigger_get_subdev_id,
	.flash_led_config = msm_led_i2c_trigger_config,
	.flash_led_init = lm3644tt_flash_led_init,
	.flash_led_release = lm3644tt_flash_led_release,
	.flash_led_off = lm3644tt_flash_led_off,
	.flash_led_low = lm3644tt_flash_led_low,
	.flash_led_high = lm3644tt_flash_led_high,
};

static struct msm_led_flash_ctrl_t fctrl = {
	.flash_i2c_client = &lm3644tt_i2c_client,
	.reg_setting = &lm3644tt_regs,
	.func_tbl = &lm3644tt_func_tbl,
};

/*subsys_initcall(msm_flash_i2c_add_driver);*/
module_init(msm_flash_lm3644tt_i2c_add_driver);
module_exit(msm_flash_lm3644tt_i2c_remove);
MODULE_DESCRIPTION("lm3644tt FLASH");
MODULE_LICENSE("GPL v2");
