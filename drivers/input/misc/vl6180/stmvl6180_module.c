/*
 *  stmvl6180.c - Linux kernel modules for STM VL6180 FlightSense Time-of-Flight sensor
 *
 *  Copyright (C) 2014 STMicroelectronics Imaging Division.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/time.h>
#include <linux/pwm.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/clk.h>
#include <linux/of_gpio.h>

//API includes
#include "vl6180x_api.h"
#include "vl6180x_def.h"
#include "vl6180x_platform.h"
#include "stmvl6180.h"
#include "vl6180x_i2c.h"

#include <linux/proc_fs.h>

//#define VL6180_LOG(fmt, args...) {}
#define VL6180_LOG(fmt, args...) printk("[VL6180]: [%s]:[%d]" fmt "\n", __func__, __LINE__, ##args)
#define VL6180_ERR(fmt, args...) printk("[VL6180]: [%s]:[%d]" fmt "\n", __func__, __LINE__, ##args)


#define LASER_FOCUS_ON_PATH "driver/LaserFocus_on"
#define LASER_FOCUS_VALUE "driver/LaserFocus_value"
#define LASER_FOCUS_VALUE_MORE_INFO "driver/LaserFocus_value_more_info"
#define LASER_FOCUS_STATUS_FOR_CAMERA "driver/LaserFocus_Status_For_Camera"

static struct proc_dir_entry *proc_laser_on;
static struct proc_dir_entry *proc_laser_value;
static struct proc_dir_entry *proc_laser_value_more;
static struct proc_dir_entry *proc_laser_status;
extern bool laser_enable;

stmvl6180x_dev vl6180x_dev;
//#define USE_INT
#define USE_RESET
#define IRQ_NUM	20
#define RESET_GPIO	14
#define VL6180_I2C_ADDRESS  (0x52>>1)
//static struct i2c_client *client;
/*
 * Global data
 */
//******************************** IOCTL definitions
#define VL6180_IOCTL_INIT 		_IO('p', 0x01)
#define VL6180_IOCTL_XTALKCALB		_IO('p', 0x02)
#define VL6180_IOCTL_OFFCALB		_IO('p', 0x03)
#define VL6180_IOCTL_STOP		_IO('p', 0x05)
#define VL6180_IOCTL_SETXTALK		_IOW('p', 0x06, unsigned int)
#define VL6180_IOCTL_SETOFFSET		_IOW('p', 0x07, int8_t)
#define VL6180_IOCTL_GETDATA 		_IOR('p', 0x0a, unsigned long)
#define VL6180_IOCTL_GETDATAS 		_IOR('p', 0x0b, VL6180x_RangeData_t)
struct mutex	  vl6180_mutex;
#define MULTI_READ	     1
#define CALIBRATION_FILE 1
#ifdef CALIBRATION_FILE
int8_t offset_calib=0;
int16_t xtalk_calib=0;
#endif
#ifdef MULTI_READ	
static uint32_t get_unsigned_int_from_buffer(uint8_t *pdata, int8_t count);
static uint16_t get_unsigned_short_from_buffer(uint8_t *pdata, int8_t count);
static int stmvl6180_ps_read_result(struct i2c_client *client);
static void stmvl6180_ps_parse_result(struct i2c_client *client);
#endif
extern void i2c_setclient(struct i2c_client *client);

static int log_count = 0;//ASUS BSP Ryan +++ reduce log

extern struct i2c_client* i2c_getclient(void);

#define CAL_OFFSET_PATH "/factory/sensors/laser_10"
#define CAL_XTALK_PATH  "/factory/sensors/laser_40"






static void normal_mode(void);

static void stop_mode(void);
static void cal_offset_mode(void);
static void cal_xtalk_mode(void);


#define N_MEASURE_AVG 10 //the total number that sensor read when calibration
#define XTALK_DISTANCE 400

static ssize_t stmvl6180_show_cal_10cm(struct device *dev,
				struct device_attribute *attr, char *buf)
{

	int average=0,offset;
	int loop_i,loop_j;
	int ret;
	struct i2c_client *client = to_i2c_client(dev);
	struct stmvl6180_data *data = i2c_get_clientdata(client);
	int running_status =0;

	if(data->enable_ps_sensor)
		running_status = 1;

	stop_mode();
	cal_offset_mode();

	for(loop_i=0;loop_i<N_MEASURE_AVG;loop_i++)
	{

		for(loop_j=0;loop_j<10;loop_j++)
		{
			mdelay(10);
			ret = stmvl6180_ps_read_result(client);
			if (ret == 0 && ((data->ResultBuffer[0]&0x01) == 0x01))
			{
				if( data->enable_ps_sensor)
				{
					stmvl6180_ps_parse_result(client);
					VL6180x_RangeGetMeasurement_ext(vl6180x_dev, &(data->rangeResult), &(data->rangeData));
					//stmvl6180_ps_read_measurement(client);
					pr_err("OffsetCal range:%d, signalrate_mcps:%d, error:0x%x,rtnsgnrate:%u, rtnambrate:%u,rtnconvtime:%u\n",
								data->rangeData.range_mm,
								data->rangeData.signalRate_mcps,
								data->rangeData.errorStatus,
								data->rangeData.rtnRate,
								data->rangeData.rtnAmbRate,
								data->rangeData.rtnConvTime);
					if(data->rangeData.errorStatus)
					{
						printk("Status error\n");
						continue;
					}
				}
				break;
			}else
			{
				//printk("continue\n");
				continue;

			}
		}

		average+=data->rangeData.range_mm;
		VL6180x_RangeSetSystemMode(vl6180x_dev, MODE_START_STOP | MODE_SINGLESHOT);
	}
	average = average / N_MEASURE_AVG;
	offset = (100 -average);
	printk("OffsetCal avg read data %d\n",offset);
	VL6180x_SetOffset(vl6180x_dev, offset);
	//normal_mode();

	offset = VL6180x_GetOffsetCalibrationData(vl6180x_dev);
	printk("OffsetCal read offset data %d\n",offset);
//	status = VL6180x_RdByte(vl6180x_dev,SYSRANGE_PART_TO_PART_RANGE_OFFSET, (uint8_t*)&cal_data);
//	if( status ){
//		printk("SYSRANGE_PART_TO_PART_RANGE_OFFSET rd fail");
//	}

	if(running_status ==1)
	{
		stop_mode();
		normal_mode();
	}

	return sprintf(buf, "%d\n", offset);
}

//for als integration time setup
static ssize_t stmvl6180_store_cal_10cm(struct device *dev,
					struct device_attribute *attr, const char *buf, size_t count)
{

	//if ( on ==1)
	{
		int average=0,offset;
		int loop_i,loop_j;
		int ret;
		struct i2c_client *client = to_i2c_client(dev);
		struct stmvl6180_data *data = i2c_get_clientdata(client);
		stop_mode();
		cal_offset_mode();

		for(loop_i=0;loop_i<N_MEASURE_AVG;loop_i++)
		{

			for(loop_j=0;loop_j<10;loop_j++)
			{
				mdelay(10);
				ret = stmvl6180_ps_read_result(client);
				if (ret == 0 && ((data->ResultBuffer[0]&0x01) == 0x01))
				{
					if( data->enable_ps_sensor)
					{
						stmvl6180_ps_parse_result(client);
						VL6180x_RangeGetMeasurement_ext(vl6180x_dev, &(data->rangeResult), &(data->rangeData));
						//stmvl6180_ps_read_measurement(client);
						pr_err("OffsetCal range:%d, signalrate_mcps:%d, error:0x%x,rtnsgnrate:%u, rtnambrate:%u,rtnconvtime:%u\n",
									data->rangeData.range_mm,
									data->rangeData.signalRate_mcps,
									data->rangeData.errorStatus,
									data->rangeData.rtnRate,
									data->rangeData.rtnAmbRate,
									data->rangeData.rtnConvTime);
						if(data->rangeData.errorStatus)
						{
							printk("Status error\n");
							continue;
						}
					}
					break;
				}else
				{
					//printk("continue\n");
					continue;
				}
			}

			average+=data->rangeData.range_mm;
			VL6180x_RangeSetSystemMode(vl6180x_dev, MODE_START_STOP | MODE_SINGLESHOT);
		}
		average = average / N_MEASURE_AVG;
		offset = (100 -average);
		printk("OffsetCal avg read data %d\n",offset);
		VL6180x_SetOffset(vl6180x_dev, offset);

	}

	return count;
}

//DEVICE_ATTR(name,mode,show,store)
static DEVICE_ATTR(cal_10cm, S_IWUSR | S_IRUGO,
				   stmvl6180_show_cal_10cm, stmvl6180_store_cal_10cm);



static ssize_t stmvl6180_show_cal_40cm(struct device *dev,
				struct device_attribute *attr, char *buf)
{

	int status;
	uint16_t xtalkrate;
	int loop_i,loop_j,ret;
	//VL6180x_RangeData_t pRangeData;
	int  RangeSum=0;
    int RateSum=0;
    int XTalkInt;
	struct i2c_client *client = to_i2c_client(dev);
	struct stmvl6180_data *data = i2c_get_clientdata(client);
	int running_status =0;

	if(data->enable_ps_sensor)
		running_status = 1;

	stop_mode();
	cal_xtalk_mode();

	for(loop_i=0;loop_i<N_MEASURE_AVG;loop_i++)
	{

		for(loop_j=0;loop_j<10;loop_j++)
		{
			mdelay(10);
			ret = stmvl6180_ps_read_result(client);
			if (ret == 0 && ((data->ResultBuffer[0]&0x01) == 0x01))
			{
				if( data->enable_ps_sensor)
				{
					stmvl6180_ps_parse_result(client);
					VL6180x_RangeGetMeasurement_ext(vl6180x_dev, &(data->rangeResult), &(data->rangeData));
					//stmvl6180_ps_read_measurement(client);
					pr_err("xtalkCal range:%d, signalrate_mcps:%d, error:0x%x,rtnsgnrate:%u, rtnambrate:%u,rtnconvtime:%u\n",
								data->rangeData.range_mm,
								data->rangeData.signalRate_mcps,
								data->rangeData.errorStatus,
								data->rangeData.rtnRate,
								data->rangeData.rtnAmbRate,
								data->rangeData.rtnConvTime);
					if(data->rangeData.errorStatus)
					{
						printk("Status error\n");
						continue;
					}
				}
				break;
			}else
			{
				//printk("continue\n");
				continue;
			}
		}
	   RangeSum+=data->rangeData.range_mm;
	   RateSum+=data->rangeData.signalRate_mcps;

	   VL6180x_RangeSetSystemMode(vl6180x_dev, MODE_START_STOP | MODE_SINGLESHOT);
	}

    XTalkInt = RateSum *( N_MEASURE_AVG * XTALK_DISTANCE  - RangeSum )
    		/ N_MEASURE_AVG
    		/ (XTALK_DISTANCE * N_MEASURE_AVG);
    XTalkInt = (XTalkInt>0) ? XTalkInt : 0; // Must be positive of null
    printk("xtalkCal range %d rate %d comp %d\n", RangeSum/N_MEASURE_AVG, RateSum/N_MEASURE_AVG, XTalkInt);
    VL6180x_SetUserXTalkCompensationRate(vl6180x_dev, XTalkInt);
    VL6180x_SetXTalkCompensationRate(vl6180x_dev, XTalkInt);
	//normal_mode();


	status = VL6180x_RdWord(vl6180x_dev,SYSRANGE_CROSSTALK_COMPENSATION_RATE, &xtalkrate);
	if( status ){
		printk("SYSRANGE_CROSSTALK_COMPENSATION_RATE rd fail");
	}
	printk("xtalkCal xtalk %d \n", xtalkrate);

	if(running_status ==1)
	{
		stop_mode();
		normal_mode();
	}

	return sprintf(buf, "%d\n", xtalkrate);
}

//for als integration time setup
static ssize_t stmvl6180_store_cal_40cm(struct device *dev,
					struct device_attribute *attr, const char *buf, size_t count)
{

	long on = simple_strtol(buf, NULL, 10);

	if ( on ==1)
	{
		int loop_i,loop_j,ret;
		//VL6180x_RangeData_t pRangeData;
		int  RangeSum=0;
	    int RateSum=0;
	    int XTalkInt;
		struct i2c_client *client = to_i2c_client(dev);
		struct stmvl6180_data *data = i2c_get_clientdata(client);

		stop_mode();
		cal_xtalk_mode();
		//TODO CLEAR XTALK REG

		for(loop_i=0;loop_i<N_MEASURE_AVG;loop_i++)
		{

			for(loop_j=0;loop_j<10;loop_j++)
			{
				mdelay(10);
				ret = stmvl6180_ps_read_result(client);
				if (ret == 0 && ((data->ResultBuffer[0]&0x01) == 0x01))
				{
					if( data->enable_ps_sensor)
					{
						stmvl6180_ps_parse_result(client);
						VL6180x_RangeGetMeasurement_ext(vl6180x_dev, &(data->rangeResult), &(data->rangeData));
						//stmvl6180_ps_read_measurement(client);
						pr_err("xtalkCal range:%d, signalrate_mcps:%d, error:0x%x,rtnsgnrate:%u, rtnambrate:%u,rtnconvtime:%u\n",
									data->rangeData.range_mm,
									data->rangeData.signalRate_mcps,
									data->rangeData.errorStatus,
									data->rangeData.rtnRate,
									data->rangeData.rtnAmbRate,
									data->rangeData.rtnConvTime);
						if(data->rangeData.errorStatus)
						{
							printk("Status error\n");
							continue;
						}
					}
					break;
				}else
				{
					//printk("continue\n");
					continue;

				}
			}
		   RangeSum+=data->rangeData.range_mm;
		   RateSum+=data->rangeData.signalRate_mcps;

		   VL6180x_RangeSetSystemMode(vl6180x_dev, MODE_START_STOP | MODE_SINGLESHOT);
		}

	    XTalkInt = RateSum *( N_MEASURE_AVG * XTALK_DISTANCE  - RangeSum )
	    		/ N_MEASURE_AVG
	    		/ (XTALK_DISTANCE * N_MEASURE_AVG);
	    XTalkInt = (XTalkInt>0) ? XTalkInt : 0; // Must be positive of null
	    printk("xtalkCal range %d rate %d comp %d\n", RangeSum/N_MEASURE_AVG, RateSum/N_MEASURE_AVG, XTalkInt);
            VL6180x_SetUserXTalkCompensationRate(vl6180x_dev, XTalkInt);
	    VL6180x_SetXTalkCompensationRate(vl6180x_dev, XTalkInt);

	}


	return count;
}

//DEVICE_ATTR(name,mode,show,store)
static DEVICE_ATTR(cal_40cm, S_IWUSR | S_IRUGO,
				   stmvl6180_show_cal_40cm, stmvl6180_store_cal_40cm);

struct regulator *vl6180_vana = NULL,*vl6180_vio = NULL,*vl6180_vana2 = NULL;

static int stmvl6180_power(struct i2c_client *client, int on_off)
{
	int retval;

	if(on_off)
	{
		if (vl6180_vio==NULL)
		{
			//i2c powers
			vl6180_vio = regulator_get(&client->dev, "vio");
			if (IS_ERR(vl6180_vio)) {
				pr_err("In %s, vio supply is not provided\n", __func__);
				vl6180_vio = NULL;
			} else {
				pr_err("In %s, vio supply is SUCCESS\n", __func__);
				retval = regulator_set_voltage(vl6180_vio,
						1800000,1800000);
				if (retval < 0) {
					pr_err("set_vol(%p) fail %d\n",vl6180_vio , retval);
				}

				retval = regulator_enable(vl6180_vio);
				if (retval < 0) {
					pr_err("reg enable(%p) failed.rc=%d\n", vl6180_vio, retval);
				}
			}
		}//if (vl6180_vio==NULL)

		if (vl6180_vana==NULL)
		{
			vl6180_vana = regulator_get(&client->dev, "vdd");
			if (IS_ERR(vl6180_vana)) {
				pr_err("In %s, vdd supply is not provided\n", __func__);
				vl6180_vana = NULL;
			} else {
				pr_err("In %s, vdd supply is SUCCESS\n", __func__);
				retval = regulator_set_voltage(vl6180_vana,
						2850000,2850000);
				if (retval < 0) {
					pr_err("set_vol(%p) fail %d\n",vl6180_vana , retval);
				}

				retval = regulator_enable(vl6180_vana);
				if (retval < 0) {
					pr_err("reg enable(%p) failed.rc=%d\n", vl6180_vana, retval);
				}
			}
		}//if (vl6180_vana==NULL)

		if (vl6180_vana2==NULL)
		{
			vl6180_vana2 = regulator_get(&client->dev, "vdd2");
			if (IS_ERR(vl6180_vana2)) {
				pr_err("In %s, vdd supply is not provided\n", __func__);
				vl6180_vana2 = NULL;
			} else {
				pr_err("In %s, vdd supply is SUCCESS\n", __func__);
				retval = regulator_set_voltage(vl6180_vana2,
						2850000,2850000);
				if (retval < 0) {
					pr_err("set_vol(%p) fail %d\n",vl6180_vana2 , retval);
				}

				retval = regulator_enable(vl6180_vana2);
				if (retval < 0) {
					pr_err("reg enable(%p) failed.rc=%d\n", vl6180_vana2, retval);
				}
			}
		}//if (vl6180_vana2==NULL)

	}else
	{
		if (vl6180_vio!=NULL)
		{
			retval = regulator_disable(vl6180_vio);
			if (retval < 0) {
				pr_err("reg disable(%p) failed.rc=%d\n", vl6180_vio, retval);
			}
			regulator_put(vl6180_vio);
			vl6180_vio = NULL;
		}

		if (vl6180_vana!=NULL)
		{
			retval = regulator_disable(vl6180_vana);
			if (retval < 0) {
				pr_err("reg disable(%p) failed.rc=%d\n", vl6180_vana, retval);
			}
			regulator_put(vl6180_vana);
			vl6180_vana = NULL;
		}

		if (vl6180_vana2!=NULL)
		{
			retval = regulator_disable(vl6180_vana2);
			if (retval < 0) {
				pr_err("reg disable(%p) failed.rc=%d\n", vl6180_vana2, retval);
			}
			regulator_put(vl6180_vana2);
			vl6180_vana2 = NULL;
		}

	}
	return 0;
}

static int stmvl6180_set_enable(struct i2c_client *client, unsigned int enable)
{
#ifdef USE_RESET
	printk("enable %d\n",enable);
       printk("[STMVL6180] enable %d\n",enable);
	if(enable)
	{
		stmvl6180_power(client,1);
		msleep(5);
		gpio_set_value_cansleep(RESET_GPIO, 1);
	}else
	{
		gpio_set_value_cansleep(RESET_GPIO, 0);
		stmvl6180_power(client,0);
	}
#endif
	return 0;

}
#ifdef CALIBRATION_FILE
static void stmvl6180_read_calibration_file(void)
{
	struct file *f;
	char buf[8];
	mm_segment_t fs;
	int i,is_sign=0;
	f = filp_open(CAL_OFFSET_PATH, O_RDONLY, 0);
	if (f!= NULL && !IS_ERR(f) && f->f_dentry!=NULL)
	{
		fs = get_fs();
		set_fs(get_ds());
		//init the buffer with 0
		for (i=0;i<8;i++)
			buf[i]=0;
		f->f_op->read(f, buf, 8, &f->f_pos);
		set_fs(fs);
		printk("offset as:%s, buf[0]:%c\n",buf, buf[0]);
		for (i=0;i<8;i++)
		{
			if (i==0 && buf[0]=='-')
				is_sign=1;
			else if (buf[i]>='0' && buf[i]<='9')
				offset_calib = offset_calib*10 + (buf[i]-'0');
			else
				break;
		}
		if (is_sign==1)
			offset_calib=-offset_calib;
		printk("offset_calib as %d\n", offset_calib);
		VL6180x_SetUserOffsetCalibration(vl6180x_dev, offset_calib);
		filp_close(f, NULL);
	}
	else
		printk("no offset calibration file exist!\n");


	is_sign=0;
	f = filp_open(CAL_XTALK_PATH, O_RDONLY, 0);
	if (f!= NULL && !IS_ERR(f) && f->f_dentry!=NULL)
	{
		fs = get_fs();
		set_fs(get_ds());
		//init the buffer with 0
		for (i=0;i<8;i++)
			buf[i]=0;
		f->f_op->read(f, buf, 8, &f->f_pos);
		set_fs(fs);
		printk("xtalk as:%s, buf[0]:%c\n",buf, buf[0]);
		for (i=0;i<8;i++)
		{
			if (i==0 && buf[0]=='-')
				is_sign=1;
			else if (buf[i]>='0' && buf[i]<='9')
				xtalk_calib = xtalk_calib*10 + (buf[i]-'0');
			else 
				break;
		}
		if (is_sign==1)
			xtalk_calib = -xtalk_calib;
		printk("xtalk_calib as %d\n", xtalk_calib);
		VL6180x_SetUserXTalkCompensationRate(vl6180x_dev, xtalk_calib);
		filp_close(f, NULL);
	}
	else
		printk("no xtalk calibration file exist!\n");

	return;
}
static void stmvl6180_write_offset_calibration_file(void)
{
	struct file *f;
	char buf[8];
	mm_segment_t fs;

	f = filp_open(CAL_OFFSET_PATH, O_WRONLY|O_CREAT, 0644);
	if (f!= NULL)
	{
		fs = get_fs();
		set_fs(get_ds());
		sprintf(buf,"%d",offset_calib);
		printk("write offset as:%s, buf[0]:%c\n",buf, buf[0]);
		f->f_op->write(f, buf, 8, &f->f_pos);
		set_fs(fs);
		VL6180x_SetUserOffsetCalibration(vl6180x_dev, offset_calib);
	}
	filp_close(f, NULL);

	return;
}
static void stmvl6180_write_xtalk_calibration_file(void)
{
	struct file *f;
	char buf[8];
	mm_segment_t fs;

	f = filp_open(CAL_XTALK_PATH, O_WRONLY|O_CREAT, 0644);
	if (f!= NULL)
	{
		fs = get_fs();
		set_fs(get_ds());
		sprintf(buf,"%d",xtalk_calib);
		printk("write xtalk as:%s, buf[0]:%c\n",buf, buf[0]);
		f->f_op->write(f, buf, 8, &f->f_pos);
		set_fs(fs);
		VL6180x_SetUserXTalkCompensationRate(vl6180x_dev, xtalk_calib);
	}
	filp_close(f, NULL);

	return;
}

#endif
#ifdef MULTI_READ	
static uint32_t get_unsigned_int_from_buffer(uint8_t *pdata, int8_t count)
{
	uint32_t value=0;
	while (count-- > 0)
	{
		value = (value << 8) | (uint32_t)*pdata++;
	}
	return value;
}

static uint16_t get_unsigned_short_from_buffer(uint8_t *pdata, int8_t count)
{
	uint16_t value=0;
	while (count-- > 0)
	{
		value = (value << 8) | (uint16_t)*pdata++;
	}
	return value;
}
static int stmvl6180_ps_read_result(struct i2c_client *client)
{
	struct stmvl6180_data *data = i2c_get_clientdata(client);
	int status=0;
	status = VL6180x_RdBuffer(vl6180x_dev, RESULT_RANGE_STATUS , data->ResultBuffer,RESULT_REG_COUNT);
	return status;
}
static void stmvl6180_ps_parse_result(struct i2c_client *client)
{
	struct stmvl6180_data *data = i2c_get_clientdata(client);
	
	//RESULT_RANGE_STATUS:0x004D
	data->rangeResult.Result_range_status = data->ResultBuffer[0];
	//RESULT_INTERRUPT_STATUS:0x004F
	data->rangeResult.Result_interrupt_status = data->ResultBuffer[1];
	//RESULT_RANGE_VAL:0x0062
	data->rangeResult.Result_range_val = data->ResultBuffer[(0x62-0x4d)];
	//RESULT_RANGE_RAW:0x0064
	data->rangeResult.Result_range_raw = data->ResultBuffer[(0x64-0x4d)];
	//RESULT_RANGE_RETURN_RATE:0x0066
	data->rangeResult.Result_range_return_rate = get_unsigned_short_from_buffer(data->ResultBuffer+(0x66-0x4d),2);
	//RESULT_RANGE_REFERENCE_RATE:0x0068
	data->rangeResult.Result_range_reference_rate = get_unsigned_short_from_buffer(data->ResultBuffer+(0x68-0x4d),2);
	//RESULT_RANGE_RETURN_SIGNAL_COUNT:0x006c
	data->rangeResult.Result_range_return_signal_count = get_unsigned_int_from_buffer(data->ResultBuffer+(0x6c-0x4d),4);
	//RESULT_RANGE_REFERENCE_SIGNAL_COUNT:0x0070
	data->rangeResult.Result_range_reference_signal_count = get_unsigned_int_from_buffer(data->ResultBuffer+(0x70-0x4d),4);
	//RESULT_RANGE_RETURN_AMB_COUNT:0x0074
	data->rangeResult.Result_range_return_amb_count = get_unsigned_int_from_buffer(data->ResultBuffer+(0x74-0x4d),4);
	//RESULT_RANGE_REFERENCE_AMB_COUNT:0x0078
	data->rangeResult.Result_range_reference_amb_count = get_unsigned_int_from_buffer(data->ResultBuffer+(0x78-0x4d),4);
	//RESULT_RANGE_RETURN_CONV_TIME:0x007c
	data->rangeResult.Result_range_return_conv_time = get_unsigned_int_from_buffer(data->ResultBuffer+(0x7c-0x4d),4);
	//RESULT_RANGE_REFERENCE_CONV_TIME:0x0080
	data->rangeResult.Result_range_reference_conv_time = get_unsigned_int_from_buffer(data->ResultBuffer+(0x80-0x4d),4);
	
	return;	
}
#endif
static void stmvl6180_ps_read_measurement(struct i2c_client *client)
{
	struct stmvl6180_data *data = i2c_get_clientdata(client);
	struct timeval tv;
       static int log_count = 20;
#ifdef MULTI_READ
	VL6180x_RangeGetMeasurement_ext(vl6180x_dev, &(data->rangeResult), &(data->rangeData));
#else
	VL6180x_RangeGetMeasurement(vl6180x_dev, &(data->rangeData));
#endif
	do_gettimeofday(&tv);	

	data->ps_data = data->rangeData.range_mm;		
	if(! ((data->rangeData.errorStatus == 0x10)||(data->rangeData.errorStatus == 0xd)))
	{
		input_report_abs(data->input_dev_ps, ABS_DISTANCE,(int)(data->ps_data+5)/10);
		//input_report_abs(data->input_dev_ps, ABS_HAT0X,data->rangeData.range_mm);
		//input_report_abs(data->input_dev_ps, ABS_X,data->rangeData.signalRate_mcps);
		input_report_abs(data->input_dev_ps, ABS_HAT0X,tv.tv_sec);
		input_report_abs(data->input_dev_ps, ABS_HAT0Y,tv.tv_usec);
		input_report_abs(data->input_dev_ps, ABS_HAT1X,data->rangeData.range_mm);
		input_report_abs(data->input_dev_ps, ABS_HAT1Y,data->rangeData.errorStatus);
	#ifdef VL6180x_HAVE_RATE_DATA
		input_report_abs(data->input_dev_ps, ABS_HAT2X,data->rangeData.signalRate_mcps);
		input_report_abs(data->input_dev_ps, ABS_HAT2Y,data->rangeData.rtnAmbRate);
		input_report_abs(data->input_dev_ps, ABS_HAT3X,data->rangeData.rtnConvTime);
	#endif
	#if  VL6180x_HAVE_DMAX_RANGING
		input_report_abs(data->input_dev_ps, ABS_HAT3Y,data->rangeData.DMax);
	#endif
		input_sync(data->input_dev_ps);
	}
#if 1

	//if (1){
	log_count ++;
	if (log_count >= 20){
		pr_err("[STMVL6180]range:%d, signalrate_mcps:%d, error:0x%x,rtnsgnrate:%u, rtnambrate:%u,rtnconvtime:%u\n", 
			data->rangeData.range_mm,
			data->rangeData.signalRate_mcps,
			data->rangeData.errorStatus,
			data->rangeData.rtnRate,
			data->rangeData.rtnAmbRate,
			data->rangeData.rtnConvTime);
		log_count = 0;
	}
#endif
}
/* interrupt work handler */
static void stmvl6180_work_handler(struct work_struct *work)
{
	struct stmvl6180_data *data = container_of(work, struct stmvl6180_data, dwork.work);
	struct i2c_client *client=data->client;
	int ret=0;
#ifndef MULTI_READ
	uint8_t gpio_status=0, range_start=0, range_status=0;
#endif
	uint8_t to_startPS=0;


	mutex_lock(&data->work_mutex);
	
#ifdef MULTI_READ
	ret = stmvl6180_ps_read_result(client);
	if (ret == 0 && ((data->ResultBuffer[0]&0x01) == 0x01))
	{
		if( data->enable_ps_sensor)
		{
			stmvl6180_ps_parse_result(client);
			stmvl6180_ps_read_measurement(client);
			if (data->ps_is_singleshot)
				to_startPS = 1;

		}
	}
#else	
	VL6180x_RangeGetInterruptStatus(vl6180x_dev, &gpio_status);
	VL6180x_RdByte(vl6180x_dev, RESULT_RANGE_STATUS, &range_status);
	VL6180x_RdByte(vl6180x_dev, SYSRANGE_START, &range_start);

	//if (gpio_status == RES_INT_STAT_GPIO_NEW_SAMPLE_READY)
	if (((range_status&0x01)==0x01) && (range_start== 0x00))
	{
		if( data->enable_ps_sensor)
		{
			stmvl6180_ps_read_measurement(client);
			if (data->ps_is_singleshot)
				to_startPS = 1;
		}
		VL6180x_RangeClearInterrupt(vl6180x_dev);

	}
#if 0 //#for testing
	else
	{
		uint8_t data;
		VL6180x_RdByte(vl6180x_dev, RESULT_RANGE_STATUS, &data);
		printk("we get status as 0x%x\n",data);
	}
#endif

#if 0
	printk("we get gpio :0x%x, status as 0x%x, start:0x%x\n",gpio_status,range_status,range_start);
#endif
#endif
	if (to_startPS)
	{
		VL6180x_RangeSetSystemMode(vl6180x_dev, MODE_START_STOP | MODE_SINGLESHOT);
	}

	if( data->enable_ps_sensor)//[5816][camera]fixed schedule still work when laser sensor be closed.
	{
		schedule_delayed_work(&data->dwork, msecs_to_jiffies((data->delay_ms)));	// restart timer
	}
   	mutex_unlock(&data->work_mutex);

	return;
}

#ifdef USE_INT
static irqreturn_t stmvl6180_interrupt_handler(int vec, void *info)
{

	struct i2c_client *client=(struct i2c_client *)info;
	struct stmvl6180_data *data = i2c_get_clientdata(client);
	

	if (data->irq == vec)
	{
		//vl6180_dbgmsg("==>interrupt_handler\n");
		schedule_delayed_work(&data->dwork, 0);
	}
	return IRQ_HANDLED;
}
#endif

/*
 * SysFS support
 */
static ssize_t stmvl6180_show_enable_ps_sensor(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct stmvl6180_data *data = i2c_get_clientdata(client);
	
	return sprintf(buf, "%d\n", data->enable_ps_sensor);
}

static ssize_t stmvl6180_store_enable_ps_sensor(struct device *dev,
				struct device_attribute *F, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct stmvl6180_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);
 	unsigned long flags;

	//int retval = 0;
	//struct regulator *vana = NULL;
//	vana = regulator_get(&client->dev, "vdd");
//
//   retval = regulator_set_voltage(vana,
//						2800000,2800000);
	
   pr_err("enable ps senosr ( %ld),addr:0x%x\n", val,client->addr);
	if ((val != 0) && (val != 1)) {
		pr_err("%s:store unvalid value=%ld\n", __func__, val);
		return count;
	}
 	mutex_lock(&data->work_mutex);
	if(val == 1) {

		//retval = regulator_enable(vana);
                msleep (3);

		//turn on p sensor
		if (data->enable_ps_sensor==0) {

			stmvl6180_set_enable(client,1); /* Power Off */
			//re-init
			VL6180x_Prepare(vl6180x_dev);
			VL6180x_UpscaleSetScaling(vl6180x_dev, 3);
			//set parameters
			//VL6180x_RangeSetInterMeasPeriod(vl6180x_dev, 10); //10ms
			//set interrupt mode
			//VL6180x_RangeSetupGPIO1(vl6180x_dev, GPIOx_SELECT_GPIO_INTERRUPT_OUTPUT, INTR_POL_HIGH);
			VL6180x_RangeConfigInterrupt(vl6180x_dev, CONFIG_GPIO_INTERRUPT_NEW_SAMPLE_READY);
			//start
			VL6180x_RangeSetSystemMode(vl6180x_dev, MODE_START_STOP|MODE_SINGLESHOT);
			data->ps_is_singleshot = 1;
			data->enable_ps_sensor= 1;
			/* we need this polling timer routine for house keeping*/
			spin_lock_irqsave(&data->update_lock.wait_lock, flags); 
			/*
			 * If work is already scheduled then subsequent schedules will not
			 * change the scheduled time that's why we have to cancel it first.
			 */
			cancel_delayed_work(&data->dwork);
			//schedule_delayed_work(&data->dwork, msecs_to_jiffies(INT_POLLING_DELAY));	
			schedule_delayed_work(&data->dwork, msecs_to_jiffies(data->delay_ms));	
			spin_unlock_irqrestore(&data->update_lock.wait_lock, flags);	
			//stmvl6180_set_enable(client, 1); /* Power On */
		}
	} 
	else {
		//turn off p sensor 
		data->enable_ps_sensor = 0;
		if (data->ps_is_singleshot == 0)
			VL6180x_RangeSetSystemMode(vl6180x_dev, MODE_START_STOP);
		VL6180x_RangeClearInterrupt(vl6180x_dev);

		stmvl6180_set_enable(client, 0);

		spin_lock_irqsave(&data->update_lock.wait_lock, flags); 
		/*
		 * If work is already scheduled then subsequent schedules will not
		 * change the scheduled time that's why we have to cancel it first.
		 */
		cancel_delayed_work(&data->dwork);
		spin_unlock_irqrestore(&data->update_lock.wait_lock, flags); 

         msleep (3);
		//retval = regulator_disable(vana);

	}

	mutex_unlock(&data->work_mutex);
	return count;
}

static DEVICE_ATTR(enable_ps_sensor, S_IWUGO | S_IRUGO,
				   stmvl6180_show_enable_ps_sensor, stmvl6180_store_enable_ps_sensor);

static ssize_t stmvl6180_show_enable_debug(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct stmvl6180_data *data = i2c_get_clientdata(client);

	
	return sprintf(buf, "%d\n", data->enableDebug);	
}

//for als integration time setup
static ssize_t stmvl6180_store_enable_debug(struct device *dev,
					struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct stmvl6180_data *data = i2c_get_clientdata(client);
	long on = simple_strtol(buf, NULL, 10);
	if ((on !=0) &&  (on !=1))
	{
		pr_err("%s: set debug=%ld\n", __func__, on);
		return count;
	}
	data->enableDebug=on;

	return count;
}

//DEVICE_ATTR(name,mode,show,store)
static DEVICE_ATTR(enable_debug, S_IWUSR | S_IRUGO,
				   stmvl6180_show_enable_debug, stmvl6180_store_enable_debug);


static ssize_t stmvl6180_show_set_delay_ms(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct stmvl6180_data *data = i2c_get_clientdata(client);

	
	return sprintf(buf, "%d\n", data->delay_ms);	
}

//for als integration time setup
static ssize_t stmvl6180_store_set_delay_ms(struct device *dev,
					struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct stmvl6180_data *data = i2c_get_clientdata(client);
	long delay_ms = simple_strtol(buf, NULL, 10);
	//printk("stmvl6180_store_set_delay_ms as %ld======\n",delay_ms);
	if (delay_ms == 0)
	{
		pr_err("%s: set delay_ms=%ld\n", __func__, delay_ms);
		return count;
	}
 	mutex_lock(&data->work_mutex);
	data->delay_ms=delay_ms;
	mutex_unlock(&data->work_mutex);
	return count;
}

//DEVICE_ATTR(name,mode,show,store)
static DEVICE_ATTR(set_delay_ms, S_IWUSR | S_IRUGO,
				   stmvl6180_show_set_delay_ms, stmvl6180_store_set_delay_ms);

static struct attribute *stmvl6180_attributes[] = {
	&dev_attr_enable_ps_sensor.attr,
	&dev_attr_enable_debug.attr,
	&dev_attr_set_delay_ms.attr ,
	&dev_attr_cal_10cm.attr ,
	&dev_attr_cal_40cm.attr ,
	NULL
};


static const struct attribute_group stmvl6180_attr_group = {
	.attrs = stmvl6180_attributes,
};

/////////////////////////PROC//////////////////////////////////////////////
static int laser_focus_on_read (char *page, char **start, off_t off, int count,
	int *eof, void *data_unused)
{
	VL6180_LOG("[%s] BEGIN \n", __func__);

	VL6180_LOG("[%s] END \n", __func__);
	return 0;
}
static int laser_focus_on_write (struct file *file, const char __user *buffer,
       unsigned long count, void *parg)
{
	char buf[] = "000000000";
	unsigned long len = min((unsigned long)sizeof(buf) - 1, count);
	int input_value;
	struct i2c_client *client;
	struct stmvl6180_data *data;
 	unsigned long flags;

	//VL6180_LOG("[%s] BEGIN Buff=%c \n", __func__, buffer[0]);
	if (copy_from_user(buf, buffer, len)) return count;
	buf[len] = 0;
	if (sscanf(buf, "%d", &input_value) != 1) {
		return -EINVAL;
	}

	client = i2c_getclient();
	 pr_err("laser_focus_on_write ( %d),addr:0x%x\n", input_value,client->addr);
	if ((input_value != 0) && (input_value != 1)) {
		pr_err("%s:store unvalid value=%d\n", __func__, input_value);
		return count;
	}

	data = i2c_get_clientdata(client);
	mutex_lock(&data->work_mutex);
	if(input_value == 1) {

		//retval = regulator_enable(vana);
				msleep (3);

		//turn on p sensor
		if (data->enable_ps_sensor==0) {

			stmvl6180_set_enable(client,1); /* Power Off */
			//re-init
			VL6180x_Prepare(vl6180x_dev);
			VL6180x_UpscaleSetScaling(vl6180x_dev, 3);
			//set parameters
			//VL6180x_RangeSetInterMeasPeriod(vl6180x_dev, 10); //10ms
			//set interrupt mode
			//VL6180x_RangeSetupGPIO1(vl6180x_dev, GPIOx_SELECT_GPIO_INTERRUPT_OUTPUT, INTR_POL_HIGH);
			VL6180x_RangeConfigInterrupt(vl6180x_dev, CONFIG_GPIO_INTERRUPT_NEW_SAMPLE_READY);
			//start
			VL6180x_RangeSetSystemMode(vl6180x_dev, MODE_START_STOP|MODE_SINGLESHOT);
			data->ps_is_singleshot = 1;
			data->enable_ps_sensor= 1;
			/* we need this polling timer routine for house keeping*/
			spin_lock_irqsave(&data->update_lock.wait_lock, flags);
			/*
			 * If work is already scheduled then subsequent schedules will not
			 * change the scheduled time that's why we have to cancel it first.
			 */
			cancel_delayed_work(&data->dwork);
			//schedule_delayed_work(&data->dwork, msecs_to_jiffies(INT_POLLING_DELAY));
			schedule_delayed_work(&data->dwork, msecs_to_jiffies(data->delay_ms));
			spin_unlock_irqrestore(&data->update_lock.wait_lock, flags);
			//stmvl6180_set_enable(client, 1); /* Power On */
		}
	}
	else {
		//turn off p sensor
		data->enable_ps_sensor = 0;
		if (data->ps_is_singleshot == 0)
			VL6180x_RangeSetSystemMode(vl6180x_dev, MODE_START_STOP);
		VL6180x_RangeClearInterrupt(vl6180x_dev);

		stmvl6180_set_enable(client, 0);

		spin_lock_irqsave(&data->update_lock.wait_lock, flags);
		/*
		 * If work is already scheduled then subsequent schedules will not
		 * change the scheduled time that's why we have to cancel it first.
		 */
		cancel_delayed_work(&data->dwork);
		spin_unlock_irqrestore(&data->update_lock.wait_lock, flags);

				msleep (3);
		//retval = regulator_disable(vana);

	}
	mutex_unlock(&data->work_mutex);
	VL6180_LOG("[%s] END input_value=%d, \n", __func__, input_value);
	return strnlen(buf, count);;
}

static int laser_focus_value_read (char *page, char **start, off_t off, int count,
	int *eof, void *data_unused)
{

	 int len;
	 struct i2c_client *client;
	 struct stmvl6180_data *data ;
	 log_count++;
	if(log_count == 100)//ASUS BSP Ryan +++ reduce log
	VL6180_LOG("[%s] BEGIN ", __func__);
	client = i2c_getclient();
	data = i2c_get_clientdata(client);
	len = sprintf(page, "%d\n",  data->rangeData.range_mm);

	if (len <= off+count) *eof = 1;

	*start = page + off;

	 len -= off;

	 if (len>count) len = count;

	 if (len<0) len = 0;

	if(log_count == 100){//ASUS BSP Ryan +++ reduce log
	VL6180_LOG("[%s] END ", __func__);
	log_count = 0;
	}
	 return len;
}
static int laser_focus_value_write (struct file *file, const char __user *buffer,
       unsigned long count, void *data)
{
	char buf[] = "000000000";
	unsigned long len = min((unsigned long)sizeof(buf) - 1, count);
	int input_value;

	//VL6180_LOG("[%s] BEGIN Buff=%s \n", __func__, buffer);
	if (copy_from_user(buf, buffer, len)) return count;
	buf[len] = 0;
	if (sscanf(buf, "%d", &input_value) != 1) {
		return -EINVAL;
	}


	VL6180_LOG("[%s] END input_value=%d, ", __func__, input_value);
	return strnlen(buf, count);;
}

static int laser_focus_value_more_read (char *page, char **start, off_t off, int count,
	int *eof, void *data_unused)
{
	int len;
	 struct i2c_client *client;
	 struct stmvl6180_data *data ;

	 log_count++;
	if(log_count == 100)//ASUS BSP Ryan +++ reduce log
	VL6180_LOG("[%s] BEGIN ", __func__);
	client = i2c_getclient();
	data = i2c_get_clientdata(client);
	len = sprintf(page, "%d#%d#%d\n",  data->rangeData.range_mm,
			 	 	 	 	 	 	   data->rangeData.DMax,
			 	 	 	 	 	 	   data->rangeData.errorStatus);

	if (len <= off+count) *eof = 1;

	*start = page + off;

	len -= off;

	if (len>count) len = count;

	if (len<0) len = 0;
	if(log_count == 100){//ASUS BSP Ryan +++ reduce log
	VL6180_LOG("[%s] END ", __func__);
	log_count = 0;
	}
	return len;
}
static int laser_focus_value_more_write (struct file *file, const char __user *buffer,
       unsigned long count, void *data)
{
	char buf[] = "000000000";
	unsigned long len = min((unsigned long)sizeof(buf) - 1, count);
	int input_value;

	//VL6180_LOG("[%s] BEGIN Buff=%s \n", __func__, buffer);
	if (copy_from_user(buf, buffer, len)) return count;
	buf[len] = 0;
	if (sscanf(buf, "%d", &input_value) != 1) {
		return -EINVAL;
	}


	VL6180_LOG("[%s] END input_value=%d, ", __func__, input_value);
	return strnlen(buf, count);;
}



static int laser_focus_status_read (char *page, char **start, off_t off, int count,
	int *eof, void *data_unused)
{
	VL6180_LOG("[%s] BEGIN ", __func__);

	VL6180_LOG("[%s] END ", __func__);
	return 0;
}
static int laser_focus_status_write (struct file *file, const char __user *buffer,
       unsigned long count, void *data)
{
	char buf[] = "000000000";
	unsigned long len = min((unsigned long)sizeof(buf) - 1, count);
	int input_value;

	//VL6180_LOG("[%s] BEGIN Buff=%s \n", __func__, buffer);
	if (copy_from_user(buf, buffer, len)) return count;
	buf[len] = 0;
	if (sscanf(buf, "%d", &input_value) != 1) {
		return -EINVAL;
	}


	VL6180_LOG("[%s] END input_value=%d, ", __func__, input_value);
	return strnlen(buf, count);;
}




/*
 * misc device file operation functions
 */
static int stmvl6180_ioctl_handler(struct file *file, 
				unsigned int cmd, unsigned long arg, void __user *p)
{

	int rc=0;
 	unsigned long flags;
	unsigned long distance=0;
	struct i2c_client *client;
	switch (cmd) {
	case VL6180_IOCTL_INIT:	   /* init.  */
	{
		client = i2c_getclient();
		if (client)
		{
			struct stmvl6180_data *data = i2c_get_clientdata(client);
			//turn on p sensor only if it's not enabled by other client
			if (data->enable_ps_sensor==0) {
				stmvl6180_set_enable(client,1); /* Power Off */
				//re-init
				VL6180x_Prepare(vl6180x_dev);
				VL6180x_UpscaleSetScaling(vl6180x_dev, 3);
#if VL6180x_WRAP_AROUND_FILTER_SUPPORT
				VL6180x_FilterSetState(vl6180x_dev, 1); // turn on wrap around filter
#endif
				//set parameters
				//VL6180x_RangeSetInterMeasPeriod(vl6180x_dev, 10); //10ms
				//set interrupt mode
				//VL6180x_RangeSetupGPIO1(vl6180x_dev, GPIOx_SELECT_GPIO_INTERRUPT_OUTPUT, INTR_POL_HIGH);
				VL6180x_RangeConfigInterrupt(vl6180x_dev, CONFIG_GPIO_INTERRUPT_NEW_SAMPLE_READY);
				VL6180x_RangeClearInterrupt(vl6180x_dev);
	
				//start
				//range_set_systemMode(client->addr, RANGE_START_SINGLESHOT);
				//data->ps_is_singleshot = 1;
				VL6180x_RangeSetSystemMode(vl6180x_dev, MODE_START_STOP|MODE_SINGLESHOT);
				data->ps_is_singleshot = 1;
				data->enable_ps_sensor= 1;

				/* we need this polling timer routine for house keeping*/
				spin_lock_irqsave(&data->update_lock.wait_lock, flags); 
				/*
				 * If work is already scheduled then subsequent schedules will not
				 * change the scheduled time that's why we have to cancel it first.
				 */
				cancel_delayed_work(&data->dwork);
				//schedule_delayed_work(&data->dwork, msecs_to_jiffies(INT_POLLING_DELAY));	
				schedule_delayed_work(&data->dwork, msecs_to_jiffies(data->delay_ms));	
				spin_unlock_irqrestore(&data->update_lock.wait_lock, flags);	
	
				//stmvl6180_set_enable(client, 1); /* Power On */
			}
		

		}
		return 0;
	}
	case VL6180_IOCTL_XTALKCALB: 	/*crosstalk calibration*/
	{
		client = i2c_getclient();
		if (client)
		{
			struct stmvl6180_data *data = i2c_get_clientdata(client);
			//turn on p sensor only if it's not enabled by other client
			if (data->enable_ps_sensor==0) {
				printk("ioclt XTALKCALB to enable PS sensor for crosstalk calibration=====\n");
				stmvl6180_set_enable(client,0); /* Power Off */
				//re-init
				VL6180x_Prepare(vl6180x_dev);
				VL6180x_UpscaleSetScaling(vl6180x_dev, 3);
#if VL6180x_WRAP_AROUND_FILTER_SUPPORT
				VL6180x_FilterSetState(vl6180x_dev, 1); // turn off wrap around filter
#endif

				VL6180x_RangeConfigInterrupt(vl6180x_dev, CONFIG_GPIO_INTERRUPT_NEW_SAMPLE_READY);
				VL6180x_RangeClearInterrupt(vl6180x_dev);
				VL6180x_WrWord(vl6180x_dev, SYSRANGE_CROSSTALK_COMPENSATION_RATE, 0);

				//start
				VL6180x_RangeSetSystemMode(vl6180x_dev, MODE_START_STOP|MODE_SINGLESHOT);
				data->ps_is_singleshot = 1;
				data->enable_ps_sensor= 1;

				/* we need this polling timer routine for house keeping*/
				spin_lock_irqsave(&data->update_lock.wait_lock, flags); 
				/*
				 * If work is already scheduled then subsequent schedules will not
				 * change the scheduled time that's why we have to cancel it first.
				 */
				cancel_delayed_work(&data->dwork);
				//schedule_delayed_work(&data->dwork, msecs_to_jiffies(INT_POLLING_DELAY));	
				schedule_delayed_work(&data->dwork, msecs_to_jiffies(data->delay_ms));	
				spin_unlock_irqrestore(&data->update_lock.wait_lock, flags);	
	
				stmvl6180_set_enable(client, 1); /* Power On */
			}
		

		}
		return 0;



	}
	case VL6180_IOCTL_SETXTALK:
	{
		client = i2c_getclient();
		if (client)
		{
			unsigned int xtalkint=0;
			//struct stmvl6180_data *data = i2c_get_clientdata(client);
			if (copy_from_user(&xtalkint, (unsigned int *)p, sizeof(unsigned int))) {
				rc = -EFAULT;
			}
			printk("ioctl SETXTALK as 0x%x\n", xtalkint);
#ifdef CALIBRATION_FILE
			xtalk_calib = xtalkint;
			stmvl6180_write_xtalk_calibration_file();
#endif
			VL6180x_SetXTalkCompensationRate(vl6180x_dev, xtalkint);

		}
		return 0;
	}
	case VL6180_IOCTL_OFFCALB: 	/*offset calibration*/
	{
		client = i2c_getclient();
		if (client)
		{
			struct stmvl6180_data *data = i2c_get_clientdata(client);
			//turn on p sensor only if it's not enabled by other client
			if (data->enable_ps_sensor==0) {
				printk("ioclt OFFCALB to enable PS sensor for offset calibration=====\n");
				stmvl6180_set_enable(client,0); /* Power Off */
				//re-init
				VL6180x_Prepare(vl6180x_dev);

				VL6180x_UpscaleSetScaling(vl6180x_dev, 1);
#if VL6180x_WRAP_AROUND_FILTER_SUPPORT
				VL6180x_FilterSetState(vl6180x_dev, 0); // turn off wrap around filter
#endif

				VL6180x_RangeConfigInterrupt(vl6180x_dev, CONFIG_GPIO_INTERRUPT_NEW_SAMPLE_READY);
				VL6180x_RangeClearInterrupt(vl6180x_dev);
				VL6180x_WrWord(vl6180x_dev, SYSRANGE_PART_TO_PART_RANGE_OFFSET, 0);
				VL6180x_WrWord(vl6180x_dev, SYSRANGE_CROSSTALK_COMPENSATION_RATE, 0);

				//start
				VL6180x_RangeSetSystemMode(vl6180x_dev, MODE_START_STOP|MODE_SINGLESHOT);
				data->ps_is_singleshot = 1;
				data->enable_ps_sensor= 1;

				/* we need this polling timer routine for house keeping*/
				spin_lock_irqsave(&data->update_lock.wait_lock, flags); 
				/*
				 * If work is already scheduled then subsequent schedules will not
				 * change the scheduled time that's why we have to cancel it first.
				 */
				cancel_delayed_work(&data->dwork);
				//schedule_delayed_work(&data->dwork, msecs_to_jiffies(INT_POLLING_DELAY));	
				schedule_delayed_work(&data->dwork, msecs_to_jiffies(data->delay_ms));	
				spin_unlock_irqrestore(&data->update_lock.wait_lock, flags);	
	
				stmvl6180_set_enable(client, 1); /* Power On */	 
			}
		

		}
		return 0;



	}
	case VL6180_IOCTL_SETOFFSET:
	{
		client = i2c_getclient();
		if (client)
		{
			int8_t offsetint=0;
			/* int8_t scaling; */
			if (copy_from_user(&offsetint, (int8_t *)p, sizeof(int8_t))) {
				rc = -EFAULT;
			}
			printk("ioctl SETOFFSET as %d\n", offsetint);
#ifdef CALIBRATION_FILE
			offset_calib = offsetint;
			stmvl6180_write_offset_calibration_file();
#endif
			VL6180x_SetOffset(vl6180x_dev,offsetint);
		}
		return 0;
	}
	case VL6180_IOCTL_STOP:
	{
		client = i2c_getclient();
		if (client)
		{
			struct stmvl6180_data *data = i2c_get_clientdata(client);
			//turn off p sensor only if it's enabled by other client
			if (data->enable_ps_sensor==1) {

				//turn off p sensor 
				data->enable_ps_sensor = 0;
				if (data->ps_is_singleshot == 0)
					VL6180x_RangeSetSystemMode(vl6180x_dev, MODE_START_STOP);
				VL6180x_RangeClearInterrupt(vl6180x_dev);

				stmvl6180_set_enable(client, 0);

				spin_lock_irqsave(&data->update_lock.wait_lock, flags); 
				/*
		 		* If work is already scheduled then subsequent schedules will not
		 		* change the scheduled time that's why we have to cancel it first.
		 		*/
				cancel_delayed_work(&data->dwork);
				spin_unlock_irqrestore(&data->update_lock.wait_lock, flags); 
			}
		}
		return 0;
	}
	case VL6180_IOCTL_GETDATA:	  /* Get proximity value only */
	{
		client = i2c_getclient();
		if (client)
		{
			struct stmvl6180_data *data = i2c_get_clientdata(client);
			distance = data->rangeData.FilteredData.range_mm;	
		}
		//printk("vl6180_getDistance return %ld\n",distance);
		return put_user(distance, (unsigned long *)p);
		
	}
	case VL6180_IOCTL_GETDATAS:	 /* Get all range data */
	{
		client = i2c_getclient();
		if (client)
		{
			struct stmvl6180_data *data = i2c_get_clientdata(client);
			//printk("IOCTL_GETDATAS, m_range_mm:%d===\n",data->rangeData.m_range_mm);
			if (copy_to_user((VL6180x_RangeData_t *)p, &(data->rangeData), sizeof(VL6180x_RangeData_t))) {
				rc = -EFAULT;
			}	 
		}
		else
			return -EFAULT;

		return rc;   
	}
	default:
		return -EINVAL;
	}
	return rc;
}

static int stmvl6180_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int stmvl6180_flush(struct file *file, fl_owner_t id)
{
	//kent remove it for crash issue when vl6180 do not exist
// 	unsigned long flags;
//	struct i2c_client *client;
//	client = i2c_getclient();
//
//	(void) file;
//	(void) id;
//	if (client)
//	{
//		struct stmvl6180_data *data = i2c_get_clientdata(client);
//		if (data->enable_ps_sensor==1)
//		{
//			//turn off p sensor if it's enabled
//			data->enable_ps_sensor = 0;
//			VL6180x_RangeClearInterrupt(vl6180x_dev);
//
//			stmvl6180_set_enable(client, 0);
//
//			spin_lock_irqsave(&data->update_lock.wait_lock, flags);
//			/*
//		 	* If work is already scheduled then subsequent schedules will not
//		 	* change the scheduled time that's why we have to cancel it first.
//		 	*/
//			cancel_delayed_work(&data->dwork);
//			spin_unlock_irqrestore(&data->update_lock.wait_lock, flags);
//
//		}
//	}
	return 0;
}
static long stmvl6180_ioctl(struct file *file, 
				unsigned int cmd, unsigned long arg)
{
	int ret;
	mutex_lock(&vl6180_mutex);
	ret = stmvl6180_ioctl_handler(file, cmd, arg, (void __user *)arg);
	mutex_unlock(&vl6180_mutex);

	return ret;
}


/*
 * Initialization function
 */
static int stmvl6180_init_client(struct i2c_client *client)
{
	struct stmvl6180_data *data = i2c_get_clientdata(client);
	uint8_t id=0,module_major=0,module_minor=0;
	uint8_t model_major=0,model_minor=0;
	uint8_t i=0,val;

	// Read Model ID
	VL6180x_RdByte(vl6180x_dev, VL6180_MODEL_ID_REG, &id);
	printk("read MODLE_ID: 0x%x, i2cAddr:0x%x\n",id,client->addr);
	if (id == 0xb4) {
		printk("STM VL6180 Found\n");
	}
	else if (id==0){
		printk("Not found STM VL6180\n");
		return -EIO;
	}

	// Read Model Version
	VL6180x_RdByte(vl6180x_dev, VL6180_MODEL_REV_MAJOR_REG, &model_major);
	model_major &= 0x07;
	VL6180x_RdByte(vl6180x_dev, VL6180_MODEL_REV_MINOR_REG, &model_minor);
	model_minor &= 0x07;
	printk("STM VL6180 Model Version : %d.%d\n", model_major,model_minor);

	// Read Module Version
	VL6180x_RdByte(vl6180x_dev, VL6180_MODULE_REV_MAJOR_REG, &module_major);
	VL6180x_RdByte(vl6180x_dev, VL6180_MODULE_REV_MINOR_REG, &module_minor);
	printk("STM VL6180 Module Version : %d.%d\n",module_major,module_minor);
	
	// Read Identification 
	printk("STM VL6180 Serial Numbe: ");
	for (i=0; i<=(VL6180_FIRMWARE_REVISION_ID_REG-VL6180_REVISION_ID_REG);i++)
	{
		VL6180x_RdByte(vl6180x_dev, (VL6180_REVISION_ID_REG+i), &val);
		printk("0x%x-",val);
	}
	printk("\n");
	

	data->delay_ms=50; //init to 50ms
	data->ps_data=0;			
	data->enableDebug=0;
#ifdef CALIBRATION_FILE
	stmvl6180_read_calibration_file();
#endif
	
	//VL6180 Initialization
	VL6180x_WaitDeviceBooted(vl6180x_dev);
	VL6180x_InitData(vl6180x_dev);
	//VL6180x_FilterSetState(vl6180x_dev, 1); /* activate wrap around filter */
	//VL6180x_DisableGPIOxOut(vl6180x_dev, 1); /* diable gpio 1 output, not needed when polling */

	return 0;
}
#if 0
static int stmvl6180_dt_parse_vreg_info(struct device *dev,
			struct  *vreg, const char *vreg_name)
{
	int ret = 0;
	u32 vol_suply[2];
	struct device_node *np = dev->of_node;

	ret = of_property_read_u32_array(np, vreg_name, vol_suply, 2);
	if (ret < 0) {
		FMDERR("Invalid property name\n");
		ret =  -EINVAL;
	} else {
		vreg->low_vol_level = vol_suply[0];
		vreg->high_vol_level = vol_suply[1];
	}
	return ret;
}
#endif
/*
 * I2C init/probing/exit functions
 */

static struct i2c_driver stmvl6180_driver;

static int stmvl6180_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
//	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct stmvl6180_data *data;
	int err = 0;
	if(laser_enable != true){
		pr_err("laser enable == false, do not probe laser");
		err = -EIO;
		goto exit;
	}
	/*int irq = 0;*/
	printk("stmvl6180_probe==========\n");
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE)) {
		err = -EIO;
	pr_err("stmvl6180_probe=====error=====\n");
		goto exit;
	}

	data = kzalloc(sizeof(struct stmvl6180_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}
	memset(data,0,sizeof(struct stmvl6180_data));
	data->client = client;
	i2c_set_clientdata(client, data);

	data->enable = 0;		/* default mode is standard */
	

	mutex_init(&data->update_lock);
	mutex_init(&data->work_mutex);
	mutex_init(&vl6180_mutex);

	// setup platform i2c client
	i2c_setclient(client);
	
	//interrupt set up
#ifdef USE_INT
	gpio_request(IRQ_NUM,"vl6180_gpio_int");
	gpio_direction_input(IRQ_NUM);
	irq = gpio_to_irq(IRQ_NUM);
	if (irq < 0)
	{
		pr_err("filed to map GPIO :%d to interrupt:%d\n",IRQ_NUM,irq);
	}
	else
	{
		int result;
		vl6180_dbgmsg("register_irq:%d\n",irq);
		if ((result = request_threaded_irq(irq, NULL, stmvl6180_interrupt_handler, IRQF_TRIGGER_RISING, //IRQF_TRIGGER_FALLING- poliarity:0 IRQF_TRIGGER_RISNG - poliarty:1
			"vl6180_lb_gpio_int", (void *)client))) 
		{
			pr_err("%s Could not allocate STMVL6180_INT ! result:%d\n", __func__,result);
	
			goto exit_kfree;
		}
	}	
	//disable_irq(irq);

	data->irq = irq;
	vl6180_dbgmsg("%s interrupt is hooked\n", __func__);
#endif

	//kent
#ifdef USE_RESET
	err=gpio_request(RESET_GPIO,"vl6180_gpio_reset");
	if(err)
		pr_err("In %s, gpio_request failed\n", __func__);
    gpio_direction_output(RESET_GPIO,1);
#endif


    stmvl6180_set_enable(client,1);

	INIT_DELAYED_WORK(&data->dwork, stmvl6180_work_handler);

	/* Initialize the STM VL6180 chip */
	err = stmvl6180_init_client(client);
       // regulator_disable (vana);
	if (err)
		goto exit_kfree;

	/* Register to Input Device */
	data->input_dev_ps = input_allocate_device();
	if (!data->input_dev_ps) {
		err = -ENOMEM;
		pr_err("%s Failed to allocate input device ps\n",__func__);
		goto exit_free_dev_ps;
	}
	
	set_bit(EV_ABS, data->input_dev_ps->evbit);

	input_set_abs_params(data->input_dev_ps, ABS_DISTANCE, 0, 76, 0, 0); //range in cm 
	input_set_abs_params(data->input_dev_ps, ABS_HAT0X, 0, 0xffffffff, 0, 0); //timeval.tv_sec
	input_set_abs_params(data->input_dev_ps, ABS_HAT0Y, 0, 0xffffffff, 0, 0); //timeval.tv_usec
	input_set_abs_params(data->input_dev_ps, ABS_HAT1X, 0, 765, 0, 0); //range in mm
	input_set_abs_params(data->input_dev_ps, ABS_HAT1Y, 0, 0xffffffff, 0, 0); //errorStatus
	input_set_abs_params(data->input_dev_ps, ABS_HAT2X, 0, 0xffffffff, 0, 0); //signal rate (MCPS)
	input_set_abs_params(data->input_dev_ps, ABS_HAT2Y, 0, 0xffffffff, 0, 0); //Return Ambient rate in KCPS
	input_set_abs_params(data->input_dev_ps, ABS_HAT3X, 0, 0xffffffff, 0, 0); // Return Convergence time
	input_set_abs_params(data->input_dev_ps, ABS_HAT3Y, 0, 0xffffffff, 0, 0); //DMax

	data->input_dev_ps->name = "STM VL6180 proximity sensor";


	err = input_register_device(data->input_dev_ps);
	if (err) {
		err = -ENOMEM;
		pr_err("%sUnable to register input device ps: %s\n",__func__, data->input_dev_ps->name);
		goto exit_unregister_dev_ps;
	}

	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &stmvl6180_attr_group);
	if (err)
	{
		pr_err("%sUnable to create sysfs group\n",__func__);
		goto exit_unregister_dev_ps;
	}

	pr_err("%s support ver. %s enabled\n", __func__, DRIVER_VERSION);

	stmvl6180_set_enable(client,0);


	if ((proc_laser_on = create_proc_entry(LASER_FOCUS_ON_PATH, S_IRUGO | S_IWUGO, NULL))) {
		proc_laser_on->read_proc = laser_focus_on_read;
		proc_laser_on->write_proc = laser_focus_on_write;
	}

	if ((proc_laser_value = create_proc_entry(LASER_FOCUS_VALUE, S_IRUGO | S_IWUGO, NULL))) {
		proc_laser_value->read_proc = laser_focus_value_read;
		proc_laser_value->write_proc = laser_focus_value_write;
	}

	if ((proc_laser_value_more = create_proc_entry(LASER_FOCUS_VALUE_MORE_INFO, S_IRUGO | S_IWUGO, NULL))) {
		proc_laser_value_more->read_proc = laser_focus_value_more_read;
		proc_laser_value_more->write_proc = laser_focus_value_more_write;
	}

	if ((proc_laser_status = create_proc_entry(LASER_FOCUS_STATUS_FOR_CAMERA, S_IRUGO | S_IWUGO, NULL))) {
		proc_laser_status->read_proc = laser_focus_status_read;
		proc_laser_status->write_proc = laser_focus_status_write;
	}

	return 0;

exit_unregister_dev_ps:
	input_unregister_device(data->input_dev_ps);	
exit_free_dev_ps:
	input_free_device(data->input_dev_ps);
/*
Defined but not used
exit_free_irq:*/
#ifdef USE_INT
	free_irq(irq, client);
#endif
exit_kfree:
	kfree(data);
exit:

	pr_err("stmvl6180_probe==error exit========\n");
	return err;
}

static int stmvl6180_remove(struct i2c_client *client)
{
	struct stmvl6180_data *data = i2c_get_clientdata(client);
	
	//input_unregister_device(data->input_dev_als);
	input_unregister_device(data->input_dev_ps);
	
	//input_free_device(data->input_dev_als);
	input_free_device(data->input_dev_ps);

#ifdef  USE_INT
	free_irq(data->irq, client);
#endif

	sysfs_remove_group(&client->dev.kobj, &stmvl6180_attr_group);

	/* Power down the device */
	stmvl6180_set_enable(client, 0);

	kfree(data);

	return 0;
}



static const struct i2c_device_id stmvl6180_id[] = {
	{ STMVL6180_DRV_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, stmvl6180_id);

static const struct file_operations stmvl6180_ranging_fops = {
		.owner =			THIS_MODULE,
		.unlocked_ioctl =	stmvl6180_ioctl,
		.open =				stmvl6180_open,
		.flush = 			stmvl6180_flush,
};

static const struct of_device_id st_stmv16180_dt_match[] = {
	{ .compatible = "st,stmvl6180", },
	{ },
};

static struct i2c_driver stmvl6180_driver = {
	.driver = {
		.name	= STMVL6180_DRV_NAME,
		.owner	= THIS_MODULE,
    .of_match_table = st_stmv16180_dt_match,
	},
	.probe	= stmvl6180_probe,
	.remove	= stmvl6180_remove,
	.id_table = stmvl6180_id,

};


static int __init stmvl6180_init(void)
{
	int ret = i2c_add_driver(&stmvl6180_driver);
	return ret;
}

static void __exit stmvl6180_exit(void)
{
	printk("stmvl6180_exit===\n");
	i2c_del_driver(&stmvl6180_driver);
}



static void normal_mode(void)
{
	unsigned long flags;
	struct i2c_client *client;

	client = i2c_getclient();
	if (client)
	{
		struct stmvl6180_data *data = i2c_get_clientdata(client);
		//turn on p sensor only if it's not enabled by other client
		if (data->enable_ps_sensor==0) {
			stmvl6180_set_enable(client,1); /* Power Off */
			//re-init
			VL6180x_Prepare(vl6180x_dev);
			VL6180x_UpscaleSetScaling(vl6180x_dev, 3);
#if VL6180x_WRAP_AROUND_FILTER_SUPPORT
			VL6180x_FilterSetState(vl6180x_dev, 1); // turn on wrap around filter
#endif
			//set parameters
			//VL6180x_RangeSetInterMeasPeriod(vl6180x_dev, 10); //10ms
			//set interrupt mode
			//VL6180x_RangeSetupGPIO1(vl6180x_dev, GPIOx_SELECT_GPIO_INTERRUPT_OUTPUT, INTR_POL_HIGH);
			VL6180x_RangeConfigInterrupt(vl6180x_dev, CONFIG_GPIO_INTERRUPT_NEW_SAMPLE_READY);
			VL6180x_RangeClearInterrupt(vl6180x_dev);

			//start
			//range_set_systemMode(client->addr, RANGE_START_SINGLESHOT);
			//data->ps_is_singleshot = 1;
			VL6180x_RangeSetSystemMode(vl6180x_dev, MODE_START_STOP|MODE_SINGLESHOT);
			data->ps_is_singleshot = 1;
			data->enable_ps_sensor= 1;

			/* we need this polling timer routine for house keeping*/
			spin_lock_irqsave(&data->update_lock.wait_lock, flags);
			/*
			 * If work is already scheduled then subsequent schedules will not
			 * change the scheduled time that's why we have to cancel it first.
			 */
			cancel_delayed_work(&data->dwork);
			//schedule_delayed_work(&data->dwork, msecs_to_jiffies(INT_POLLING_DELAY));
			schedule_delayed_work(&data->dwork, msecs_to_jiffies(data->delay_ms));
			spin_unlock_irqrestore(&data->update_lock.wait_lock, flags);

			//stmvl6180_set_enable(client, 1); /* Power On */
			pr_err("[STMVL6180] normal_mode entered");
		}
	}
}

static void stop_mode(void)
{
	unsigned long flags;
	struct i2c_client *client;

	client = i2c_getclient();
	if (client)
	{
		struct stmvl6180_data *data = i2c_get_clientdata(client);
		//turn off p sensor only if it's enabled by other client
		if (data->enable_ps_sensor==1) {

			//turn off p sensor
			data->enable_ps_sensor = 0;
			if (data->ps_is_singleshot == 0)
				VL6180x_RangeSetSystemMode(vl6180x_dev, MODE_START_STOP);
			VL6180x_RangeClearInterrupt(vl6180x_dev);

			stmvl6180_set_enable(client, 0);

			spin_lock_irqsave(&data->update_lock.wait_lock, flags);
			/*
			* If work is already scheduled then subsequent schedules will not
			* change the scheduled time that's why we have to cancel it first.
			*/
			cancel_delayed_work(&data->dwork);
			spin_unlock_irqrestore(&data->update_lock.wait_lock, flags);
			pr_err("[STMVL6180] stop_mode entered");
		}
	}
}


static  void cal_offset_mode(void)
{
	struct i2c_client *client;

	//unsigned long flags;
	client = i2c_getclient();
	if (client)
	{
		struct stmvl6180_data *data = i2c_get_clientdata(client);
		//turn on p sensor only if it's not enabled by other client
		if (data->enable_ps_sensor==0) {
			printk("ioclt OFFCALB to enable PS sensor for offset calibration=====\n");
			stmvl6180_set_enable(client,1); /* Power Off */
			//re-init
			VL6180x_Prepare(vl6180x_dev);

			VL6180x_UpscaleSetScaling(vl6180x_dev, 3);
#if VL6180x_WRAP_AROUND_FILTER_SUPPORT
			VL6180x_FilterSetState(vl6180x_dev, 0); // turn off wrap around filter
#endif

			VL6180x_RangeConfigInterrupt(vl6180x_dev, CONFIG_GPIO_INTERRUPT_NEW_SAMPLE_READY);
			VL6180x_RangeClearInterrupt(vl6180x_dev);
			VL6180x_WrWord(vl6180x_dev, SYSRANGE_PART_TO_PART_RANGE_OFFSET, 0);
			VL6180x_WrWord(vl6180x_dev, SYSRANGE_CROSSTALK_COMPENSATION_RATE, 0);

			//start
			VL6180x_RangeSetSystemMode(vl6180x_dev, MODE_START_STOP|MODE_SINGLESHOT);
			data->ps_is_singleshot = 1;
			data->enable_ps_sensor= 1;

//			/* we need this polling timer routine for house keeping*/
//			spin_lock_irqsave(&data->update_lock.wait_lock, flags);
//			/*
//			 * If work is already scheduled then subsequent schedules will not
//			 * change the scheduled time that's why we have to cancel it first.
//			 */
//			cancel_delayed_work(&data->dwork);
//			//schedule_delayed_work(&data->dwork, msecs_to_jiffies(INT_POLLING_DELAY));
//			schedule_delayed_work(&data->dwork, msecs_to_jiffies(data->delay_ms));
//			spin_unlock_irqrestore(&data->update_lock.wait_lock, flags);

			//stmvl6180_set_enable(client, 1); /* Power On */
		}


	}
}


static void cal_xtalk_mode(void)
{
	//unsigned long flags;
	struct i2c_client *client;

	client = i2c_getclient();
	if (client)
	{
		struct stmvl6180_data *data = i2c_get_clientdata(client);
		//turn on p sensor only if it's not enabled by other client
		if (data->enable_ps_sensor==0) {
			printk("ioclt XTALKCALB to enable PS sensor for crosstalk calibration=====\n");
			stmvl6180_set_enable(client,1); /* Power Off */
			//re-init
			VL6180x_Prepare(vl6180x_dev);
			VL6180x_UpscaleSetScaling(vl6180x_dev, 3);
#if VL6180x_WRAP_AROUND_FILTER_SUPPORT
			VL6180x_FilterSetState(vl6180x_dev, 1); // turn off wrap around filter
#endif

			VL6180x_RangeConfigInterrupt(vl6180x_dev, CONFIG_GPIO_INTERRUPT_NEW_SAMPLE_READY);
			VL6180x_RangeClearInterrupt(vl6180x_dev);
			VL6180x_WrWord(vl6180x_dev, SYSRANGE_CROSSTALK_COMPENSATION_RATE, 0);

			//start
			VL6180x_RangeSetSystemMode(vl6180x_dev, MODE_START_STOP|MODE_SINGLESHOT);
			data->ps_is_singleshot = 1;
			data->enable_ps_sensor= 1;

//			/* we need this polling timer routine for house keeping*/
//			spin_lock_irqsave(&data->update_lock.wait_lock, flags);
//			/*
//			 * If work is already scheduled then subsequent schedules will not
//			 * change the scheduled time that's why we have to cancel it first.
//			 */
//			cancel_delayed_work(&data->dwork);
//			//schedule_delayed_work(&data->dwork, msecs_to_jiffies(INT_POLLING_DELAY));
//			schedule_delayed_work(&data->dwork, msecs_to_jiffies(data->delay_ms));
//			spin_unlock_irqrestore(&data->update_lock.wait_lock, flags);

			//stmvl6180_set_enable(client, 1); /* Power On */
		}
	}
}



MODULE_AUTHOR("STMicroelectronics Imaging Division");
MODULE_DESCRIPTION("ST FlightSense Time-of-Flight sensor driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

module_init(stmvl6180_init);
module_exit(stmvl6180_exit);

