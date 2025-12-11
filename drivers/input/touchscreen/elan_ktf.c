/* drivers/input/touchscreen/ektf.c - ELAN EKTF verions of driver
*
* Copyright (C) 2011 Elan Microelectronics Corporation.
*
* This software is licensed under the terms of the GNU General Public
* License version 2, as published by the Free Software Foundation, and
* may be copied, distributed, and modified under those terms.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* 2014/0/28: The first release, version 0x0006
*             Integrated 2 ,5 ,and 10 fingers driver code together and
*             auto-mapping resolution.
*             Please change following parameters
*                 1. For 5 fingers protocol, please enable ELAN_PROTOCOL.
*                    The packet size is 18 or 24 bytes.
*                 2. For 10 fingers, please enable both ELAN_PROTOCOL and ELAN_TEN_FINGERS.
*                    The packet size is 40 or 4+40+40+40 (Buffer mode) bytes.
*                 3. Please enable the ELAN_BUTTON configuraton to support button.
*                 4. For ektf3k serial, Add Re-Calibration Machanism
*                    So, please enable the define of RE_CALIBRATION.
*/

/* The ELAN_PROTOCOL support normanl packet format */
#define FINGER_NUM 10
#define ELAN_PROTOCOL
//#define ELAN_BUFFER_MODE

/* [Arima_5816][bozhi_lin] enable elan touch virtual key 20150925 +++ */
#define ELAN_BUTTON
/* [Arima_5816][bozhi_lin] enable elan touch virtual key 20150925 --- */

//#define RE_CALIBRATION // Re-Calibration after system resume
//#define ELAN_2WIREICE
#define ELAN_POWER_SOURCE
#define DEVICE_NAME "elan_ktf"
#define EKTF3K_FLASH
#define PROTOCOL_A // multi-touch protocol
//#define PROTOCOL_B // Default: PROTOCOL B

#include <linux/module.h>
#include <linux/input.h>
#ifdef PROTOCOL_B
#include <linux/input/mt.h>
#endif
#ifdef PROTOCOL_A
#include <linux/input.h>
#endif
#include <linux/interrupt.h>
#include <linux/earlysuspend.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/miscdevice.h>
#include <linux/debugfs.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>

#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#endif
// for linux 2.6.36.3
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/ioctl.h>
#include <linux/switch.h>
#include <linux/proc_fs.h>
#include <linux/firmware.h>
#include <linux/wakelock.h>
#include <linux/input/elan_ktf.h>
#include <linux/kthread.h>

#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/krait-regulator.h>
#endif

#define PACKET_SIZE		67 // support 10 fingers packet for nexus7 55
#define MAX_FINGER_SIZE		255
#define PWR_STATE_DEEP_SLEEP	0
#define PWR_STATE_NORMAL		1
#define PWR_STATE_MASK			BIT(3)

#define CMD_S_PKT		0x52
#define CMD_R_PKT		0x53
#define CMD_W_PKT		0x54
#define RESET_PKT		0x77
#define CALIB_PKT		0x66
#define IamLive_PKT		0x78
#define PEN_PKT			0x71
#define SmartWake_PKT		0x88

#define HELLO_PKT		0x55
#define TWO_FINGERS_PKT		0x5A
#define FIVE_FINGERS_PKT	0x5D
#define MTK_FINGERS_PKT		0x6D
#define TEN_FINGERS_PKT		0x62
#define ELAN_HID_PKT			0x3F
#define BUFFER_PKT		0x63
#define BUFFER55_PKT		0x66

#define Check_IAPMODE_BUF0	0x55
#define Check_IAPMODE_BUF1	0xAA
#define Check_IAPMODE_BUF2	0x33
#define Check_IAPMODE_BUF3	0xCC
#define GLOVEMODE
#ifdef GLOVEMODE
bool glovemode_state = false;
#endif
#define ESD_CHECK
#ifdef ESD_CHECK
int live_state = 1;
#endif

/* [Arima_5816][bozhi_lin] enable elan touch gesture and double-click mode 20151006 +++ */
#define GESTUREMODE
#if defined(GESTUREMODE)
#define GESTURE_ENABLE		(1<<6)
#define GESTURE_W_ENABLE	(1<<5)
#define GESTURE_S_ENABLE	(1<<4)
#define GESTURE_E_ENABLE	(1<<3)
#define GESTURE_C_ENABLE	(1<<2)
#define GESTURE_Z_ENABLE	(1<<1)
#define GESTURE_V_ENABLE	(1<<0)
#define GESTURE_MASK		0x3F
#define GESTURE_W			0x05
#define GESTURE_S			0x08
#define GESTURE_E			0x00
#define GESTURE_C			0x07
#define GESTURE_Z			0x01
#define GESTURE_V			0x03
#define GESTURE_DOUBLECLICK	0x0f
#endif
/* [Arima_5816][bozhi_lin] enable elan touch gesture and double-click mode 20151006 --- */

// Reset pin need to be modified by customer
#define SYSTEM_RESET_PIN_SR 16	// nexus7_grouper TEGRA_GPIO_PH6: 62, nexus7_flo 31

//Add these Define
#define PAGERETRY  30
#define IAPRESTART 5
#define FW_UPDATE_MANUAL_MODE 2

#define ELAN_VTG_MIN_UV		2700000
#define ELAN_VTG_MAX_UV		3300000
#define ELAN_ACTIVE_LOAD_UA	15000
#define ELAN_LPM_LOAD_UA	10

#define ELAN_I2C_VTG_MIN_UV	1800000
#define ELAN_I2C_VTG_MAX_UV	1800000
#define ELAN_I2C_LOAD_UA	10000
#define ELAN_I2C_LPM_LOAD_UA	10

// For Firmware Update
#define ELAN_IOCTLID	0xD0
#define IOCTL_I2C_SLAVE  _IOW(ELAN_IOCTLID,  1, int)
#define IOCTL_MAJOR_FW_VER  _IOR(ELAN_IOCTLID, 2, int)
#define IOCTL_MINOR_FW_VER  _IOR(ELAN_IOCTLID, 3, int)
#define IOCTL_RESET  _IOR(ELAN_IOCTLID, 4, int)
#define IOCTL_IAP_MODE_LOCK  _IOR(ELAN_IOCTLID, 5, int)
#define IOCTL_CHECK_RECOVERY_MODE  _IOR(ELAN_IOCTLID, 6, int)
#define IOCTL_FW_VER  _IOR(ELAN_IOCTLID, 7, int)
#define IOCTL_X_RESOLUTION  _IOR(ELAN_IOCTLID, 8, int)
#define IOCTL_Y_RESOLUTION  _IOR(ELAN_IOCTLID, 9, int)
#define IOCTL_FW_ID  _IOR(ELAN_IOCTLID, 10, int)
#define IOCTL_ROUGH_CALIBRATE  _IOR(ELAN_IOCTLID, 11, int)
#define IOCTL_IAP_MODE_UNLOCK  _IOR(ELAN_IOCTLID, 12, int)
#define IOCTL_I2C_INT  _IOR(ELAN_IOCTLID, 13, int)
#define IOCTL_RESUME  _IOR(ELAN_IOCTLID, 14, int)
#define IOCTL_POWER_LOCK  _IOR(ELAN_IOCTLID, 15, int)
#define IOCTL_POWER_UNLOCK  _IOR(ELAN_IOCTLID, 16, int)
#define IOCTL_FW_UPDATE  _IOR(ELAN_IOCTLID, 17, int)
#define IOCTL_BC_VER  _IOR(ELAN_IOCTLID, 18, int)
#define IOCTL_2WIREICE  _IOR(ELAN_IOCTLID, 19, int)

#define CUSTOMER_IOCTLID	0xA0
#define IOCTL_CIRCUIT_CHECK	_IOR(CUSTOMER_IOCTLID, 1, int)
#define IOCTL_GET_UPDATE_PROGREE	_IOR(CUSTOMER_IOCTLID,  2, int)

#define ELAN_TRULY_FW_FILENAME	"elan_truly.fw"
#define ELAN_HOLITECH_FW_FILENAME	"elan_holitech.fw"
#define TRULY_FW_ID	0x303C
#define HOLITECH_FW_ID	0x303F
#define TRULY_TP_ID	1
#define HOLITECH_TP_ID	0
static int TP_ID_GPIO_VALUE = -1;
static int TP_ID = -1;
static int TP_FW_ID = 0xFFFF;

#define SYSFS_MAX_LEN 100
#define LOGE(fmt,arg...)	printk("[ELAN_KETF] [%d] %s TOUCH_ERR : "fmt"\n",__LINE__, __func__ ,##arg)
#define LOGI(fmt,arg...)	printk("[ELAN_KETF] [%d] %s : "fmt"\n",__LINE__, __func__ ,##arg)
static int gPrint_point = 0;
static int debug_trace = 0;
static int update_fw_flag = 0;
static int point_flag[10] = {0};
static int haspoint = false;
static bool fwUploadResult = false;
static bool need_upgrade_flag = false;
static bool need_check_touch_mode = false;
static bool skip_powersupply_mode_switch = false;
static bool skip_smartwindow_mode_switch = false;
static unsigned long last_time_psensor = 0;

#define POWER_BATTERY 0
#define POWER_USB     1
#define POWER_CHARGER 2

uint8_t RECOVERY = 0x00;
int FW_VERSION = 0x00;
int X_RESOLUTION = 1024;
int Y_RESOLUTION = 1728;
int FW_ID = 0x00;
int work_lock = 0x00;
int power_lock = 0x00;
int circuit_ver = 0x01;
struct mutex ktf_mutex;
bool KEY_BACK_STATE = false;
bool KEY_HOME_STATE = false;
bool KEY_MENU_STATE = false;
int update_progree = 0;
uint8_t I2C_DATA[3] = {0x10, 0x20, 0x21};/*I2C devices address*/
int is_OldBootCode = 0; // 0:new 1:old

#ifdef ELAN_POWER_SOURCE
static unsigned now_usb_cable_status = 0;
#endif

enum
{
	PageSize		= 132,
	ACK_Fail		= 0xFF,
	ACK_OK			= 0xAA,
	ACK_REWRITE		= 0x55,
};

enum
{
	E_FD			= -1,
};

#define _ENABLE_DBG_LEVEL
#ifdef _ENABLE_DBG_LEVEL
#define PROC_FS_NAME "ektf_dbg"
#define PROC_FS_MAX_LEN 8
static struct proc_dir_entry *dbgProcFile;
#endif

struct elan_ktf_ts_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct switch_dev touch_sdev;
	struct work_struct powersupply_work;
	struct work_struct smartwindow_work;
#if defined(CONFIG_FB)
	struct notifier_block fb_notif;
	int elan_is_suspend;
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif
	int irq_gpio;
	int reset_gpio;
	int tpid_gpio;
	bool i2c_pull_up;
	// Firmware Information
	int fw_ver;
	int fw_id;
	int bc_ver;
	int x_resolution;
	int y_resolution;
	// For Firmare Update
	struct miscdevice firmware;
	struct regulator *vdd;
	struct regulator *vcc_i2c;
	struct wake_lock wakelock;
	/* [Arima_5816][bozhi_lin] enable elan touch gesture and double-click mode 20151006 +++ */
#if defined(GESTUREMODE)
	bool wakeup_dclick;
	int wakeup_gesture_type;
	bool wakeup_gesture;
#endif
	int smart_window;
	int power_supply;
	int probe_success;
	struct workqueue_struct *powersupply_work_queue;
	struct workqueue_struct *smartwindow_work_queue;
	/* [Arima_5816][bozhi_lin] enable elan touch gesture and double-click mode 20151006 --- */
#if defined(ESD_CHECK)
	struct delayed_work check_work;
#endif
};

static struct elan_ktf_ts_data *global_ts;
static int __fw_packet_handler(struct i2c_client *client);
static int elan_ktf_ts_calibrate(struct i2c_client *client);
static int elan_ktf_ts_resume(struct i2c_client *client);
static int elan_ktf_ts_suspend(struct i2c_client *client, pm_message_t mesg);
static void elan_ktf_ts_hw_reset(void);
static int __hello_packet_handler(struct i2c_client *client);
static int FW_Update(struct device *dev, bool manual);
static void elan_ktf_check_touch_mode(void);
int getCheckSUM(struct i2c_client *client);
int CalculateCheckSum(const u8 *fw_data, int PageNum);

#ifdef ELAN_2WIREICE
int elan_TWO_WIRE_ICE(struct i2c_client *client);
#endif

static int __elan_ktf_ts_poll(struct i2c_client *client)
{
	struct elan_ktf_ts_data *ts = i2c_get_clientdata(client);
	int status = 0, retry = 10;

	do {
		status = gpio_get_value(ts->irq_gpio);
		if (status == 0) break;

		retry--;
		mdelay(50);
	} while (status == 1 && retry > 0);

	return (status == 0 ? 0 : -ETIMEDOUT);
}

static int elan_ktf_ts_poll(struct i2c_client *client)
{
	return __elan_ktf_ts_poll(client);
}

static void elan_ktf_ts_hw_reset()
{
	LOGI("Start HW reset.");
	gpio_direction_output(SYSTEM_RESET_PIN_SR, 0);
	msleep(20);
	gpio_direction_output(SYSTEM_RESET_PIN_SR, 1);
	msleep(10);
}

int elan_iap_open(struct inode *inode, struct file *filp)
{
	LOGI("enter");
	if (global_ts == NULL)
		LOGE("global_ts is NULL");

	return 0;
}

int elan_iap_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t elan_iap_write(struct file *filp, const char *buff, size_t count, loff_t *offp)
{
	int ret;
	char *tmp;
	LOGI("into elan_iap_write");

	if (count > 8192)
		count = 8192;

	tmp = kmalloc(count, GFP_KERNEL);

	if (tmp == NULL)
		return -ENOMEM;

	if (copy_from_user(tmp, buff, count)) {
		return -EFAULT;
	}

	ret = i2c_master_send(global_ts->client, tmp, count);

	kfree(tmp);
	return (ret == 1) ? count : ret;

}

ssize_t elan_iap_read(struct file *filp, char *buff, size_t count, loff_t *offp)
{
	char *tmp;
	int ret;
	long rc;
	LOGI("into elan_iap_read");

	if (count > 8192)
		count = 8192;

	tmp = kmalloc(count, GFP_KERNEL);

	if (tmp == NULL)
		return -ENOMEM;

	ret = i2c_master_recv(global_ts->client, tmp, count);

	if (ret >= 0)
		rc = copy_to_user(buff, tmp, count);

	kfree(tmp);

	return (ret == 1) ? count : ret;
}

static long elan_iap_ioctl( struct file *filp, unsigned int cmd, unsigned long arg)
{

	int __user *ip = (int __user *)arg;
	LOGI("cmd value %x",cmd);

	switch (cmd) {
		case IOCTL_I2C_SLAVE:
			global_ts->client->addr = (int __user)arg;
			break;
		case IOCTL_MAJOR_FW_VER:
			break;
		case IOCTL_MINOR_FW_VER:
			break;
		case IOCTL_RESET:
			elan_ktf_ts_hw_reset();
			break;
		case IOCTL_IAP_MODE_LOCK:
			if (work_lock == 0) {
				work_lock=1;
				disable_irq(global_ts->client->irq);
#ifdef ESD_CHECK
				cancel_delayed_work_sync(&global_ts->check_work);
#endif
			}
			break;
		case IOCTL_IAP_MODE_UNLOCK:
			if (work_lock == 1) {
				work_lock=0;
				enable_irq(global_ts->client->irq);
#ifdef ESD_CHECK
				schedule_delayed_work(&global_ts->check_work, msecs_to_jiffies(2500));
#endif
			}
			break;
		case IOCTL_CHECK_RECOVERY_MODE:
			return RECOVERY;
			break;
		case IOCTL_FW_VER:
			__fw_packet_handler(global_ts->client);
			return FW_VERSION;
			break;
		case IOCTL_X_RESOLUTION:
			__fw_packet_handler(global_ts->client);
			return X_RESOLUTION;
			break;
		case IOCTL_Y_RESOLUTION:
			__fw_packet_handler(global_ts->client);
			return Y_RESOLUTION;
			break;
		case IOCTL_FW_ID:
			__fw_packet_handler(global_ts->client);
			return FW_ID;
			break;
		case IOCTL_ROUGH_CALIBRATE:
			return elan_ktf_ts_calibrate(global_ts->client);
		case IOCTL_I2C_INT:
			put_user(gpio_get_value(global_ts->irq_gpio), ip);
			break;
		case IOCTL_RESUME:
			elan_ktf_ts_resume(global_ts->client);
			break;
		case IOCTL_POWER_LOCK:
			power_lock = 1;
			break;
		case IOCTL_POWER_UNLOCK:
			power_lock = 0;
			break;
		case IOCTL_GET_UPDATE_PROGREE:
			update_progree=(int __user)arg;
			break;
		case IOCTL_FW_UPDATE:
			FW_Update(&global_ts->client->dev, false);
			break;
#ifdef ELAN_2WIREICE
		case IOCTL_2WIREICE:
			elan_TWO_WIRE_ICE(global_ts->client);
			break;
#endif
		case IOCTL_CIRCUIT_CHECK:
			return circuit_ver;
			break;
		default:
			LOGE("Un-known IOCTL Command %d", cmd);
			break;
	}
	return 0;
}

struct file_operations elan_touch_fops = {
	.open = elan_iap_open,
	.write = elan_iap_write,
	.read = elan_iap_read,
	.release = elan_iap_release,
	.unlocked_ioctl = elan_iap_ioctl,
};

int HID_EnterISPMode(struct i2c_client *client)
{
	int len = 0;
	int j;
	uint8_t flash_key[37] = {0x04, 0x00, 0x23, 0x00, 0x03, 0x00, 0x04, 0x54, 0xc0, 0xe1, 0x5a};
	uint8_t isp_cmd[37] = {0x04, 0x00, 0x23, 0x00, 0x03, 0x00, 0x04, 0x54, 0x00, 0x12, 0x34};
	uint8_t check_addr[37] = {0x04, 0x00, 0x23, 0x00, 0x03, 0x00, 0x01, 0x10};
	uint8_t buff[67] = {0};


	len = i2c_master_send(global_ts->client, flash_key,  37);
	if (len != 37) {
		LOGE("Flash key fail! len=%d", len);
		return -1;
	} else
		LOGI("FLASH key write data successfully! cmd = [%2x, %2x, %2x, %2x]", flash_key[7], flash_key[8], flash_key[9], flash_key[10]);

	mdelay(20);

	len = i2c_master_send(global_ts->client, isp_cmd,  37);
	if (len != 37) {
		LOGE("EnterISPMode fail! len=%d", len);
		return -1;
	} else
		LOGI("IAPMode write data successfully! cmd = [%2x, %2x, %2x, %2x]", isp_cmd[7], isp_cmd[8], isp_cmd[9], isp_cmd[10]);


	mdelay(20);
	len = i2c_master_send(global_ts->client, check_addr,  sizeof(check_addr));
	if (len != sizeof(check_addr)) {
		LOGE("Check Address fail! len=%d", len);
		return -1;
	} else
		LOGI("Check Address write data successfully! cmd = [%2x, %2x, %2x, %2x]", check_addr[7], check_addr[8], check_addr[9], check_addr[10]);

	mdelay(20);

	len = i2c_master_recv(global_ts->client, buff, sizeof(buff));
	if (len != sizeof(buff)) {
		LOGE("Check Address Read Data error. len=%d", len);
		return -1;
	} else {
		printk("[ELAN_KTF] [Check Addr] [%d]: ", __LINE__);
		for (j = 0; j < 37; j++)
			printk("%x ", buff[j]);
		printk("\n");
	}

	return 0;
}

int EnterISPMode(struct i2c_client *client)
{
	int len = 0;
	uint8_t isp_cmd[] = {0x45, 0x49, 0x41, 0x50};
	len = i2c_master_send(global_ts->client, isp_cmd, 4);
	if (len != 4) {
		LOGE("Enter ISP mode failed. len=%d", len);
		return -1;
	}
	else
		LOGI("Enter ISP mode success.");
	return 0;
}

int ExtractPage(struct file *filp, uint8_t * szPage, int byte)
{
	int len = 0;

	len = filp->f_op->read(filp, szPage,byte, &filp->f_pos);
	if (len != byte) {
		LOGE("read page error, read error. len=%d", len);
		return -1;
	}

	return 0;
}

int WritePage(const u8 *szPage, int byte)
{
	int len = 0;

	len = i2c_master_send(global_ts->client, szPage, byte);
	if (len != byte) {
		LOGE("i2c_master_send failed, len = %d", len);
		return -1;
	}

	return 0;
}

int GetAckData(struct i2c_client *client)
{
	int rc = 0;
	uint8_t buff[67] = {0};

#ifdef ELAN_HID_I2C
	rc = elan_ktf_ts_poll(client);
	if (rc < 0)
		LOGE("INT poll is hight");
#endif
	rc = i2c_master_recv(global_ts->client, buff, sizeof(buff));
	if (rc != sizeof(buff)) {
		LOGE("i2c_master_recv failed, rc = %d", rc);
		LOGE("buff = %x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x", buff[0], buff[1], buff[2], buff[3], buff[4], buff[5],
			buff[6], buff[7], buff[8], buff[9], buff[10], buff[11]);
		return ACK_Fail;
	}

#ifndef ELAN_HID_I2C
	if (buff[0] == 0xaa && buff[1] == 0xaa)
		return ACK_OK;
	else if (buff[0] == 0x55 && buff[1] == 0x55)
		return ACK_REWRITE;
	else
		return ACK_Fail;
#endif

	return ACK_OK;
}

void print_progress(int page, int ic_num, int j, int PageNum)
{
	int i, percent,page_tatol=351,percent_tatol;
	char str[256];
	str[0] = '\0';
	for (i = 0; i < ((page)/10); i++) {
		str[i] = '#';
		str[i+1] = '\0';
	}
	percent = ((100*page)/(PageNum));
	if ((page) == (PageNum))
		percent = 100;

	if ((page_tatol) == (PageNum*ic_num))
		percent_tatol = 100;

	printk("progress %s| %d %d", str, percent,page);
	if (page == (PageNum))
		printk("\n");

}

#ifdef GLOVEMODE
int EnterGloveMode(void)
{
	int len = 0;
	uint8_t enter_glove[4] = {0x54, 0x57, 0x02, 0x01};

	len = i2c_master_send(global_ts->client, enter_glove, 4);
	if (len != 4) {
		LOGE("enter glove mode failed, len = %d", len);
		return -1;
	} else {
		LOGI("enter glove mode success");
		glovemode_state = true;
	}
	return 0;
}

int ExitGloveMode(void)
{
	int len = 0;
	uint8_t exit_glove[4] = {0x54, 0x57, 0x00, 0x01};

	len = i2c_master_send(global_ts->client, exit_glove, 4);
	if (len != 4) {
		LOGE("exit glove mode failed, len = %d", len);
		return -1;
	} else {
		LOGI("exit glove mode success");
		glovemode_state = false;
	}
	return 0;
}
#endif

// Star 2wireIAP which used I2C to simulate JTAG function
#ifdef ELAN_2WIREICE
static uint8_t file_bin_data[] = {
	#include "2wireice.i"
};

int write_ice_status=0;
int shift_out_16(struct i2c_client *client)
{
	int res;
	uint8_t buff[] = {0xbb,0xbb,0xbb,0xbb,0xbb,0xbb,0xbb,0xbb,0xbf,0xff};
	res = i2c_master_send(client, buff,  sizeof(buff));
	return res;
}
int tms_reset(struct i2c_client *client)
{
	int res;
	uint8_t buff[] = {0xff,0xff,0xff,0xbf};
	res = i2c_master_send(client, buff,  sizeof(buff));
	return res;
}

int mode_gen(struct i2c_client *client)
{
	int res;
	int retry = 5;
	uint8_t buff[] = {0xff,0xff,0xff,0x31,0xb7,0xb7,0x7b,0xb7,0x7b,0x7b,0xb7,0x7b,0xf3,0xbb,0xbb,0xbb,0xbb,0xbb,0xbb,0xbb,0xbb,0xf1};
	uint8_t buff_1[] = {0x2a,0x6a,0xa6,0xa6,0x6e};
	char mode_buff[2] = {0};
	do {
		res = i2c_master_send(client, buff,  sizeof(buff));
		if (res != sizeof(buff)) {
			LOGE("mode_gen write buff error, write  error. res = %d", res);
		} else {
			LOGI("mode_gen write buff successfully.");
			break;
		}
		mdelay(20);
		retry -= 1;
	} while (retry);
	res = i2c_master_recv(client, mode_buff, sizeof(mode_buff));
	if (res != sizeof(mode_buff)) {
		LOGE("mode_gen read data error, write  error. res = %d", res);
		return -1;
	} else
		LOGI("mode gen read successfully(a6 59)! buff[0] = 0x%x  buff[1] = 0x%x", mode_buff[0], mode_buff[1]);

	res = i2c_master_send(client, buff_1, sizeof(buff_1));
	if (res != sizeof(buff_1)) {
		LOGE("mode_gen write buff_1 error. res = %d", res);
		return -1;
	}
	return res;
}

int word_scan_out(struct i2c_client *client)
{
	int res;
	uint8_t buff[] = {0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x26,0x66};
	res = i2c_master_send(client, buff,  sizeof(buff));
	return res;
}

int long_word_scan_out(struct i2c_client *client)
{
	int res;
	uint8_t buff[] = {0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x26,0x66};
	res = i2c_master_send(client, buff,  sizeof(buff));
	return res;
}


int bit_manipulation(int TDI, int TMS, int TCK, int TDO,int TDI_1, int TMS_1, int TCK_1, int TDO_1)
{
	int res;
	res= ((TDI<<3 |TMS<<2 |TCK |TDO)<<4) |(TDI_1<<3 |TMS_1<<2 |TCK_1 |TDO_1);
	return res;
}

int ins_write(struct i2c_client *client, uint8_t buf)
{
	int res = 0;
	int length = 13;
	uint8_t write_buf[7] = {0};
	int TDI_bit[13] = {0};
	int TMS_bit[13] = {0};
	int i = 0;
	uint8_t buf_rev = 0;
	int TDI = 0, TMS = 0, TCK = 0,TDO = 0;
	int bit_tdi, bit_tms;
	int len;

	for (i = 0; i < 8; i++) {
		buf_rev = buf_rev | (((buf >> i) & 0x01) << (7-i));
	}

	TDI = (0x7<<10) | buf_rev <<2 |0x00;
	TMS = 0x1007;
	TCK = 0x2;
	TDO = 1;

	for (len = 0; len <= length-1; len++) {
		bit_tdi = TDI & 0x1;
		bit_tms = TMS & 0x1;
		TDI_bit[length-1-len] = bit_tdi;
		TMS_bit[length-1-len] = bit_tms;
		TDI = TDI >>1;
		TMS = TMS >>1;
	}

	for (len = 0; len <= length-1; len = len+2) {
		if (len == length-1 && len % 2 == 0)
			res = bit_manipulation(TDI_bit[len], TMS_bit[len], TCK, TDO, 0, 0, 0, 0);
		else
			res = bit_manipulation(TDI_bit[len], TMS_bit[len], TCK, TDO, TDI_bit[len+1], TMS_bit[len+1], TCK, TDO);
		write_buf[len/2] = res;
	}

	res = i2c_master_send(client, write_buf,  sizeof(write_buf));
	return res;
}

int word_scan_in(struct i2c_client *client, uint16_t buf)
{
	int res=0;
	uint8_t write_buf[10] = {0};
	int TDI_bit[20] = {0};
	int TMS_bit[20] = {0};


	int TDI =  buf <<2 |0x00;
	int TMS = 0x7;
	int TCK = 0x2;
	int TDO = 1;

	int bit_tdi, bit_tms;
	int len;

	for (len = 0; len <= 19; len++) { //length =20
		bit_tdi = TDI & 0x1;
		bit_tms = TMS & 0x1;

		TDI_bit[19-len] = bit_tdi;
		TMS_bit[19-len] = bit_tms;
		TDI = TDI >>1;
		TMS = TMS >>1;
	}

	for (len = 0; len <= 19; len = len+2) {
		if (len == 19 && len %2 == 0)
			res = bit_manipulation(TDI_bit[len], TMS_bit[len], TCK, TDO, 0,0,0,0);
		else
			res = bit_manipulation(TDI_bit[len], TMS_bit[len], TCK, TDO, TDI_bit[len+1], TMS_bit[len+1], TCK, TDO);
			write_buf[len/2] = res;
	}

	res = i2c_master_send(client, write_buf, sizeof(write_buf));
	return res;
}

int long_word_scan_in(struct i2c_client *client, int buf_1, int buf_2)
{
	uint8_t write_buf[18] = {0};
	uint8_t TDI_bit[36] = {0};
	uint8_t TMS_bit[36] = {0};

	int TDI_1 = buf_1;
	int TDI_2 = (buf_2<<2) | 0x00;
	int TMS = 0x7;
	int TCK = 0x2;
	int TDO = 1;

	int bit_tdi, bit_tms;
	int len = 0;
	int res = 0;

	for (len = 0; len <= 35; len++) { //length =36
		if (len < 18) {
			bit_tdi = TDI_2 & 0x1;
		} else {
			bit_tdi = TDI_1 & 0x1;
		}
		bit_tms = TMS & 0x1;
		TDI_bit[35-len] = bit_tdi;
		TMS_bit[35-len] = bit_tms;
		if (len < 18) {
			TDI_2 = TDI_2 >> 1;
		} else {
			TDI_1 = TDI_1 >> 1;
		}
		TMS = TMS >>1;
		bit_tdi = 0;
		bit_tms = 0;
	}

	for (len = 0; len <= 35; len = len + 2) {
		if (len == 35 && len % 2 == 0)
			res = bit_manipulation(TDI_bit[len], TMS_bit[len], TCK, TDO, 0,0,0,1);
		else
			res = bit_manipulation(TDI_bit[len], TMS_bit[len], TCK, TDO, TDI_bit[len+1], TMS_bit[len+1], TCK, TDO);
		write_buf[len/2] = res;
	}

	res = i2c_master_send(client, write_buf,  sizeof(write_buf));
	return res;
}

uint16_t trimtable[8] = {0};
int Read_SFR(struct i2c_client *client, int open)
{
	uint8_t voltage_recv[2] = {0};

	int count, ret;
	ins_write(client, 0x6f); // IO write
	long_word_scan_in(client, 0x007e, 0x0020);
	long_word_scan_in(client, 0x007f, 0x4000);
	long_word_scan_in(client, 0x007e, 0x0023);
	long_word_scan_in(client, 0x007f, 0x8000);
	ins_write(client, 0x6f); //IO Write
	long_word_scan_in(client, 0x007f, 0x9002); //TM=2h
	ins_write(client, 0x68); //Program Memory Sequential Read
	word_scan_in(client, 0x0000); //set Address 0x0000
	shift_out_16(client); //move data to I2C buf

	mdelay(10);
	count = 0;
	ret = i2c_master_recv(client, voltage_recv, sizeof(voltage_recv));
	if (ret != sizeof(voltage_recv)) {
		LOGE("read data error. ret=%d", ret);
		return -1;
	} else {
		trimtable[count]=voltage_recv[0]<<8 | voltage_recv[1];
		LOGI("read data successfully! voltage_recv buff[0]=0x%x buff[1]=0x%x trimtable[%d]=0x%x",
			voltage_recv[0], voltage_recv[1], count, trimtable[count]);
	}

	ins_write(client, 0x6f); // IO write
	long_word_scan_in(client, 0x007e, 0x0020);
	long_word_scan_in(client, 0x007f, 0x4000);
	long_word_scan_in(client, 0x007e, 0x0023);
	long_word_scan_in(client, 0x007f, 0x8000);

	ins_write(client, 0x6f);
	long_word_scan_in(client, 0x007f, 0x9002);
	ins_write(client, 0x68);
	word_scan_in(client, 0x0001);
	shift_out_16(client);

	mdelay(1);
	count = 1;
	ret = i2c_master_recv(client, voltage_recv, sizeof(voltage_recv));
	if (ret != sizeof(voltage_recv)) {
		LOGE("read data error. ret=%d", ret);
		return -1;
	} else {
		trimtable[count]=voltage_recv[0]<<8 | voltage_recv[1];
		LOGI("read data successfully! voltage_recv buff[0]=0x%x buff[1]=0x%x trimtable[%d]=0x%x",
			voltage_recv[0], voltage_recv[1], count, trimtable[count]);
	}

	ins_write(client, 0x6f); // IO write
	long_word_scan_in(client, 0x007e, 0x0020);
	long_word_scan_in(client, 0x007f, 0x4000);
	long_word_scan_in(client, 0x007e, 0x0023);
	long_word_scan_in(client, 0x007f, 0x8000);

	ins_write(client, 0x6f);
	long_word_scan_in(client, 0x007f, 0x9002);
	ins_write(client, 0x68);
	word_scan_in(client, 0x0002);
	shift_out_16(client);

	mdelay(1);
	count = 2;
	ret = i2c_master_recv(client, voltage_recv, sizeof(voltage_recv));
	if (ret != sizeof(voltage_recv)) {
		LOGE("read data error. ret=%d", ret);
		return -1;
	} else {
		trimtable[count]=voltage_recv[0]<<8 | voltage_recv[1];
		LOGI("read data successfully! voltage_recv buff[0]=0x%x buff[1]=0x%x trimtable[%d]=0x%x",
			voltage_recv[0], voltage_recv[1], count, trimtable[count]);
	}

	ins_write(client, 0x6f); // IO write
	long_word_scan_in(client, 0x007e, 0x0020);
	long_word_scan_in(client, 0x007f, 0x4000);
	long_word_scan_in(client, 0x007e, 0x0023);
	long_word_scan_in(client, 0x007f, 0x8000);

	ins_write(client, 0x6f);
	long_word_scan_in(client, 0x007f, 0x9002);
	ins_write(client, 0x68);
	word_scan_in(client, 0x0003);
	shift_out_16(client);

	mdelay(1);
	count = 3;
	ret = i2c_master_recv(client, voltage_recv, sizeof(voltage_recv));
	if (ret != sizeof(voltage_recv)) {
		LOGE("read data error. ret=%d", ret);
		return -1;
	} else {
		trimtable[count]=voltage_recv[0]<<8 | voltage_recv[1];
		LOGI("read data successfully! voltage_recv buff[0]=0x%x buff[1]=0x%x trimtable[%d]=0x%x",
			voltage_recv[0], voltage_recv[1], count, trimtable[count]);
	}

	ins_write(client, 0x6f); // IO write
	long_word_scan_in(client, 0x007e, 0x0020);
	long_word_scan_in(client, 0x007f, 0x4000);
	long_word_scan_in(client, 0x007e, 0x0023);
	long_word_scan_in(client, 0x007f, 0x8000);

	ins_write(client, 0x6f);
	long_word_scan_in(client, 0x007f, 0x9002);
	ins_write(client, 0x68);
	word_scan_in(client, 0x0004);
	shift_out_16(client);

	mdelay(1);
	count = 4;
	ret = i2c_master_recv(client, voltage_recv, sizeof(voltage_recv));
	if (ret != sizeof(voltage_recv)) {
		LOGE("read data error. ret=%d", ret);
		return -1;
	} else {
		trimtable[count]=voltage_recv[0]<<8 | voltage_recv[1];
		LOGI("read data successfully! voltage_recv buff[0]=0x%x buff[1]=0x%x trimtable[%d]=0x%x",
			voltage_recv[0], voltage_recv[1], count, trimtable[count]);
	}

	ins_write(client, 0x6f); // IO write
	long_word_scan_in(client, 0x007e, 0x0020);
	long_word_scan_in(client, 0x007f, 0x4000);
	long_word_scan_in(client, 0x007e, 0x0023);
	long_word_scan_in(client, 0x007f, 0x8000);

	ins_write(client, 0x6f);
	long_word_scan_in(client, 0x007f, 0x9002);
	ins_write(client, 0x68);
	word_scan_in(client, 0x0005);
	shift_out_16(client);

	mdelay(1);
	count = 5;
	ret = i2c_master_recv(client, voltage_recv, sizeof(voltage_recv));
	if (ret != sizeof(voltage_recv)) {
		LOGE("read data error. ret=%d", ret);
		return -1;
	} else {
		trimtable[count]=voltage_recv[0]<<8 | voltage_recv[1];
		LOGI("read data successfully! voltage_recv buff[0]=0x%x buff[1]=0x%x trimtable[%d]=0x%x",
			voltage_recv[0], voltage_recv[1], count, trimtable[count]);
	}

	ins_write(client, 0x6f); // IO write
	long_word_scan_in(client, 0x007e, 0x0020);
	long_word_scan_in(client, 0x007f, 0x4000);
	long_word_scan_in(client, 0x007e, 0x0023);
	long_word_scan_in(client, 0x007f, 0x8000);

	ins_write(client, 0x6f);
	long_word_scan_in(client, 0x007f, 0x9002);
	ins_write(client, 0x68);
	word_scan_in(client, 0x0006);
	shift_out_16(client);

	mdelay(1);
	count = 6;
	ret = i2c_master_recv(client, voltage_recv, sizeof(voltage_recv));
	if (ret != sizeof(voltage_recv)) {
		LOGE("read data error. ret=%d", ret);
		return -1;
	} else {
		trimtable[count]=voltage_recv[0]<<8 | voltage_recv[1];
		LOGI("read data successfully! voltage_recv buff[0]=0x%x buff[1]=0x%x trimtable[%d]=0x%x",
			voltage_recv[0], voltage_recv[1], count, trimtable[count]);
	}
	//  7
	ins_write(client, 0x6f); // IO write
	long_word_scan_in(client, 0x007e, 0x0020);
	long_word_scan_in(client, 0x007f, 0x4000);
	long_word_scan_in(client, 0x007e, 0x0023);
	long_word_scan_in(client, 0x007f, 0x8000);

	ins_write(client, 0x6f);
	long_word_scan_in(client, 0x007f, 0x9002);
	ins_write(client, 0x68);
	word_scan_in(client, 0x0007);
	shift_out_16(client);

	mdelay(1);
	count=7;
	ret = i2c_master_recv(client, voltage_recv, sizeof(voltage_recv));
	if (ret != sizeof(voltage_recv)) {
		LOGE("read data error. ret=%d", ret);
		return -1;
	}
	if (open == 1)
		trimtable[count]=voltage_recv[0]<<8 |  (voltage_recv[1] & 0xbf);
	else
		trimtable[count]=voltage_recv[0]<<8 | (voltage_recv[1] | 0x40);
	LOGE("Open_High_Voltage recv  voltage_recv buff[0]=%x buff[1]=%x, trimtable[%d]=%x",
		voltage_recv[0],voltage_recv[1], count, trimtable[count]);


	ins_write(client, 0x6f);
	long_word_scan_in(client, 0x007f, 0x8000);

	return 0;
}

int Write_SFR_2k(struct i2c_client *client, int open)
{
	//set page 1
	ins_write(client, 0x6f);
	long_word_scan_in(client, 0x0001, 0x0100);
	if (open == 1) {
		//set HV enable
		LOGI("set HV enable");
		ins_write(client, 0x6f);
		long_word_scan_in(client, 0x0050, 0xc041);
	} else {
		//set HV disable
		LOGI("set HV disable");
		ins_write(client, 0x6f);
		long_word_scan_in(client, 0x0050, 0xc040);
	}
	return 0;
}

int Write_SFR(struct i2c_client *client)
{

	ins_write(client, 0x6f);
	long_word_scan_in(client, 0x007f, 0x9001);

	ins_write(client, 0x66); // Program Memory Write
	long_word_scan_in(client, 0x0000, trimtable[0]);
	ins_write(client, 0xfd); //Set up the initial addr for sequential access
	word_scan_in(client,0x7f);

	ins_write(client, 0x66);
	long_word_scan_in(client, 0x0001, trimtable[1]);
	ins_write(client, 0xfd);
	word_scan_in(client,0x7f);

	ins_write(client, 0x66);
	long_word_scan_in(client, 0x0002, trimtable[2]);
	ins_write(client, 0xfd);
	word_scan_in(client,0x7f);

	ins_write(client, 0x66);
	long_word_scan_in(client, 0x0003, trimtable[3]);
	ins_write(client, 0xfd);
	word_scan_in(client,0x7f);

	ins_write(client, 0x66);
	long_word_scan_in(client, 0x0004, trimtable[4]);
	ins_write(client, 0xfd);
	word_scan_in(client,0x7f);

	ins_write(client, 0x66);
	long_word_scan_in(client, 0x0005, trimtable[5]);
	ins_write(client, 0xfd);
	word_scan_in(client,0x7f);

	ins_write(client, 0x66);
	long_word_scan_in(client, 0x0006, trimtable[6]);
	ins_write(client, 0xfd);
	word_scan_in(client,0x7f);

	ins_write(client, 0x66);
	long_word_scan_in(client, 0x0007, trimtable[7]);
	ins_write(client, 0xfd);
	word_scan_in(client,0x7f);

	ins_write(client, 0x6f);
	long_word_scan_in(client, 0x7f, 0x8000);

	return 0;
}

int Enter_Mode(struct i2c_client *client)
{
	mode_gen(client);
	tms_reset(client);
	ins_write(client,0xfc); //system reset
	tms_reset(client);
	return 0;
}
int Open_High_Voltage(struct i2c_client *client, int open)
{
#ifdef EKTF3K_FLASH
	Read_SFR(client, open);
	Write_SFR(client);
	Read_SFR(client, open);
#endif
	Write_SFR_2k(client, open);
	return 0;
}

int Mass_Erase(struct i2c_client *client)
{
	char mass_buff[4]={0};
	char mass_buff_1[2]={0};
	int ret, finish=0, i=0;
	LOGI("Mass_Erase!!");
	ins_write(client,0x01); //id code read
	mdelay(2);
	long_word_scan_out(client);

	ret = i2c_master_recv(client, mass_buff, sizeof(mass_buff));
	LOGI("Mass_Erase mass_buff=%x %x %x %x(c0 08 01 00)", mass_buff[0],mass_buff[1],mass_buff[2],mass_buff[3]); //id: c0 08 01 00

	ins_write(client,0x6f); //IO Write
	/*add by herman*/
	long_word_scan_in(client,0x007e,0x0020);

	long_word_scan_in(client,0x007f,0x4000); //orig 4000
	long_word_scan_in(client,0x007e,0x0023);
	long_word_scan_in(client,0x007f,0x8000);
	ins_write(client,0x6f);
	long_word_scan_in(client,0x007f,0x9040);
	ins_write(client,0x66); //Program data Write
	long_word_scan_in(client, 0x0000,0x8765);//change by herman
	ins_write(client,0x6f); //IO Write
	long_word_scan_in(client, 0x007f,0x8000); //clear flash control PROG

	ins_write(client,0xf3);

	while (finish == 0) {
		word_scan_out(client);
		ret = i2c_master_recv(client, mass_buff_1, sizeof(mass_buff_1));
		if (ret != sizeof(mass_buff_1)) {
			LOGE("read data error. res=%d", ret);
			return -1;
		} else {
			finish = (mass_buff_1[1] >> 4 ) & 0x01;
			LOGI("mass_buff_1[0]=%x, mass_buff_1[1]=%x (80 10) finish=%d", mass_buff_1[0], mass_buff_1[1], finish); //80 10: OK, 80 00: fail
		}
		if (mass_buff_1[1] != I2C_DATA[0] && finish != 1 && i < 100) {
			mdelay(100);
			i++;
			if (i == 50) {
				LOGE("Mass_Erase fail!");
				return -1;
			}
		}
	}

	return 0;
}

int Reset_ICE(struct i2c_client *client)
{
	int res;
	LOGI("Reset ICE!");
	ins_write(client, 0x94);
	ins_write(client, 0xd4);
	ins_write(client, 0x20);
	client->addr = I2C_DATA[0];//Modify address before 2-wire
	elan_ktf_ts_hw_reset();
	mdelay(250);
	res = __hello_packet_handler(client);

	return 0;
}

int normal_write_func(struct i2c_client *client, int j, uint8_t *szBuff)
{
	uint16_t szbuff=0, szbuff_1=0;
	uint16_t sendbuff=0;
	int write_byte, iw;

	ins_write(client,0xfd);
	word_scan_in(client, j*64);

	ins_write(client,0x65);// Program data sequential write

	write_byte = 64;

	for (iw = 0; iw < write_byte; iw++) {
		szbuff = *szBuff;
		szbuff_1 = *(szBuff+1);
		sendbuff = szbuff_1 <<8 |szbuff;
		LOGI("Write Page sendbuff=0x%04x", sendbuff);
		//mdelay(1);
		word_scan_in(client, sendbuff); //data????   buff_read_data
		szBuff+=2;
	}
	return 0;
}

int fastmode_write_func(struct i2c_client *client, int j, uint8_t *szBuff)
{
	uint8_t szfwbuff=0, szfwbuff_1=0;
	uint8_t sendfwbuff[130]={0};
	uint8_t tmpbuff;
	int i=0, len=0;
	global_ts->client->addr = 0x76;

	sendfwbuff[0] = (j*64)>>8;
	tmpbuff = ((j*64)<< 8) >> 8;
	sendfwbuff[1] = tmpbuff;

	for (i = 2;i < 129; i = i+2) { // 1 Page = 64 word, 1 word=2Byte
		szfwbuff = *szBuff;
		szfwbuff_1 = *(szBuff+1);
		sendfwbuff[i] = szfwbuff_1;
		sendfwbuff[i+1] = szfwbuff;
		szBuff+=2;
	}

	len = i2c_master_send(global_ts->client, sendfwbuff,  130);
	if (len != 130) { //address+data(128)
		LOGE("fastmode write page error, write error. len=%d, Page %d", len, j);
		return -1;
	}

	global_ts->client->addr = 0x77;

	return 0;
}

int ektSize;
int lastpage_byte;
int lastpage_flag=0;
int Write_Page(struct i2c_client *client, int j, uint8_t *szBuff)
{
	int len, finish=0;
	char buff_read_data[2];
	int i=0;

	ins_write(client,0x6f); //IO Write
	long_word_scan_in(client,0x007f,0x8000);
	long_word_scan_in(client,0x007f,0x9400);

	ins_write(client,0x66); //Program Data Write
	long_word_scan_in(client, j*64,0x0000);
	fastmode_write_func(client, j, szBuff);

	ins_write(client,0x6f);
	long_word_scan_in(client,0x007f,0x9000);
	long_word_scan_in(client,0x007f,0x8000);

	ins_write(client, 0xf3);  //Debug Reg Read

	while (finish == 0) {
		word_scan_out(client);
		len = i2c_master_recv(client, buff_read_data, sizeof(buff_read_data));
		if (len != sizeof(buff_read_data)) {
			LOGE("Write_Page read buff_read_data error, len=%d", len);
			return E_FD;
		} else {
			finish = (buff_read_data[1] >> 4 ) & 0x01;
		}
		if (finish != 1) {
			mdelay(10);
			i++;
			if (i == 50) {
				LOGE("Write_Page finish !=1, Page=%d", j);
				write_ice_status=1;
				return -1;
			}
		}

	}
	return 0;
}

int fastmode_read_func(struct i2c_client *client, int j, uint8_t *szBuff)
{
	uint8_t szfrbuff=0, szfrbuff_1=0;
	uint8_t sendfrbuff[2]={0};
	uint8_t recvfrbuff[130]={0};
	uint16_t tmpbuff;
	int i=0, len=0, retry=0;

	ins_write(client,0x67);

	global_ts->client->addr = 0x76;
	sendfrbuff[0] = (j*64)>>8;
	tmpbuff = ((j*64)<< 8) >> 8;
	sendfrbuff[1] = tmpbuff;

	len = i2c_master_send(global_ts->client, sendfrbuff,  sizeof(sendfrbuff));
	len = i2c_master_recv(global_ts->client, recvfrbuff,  sizeof(recvfrbuff));

	for (i = 2; i < 129; i = i + 2) {
		szfrbuff=*szBuff;
		szfrbuff_1=*(szBuff+1);
		szBuff+=2;
		if (recvfrbuff[i] != szfrbuff_1 || recvfrbuff[i+1] != szfrbuff) {
			LOGE("Read Page Compare Fail. recvfrbuff[%d]=%x, recvfrbuff[i+1]=%x, szfrbuff_1=%x, szfrbuff=%x, ,j =%d",
				i, recvfrbuff[i], recvfrbuff[i+1], szfrbuff_1, szfrbuff, j);
			write_ice_status=1;
			retry=1;
		}
		break;
	}

	global_ts->client->addr = 0x77;
	if (retry == 1) {
		return -1;
	}
	return 0;
}

int normal_read_func(struct i2c_client *client, int j,  uint8_t *szBuff)
{
	char read_buff[2];
	int m, len, read_byte;
	uint16_t szbuff=0, szbuff_1=0;

	ins_write(client,0xfd);
	word_scan_in(client, j*64);
	ins_write(client,0x67);
	word_scan_out(client);

	read_byte = 64;

	for (m = 0; m < read_byte; m++) {
		word_scan_out(client);
		len = i2c_master_recv(client, read_buff, sizeof(read_buff));
		szbuff = *szBuff;
		szbuff_1 = *(szBuff+1);
		szBuff += 2;
		LOGI("Read Page: byte=%x%x, szbuff=%x%x", read_buff[0], read_buff[1],szbuff, szbuff_1);

		if (read_buff[0] != szbuff_1 || read_buff[1] != szbuff) {
			LOGE("Read Page Compare Fail. j =%d. m=%d", j, m);
			write_ice_status=1;
		}
	}
	return 0;
}

int Read_Page(struct i2c_client *client, int j,  uint8_t *szBuff)
{
	int res=0;
	ins_write(client,0x6f);
	long_word_scan_in(client,0x007f,0x9000);
	ins_write(client,0x68);

	fastmode_read_func(client, j,  szBuff);

	ins_write(client,0x6f);
	long_word_scan_in(client,0x007f,0x0000);
	if (res == -1) {
		return -1;
	}
	return 0;

}

int TWO_WIRE_ICE(struct i2c_client *client)
{
	int i;
	uint8_t *szBuff = NULL;
	int curIndex = 0;
	int PageSize=128;
	int res;

	write_ice_status=0;
	ektSize = sizeof(file_bin_data) /PageSize;
	client->addr = 0x77;//Modify address before 2-wire

	LOGI("ektSize=%d ,modify address = %x", ektSize, client->addr);

	i = Enter_Mode(client);
	i = Open_High_Voltage(client, 1);
	if (i == -1) {
		LOGE("Open High Voltage fail");
		return -1;
	}

	i = Mass_Erase(client); //mark temp
	if (i == -1) {
		LOGE("Mass Erase fail");
		return -1;
	}

	//for fastmode
	ins_write(client,0x6f);
	long_word_scan_in(client, 0x007e, 0x0036);
	long_word_scan_in(client, 0x007f, 0x8000);
	long_word_scan_in(client, 0x007e, 0x0023); //add by herman

	// client->addr = 0x76;////Modify address before 2-wire
	LOGI("client->addr = %2x", client->addr);
	// for fastmode
	for (i = 0 ; i<ektSize; i++) {
		szBuff = file_bin_data + curIndex;
		curIndex =  curIndex + PageSize;

		res = Write_Page(client, i, szBuff);
		if (res == -1) {
			LOGE("Write_Page %d fail", i);
			break;
		}
		mdelay(1);
		Read_Page(client,i, szBuff);
	}

	if (write_ice_status == 0)
		LOGI("Update_FW_Boot Finish!");
	else
		LOGI("Update_FW_Boot fail!");

	i = Open_High_Voltage(client, 0);
	if (i == -1) return -1;

	Reset_ICE(client);

	return 0;
}

int elan_TWO_WIRE_ICE( struct i2c_client *client) // for driver internal 2-wire ice
{
	work_lock = 1;
	disable_irq(global_ts->client->irq);
	TWO_WIRE_ICE(client);
	work_lock = 0;
	enable_irq(global_ts->client->irq);

	return 0;
}
// End 2WireICE
#endif


int CheckISPstatus(struct i2c_client *client)
{
	int len = 0;
	int j;
	uint8_t checkstatus[37] = {0x04, 0x00, 0x23, 0x00, 0x03, 0x18};
	uint8_t buff[67] = {0};

	len = i2c_master_send(global_ts->client, checkstatus, sizeof(checkstatus));
	if (len != sizeof(checkstatus)) {
		LOGE("Flash key fail! len=%d", len);
		return -1;
	} else
		LOGI("check status write data successfully! cmd = [%x, %x, %x, %x, %x, %x]",
			checkstatus[0], checkstatus[1], checkstatus[2], checkstatus[3], checkstatus[4], checkstatus[5]);

	mdelay(10);
	len=i2c_master_recv(global_ts->client, buff, sizeof(buff));
	if (len != sizeof(buff)) {
		LOGE("Check Address Read Data error. len=%d", len);
		return -1;
	} else {
		printk("[ELAN_KTF] [Check status] [%d]: ", __LINE__);
		for (j = 0; j < 37; j++)
			printk("%x ", buff[j]);
		printk("\n");
		if (buff[6] == 0x88)
			return 0x88; /* return recovery mode 0x88 */
	}

	return 0;
}

int RecoveryISP(struct i2c_client *client)
{
	int len = 0;
	int j;
	uint8_t flash_key[37] = {0x04, 0x00, 0x23, 0x00, 0x03, 0x00, 0x04, 0x54, 0xc0, 0xe1, 0x5a};
	uint8_t check_addr[37] = {0x04, 0x00, 0x23, 0x00, 0x03, 0x00, 0x01, 0x10};
	uint8_t buff[67] = {0};


	len = i2c_master_send(global_ts->client, flash_key,  37);
	if (len != 37) {
		LOGE("Flash key fail! len=%d", len);
		return -1;
	} else
		LOGI("FLASH key write data successfully! cmd = [%2x, %2x, %2x, %2x]",
			flash_key[7], flash_key[8], flash_key[9], flash_key[10]);

	mdelay(20);

	mdelay(20);
	len = i2c_master_send(global_ts->client, check_addr,  sizeof(check_addr));
	if (len != sizeof(check_addr)) {
		LOGE("Check Address fail! len=%d", len);
		return -1;
	} else
		LOGI("Check Address write data successfully! cmd = [%2x, %2x, %2x, %2x]",
			check_addr[7], check_addr[8], check_addr[9], check_addr[10]);

	mdelay(20);
	len = i2c_master_recv(global_ts->client, buff, sizeof(buff));
	if (len != sizeof(buff)) {
		LOGE("Check Address Read Data error. len=%d", len);
		return -1;
	} else {
		printk("[ELAN_KTF] [Check Addr] [%d]: ", __LINE__);
		for (j = 0; j < 37; j++)
			printk("%x ", buff[j]);
		printk("\n");
	}

	return 0;
}

int SendEndCmd(struct i2c_client *client)
{
	int len = 0;
	uint8_t send_cmd[37] = {0x04, 0x00, 0x23, 0x00, 0x03, 0x1A};

	len = i2c_master_send(global_ts->client, send_cmd, sizeof(send_cmd));
	if (len != sizeof(send_cmd)) {
		LOGE("Send Cmd fail! len=%d", len);
		return -1;
	} else
		LOGI("check status write data successfully! cmd = [%x, %x, %x, %x, %x, %x]",
			send_cmd[0], send_cmd[1], send_cmd[2], send_cmd[3], send_cmd[4], send_cmd[5]);

	return 0;
}

static int elan_ktf_ts_get_data(struct i2c_client *client, uint8_t *cmd, uint8_t *buf, size_t w_size,  size_t r_size)
{
	int rc;

	if (buf == NULL)
		return -EINVAL;

	if ((i2c_master_send(client, cmd, w_size)) != w_size) {
		LOGE("i2c_master_send failed");
		return -EINVAL;
	}

	rc = elan_ktf_ts_poll(client);
	if (rc < 0)
		LOGE("INT poll is hight");

	if (r_size <= 0) r_size = w_size;

	if (i2c_master_recv(client, buf, r_size) != r_size) {
		LOGE("i2c_master_recv failed");
		return -EINVAL;
	}

	return 0;
}

int CheckIapMode(int checkcnt)
{
	char buff[4] = {0};
	int len = -1;

	len = i2c_master_recv(global_ts->client, buff, sizeof(buff));
	if (sizeof(buff) != len) {
		LOGE("i2c recv failed, len = %d, checkcnt = %d", len, checkcnt);
		return -1;
	} else {
		if((buff[0] == Check_IAPMODE_BUF0) && (buff[1] == Check_IAPMODE_BUF1)
			&& (buff[2] == Check_IAPMODE_BUF2) && (buff[3] == Check_IAPMODE_BUF3)) {
			LOGI("Check IAP mode success, checkcnt = %d", checkcnt);
			return 0;
		} else {
			LOGE("Check IAP mode failed, buffer = 0x%02X 0x%02X 0x%02X 0x%02X, checkcnt = %d",
				 buff[0], buff[1], buff[2], buff[3], checkcnt);
			return -1;
		}
	}
	return 0;
}

static int FW_Update(struct device *dev, bool manual)
{
	const struct firmware *p_fw_entry;
	const u8 *szBuff;
	const u8 *fw_data;
	int res = -1,ic_num = 1;
	int iPage = 0, rewriteCnt = 0; //rewriteCnt for PAGE_REWRITE
	int i = 0;
	uint8_t data;
	int restartCnt = 0; // For IAP_RESTART
	int byte_count;
	int curIndex = 0;
	int checkcnt = 0;
	int checksumcnt = 0;
	int recoverycnt = 0;
	int fw_size;
	int New_FW_ID, New_FW_VER;
	int PageNum = 0;
	update_fw_flag = 1;
	fwUploadResult = false;
	need_upgrade_flag = false;
	work_lock = 1;
	power_lock = 1;

#ifdef ESD_CHECK
	live_state = 1;
#endif

	mutex_lock(&ktf_mutex);
	disable_irq(global_ts->client->irq);

	if (global_ts->elan_is_suspend) {
		LOGI("in suspend state, can not update firmware.");
		goto Update_end;
	}

	LOGI("TP ID = 0x%2.2X, TP FW ID = 0x%4.4X", TP_ID, TP_FW_ID);
	/* Star Request Firmware */
	if (TP_ID == TRULY_TP_ID) {
		LOGI("Request firmware %s",ELAN_TRULY_FW_FILENAME);
		res = request_firmware(&p_fw_entry, ELAN_TRULY_FW_FILENAME, dev);
		if (res != 0) {
			LOGE("request firmware fail, res = %d", res);
			goto Update_end;
		}
	} else if (TP_ID == HOLITECH_TP_ID) {
		LOGI("Request firmware %s",ELAN_HOLITECH_FW_FILENAME);
		res = request_firmware(&p_fw_entry, ELAN_HOLITECH_FW_FILENAME, dev);
		if (res != 0) {
			LOGE("request firmware fail, res = %d", res);
			goto Update_end;
		}
	}
	LOGI("Firmware Size = %zu", p_fw_entry->size);

	fw_data = p_fw_entry->data;
	fw_size = p_fw_entry->size;
	PageNum = fw_size/PageSize;
	/* End Request Firmware */
	LOGI("PageNum = %d", PageNum);

	res = CalculateCheckSum(fw_data, PageNum);
	if (res < 0) {
		LOGE("Build-in firmware checksum failed");
		goto Update_end;
	}

	New_FW_ID = fw_data[0x7d67] << 8 | fw_data[0x7d66];
	New_FW_VER = fw_data[0x7d65] << 8 | fw_data[0x7d64];
	LOGI("FW_ID = 0x%x, New_FW_ID = 0x%x", FW_ID, New_FW_ID);
	LOGI("FW_VERSION = 0x%x, New_FW_VER = 0x%x", FW_VERSION, New_FW_VER);

	if (!manual) {
		if (FW_ID != TP_FW_ID) {
			need_upgrade_flag = true;
			LOGI("FW ID is wrong!");
		} else {
			if (New_FW_ID == TP_FW_ID) {
				if (New_FW_VER > FW_VERSION) {
					need_upgrade_flag = true;
					LOGI("IC's FW is old.");
				} else {
					LOGI("IC's FW is new.");
				}
				if (RECOVERY == 0x80) {
					need_upgrade_flag = true;
					LOGI("In recovery mode, need to update.");
				}
			} else {
				LOGE("NEW FW ID is wrong!");
				goto Update_end;
			}
		}
	} else {
		need_upgrade_flag = true;
		need_check_touch_mode = true;
		LOGI("Force to update");
	}

	if (!need_upgrade_flag) {
		LOGI("Check IC Firmware checksum");
		res = getCheckSUM(global_ts->client);
		if (res < 0)
			need_upgrade_flag = true;
	}

	if (need_upgrade_flag) {
IAP_RESTART:
		curIndex = 0;
		data = I2C_DATA[0];//Master

		if (RECOVERY != 0x80) {
			LOGI("Start Firmware upgrade in normal mode!");
		} else
			LOGI("Start Firmware upgrade in recovery mode!");

		/* Send enter bootcode cmd */
		elan_ktf_ts_hw_reset();
		mdelay(13);

		/* enter ISP mode */
		res = EnterISPMode(global_ts->client);
		if (res) {
			LOGE("enter ISP Mode failed");
			goto Update_end;
		}
		mdelay(20);

		res = CheckIapMode(checkcnt+1);
		if (res < 0) {
			checkcnt++;
			if (checkcnt < 5) {
				LOGE("Check IAP Failed, go to restart");
				goto IAP_RESTART;
			} else {
				LOGE("Check IAP Fail 5 times!");
				goto Update_end;
			}
		}

		/* Send Dummy Byte */
		res = i2c_master_send(global_ts->client, &data, sizeof(data));
		if (res != sizeof(data)) {
			LOGE("dummy error code = %d", res);
			goto Update_end;
		}

		// Start IAP
		for (iPage = 1; iPage <= PageNum; iPage++) {
PAGE_REWRITE:
			for (byte_count = 1; byte_count <= 17; byte_count++) {
				if (byte_count != 17) {
					szBuff = fw_data + curIndex;
					curIndex =  curIndex + 8;
					res = WritePage(szBuff, 8);
					if (res)
						LOGE("write page failed, byte_count = %d", byte_count);
				} else {
					szBuff = fw_data + curIndex;
					curIndex =  curIndex + 4;
					res = WritePage(szBuff, 4);
					if (res)
						LOGE("write page failed, byte_count = %d", byte_count);
				}
			}

			if (iPage == PageNum)
				mdelay(500);
			else
				mdelay(40);

			res = GetAckData(global_ts->client);
			if (res != ACK_OK) {
				LOGE("ACK failed, res = %d", res);
				if (res == ACK_REWRITE) {
					rewriteCnt = rewriteCnt + 1;
					if (rewriteCnt == PAGERETRY) {
						LOGE("ID 0x%02x %dth page ReWrite %d times fails!", data, iPage, PAGERETRY);
						goto Update_end;
					} else {
						LOGE("---%d--- page ReWrite %d times!",  iPage, rewriteCnt);
						curIndex = curIndex - PageSize;
						goto PAGE_REWRITE;
					}
				} else {
					restartCnt = restartCnt + 1;
					if (restartCnt >= 5) {
						LOGE("ID 0x%02x ReStart %d times fails!", data, IAPRESTART);
						goto Update_end;
					} else {
						LOGE("===%d=== page ReStart %d times!",  iPage, restartCnt);
						goto IAP_RESTART;
					}
				}
			} else {
				rewriteCnt = 0;
				if (debug_trace)
					print_progress(iPage,ic_num,i,PageNum);
			}
		}

		RECOVERY = 0x00;
		res = __hello_packet_handler(global_ts->client);
		if (res == 0) {
			LOGI("hello packet success");
			res = getCheckSUM(global_ts->client);
			if (res < 0) {
				checksumcnt ++;
				if (checksumcnt < 3) {
					LOGE("Checksum failed, go to restart, checksumcnt = %d", checksumcnt);
					goto IAP_RESTART;
				}
			} else if (res == 0) {
				fwUploadResult = true;
			}
		} else if (res == 0x80) {
			recoverycnt++;
			if (recoverycnt < 2) {
				LOGI("hello packet (Recovery), go to restart");
				goto IAP_RESTART;
			}
		} else {
			LOGE("hello packet failed, res = %d", res);
		}
	}

Update_end:
	if (need_upgrade_flag) {
		__fw_packet_handler(global_ts->client);
		if (fwUploadResult)
			LOGI("Update Firmware success");
		else
			LOGE("Update Firmware failed");
	} else {
		fwUploadResult = true;
		LOGI("Do not need to update.");
	}

	enable_irq(global_ts->client->irq);
	mutex_unlock(&ktf_mutex);

	elan_ktf_check_touch_mode();

	update_fw_flag = 0;
	work_lock = 0;
	power_lock = 0;
	return res;
}

static ssize_t show_gpio_int(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", gpio_get_value(global_ts->irq_gpio));
}

static ssize_t show_reset(struct device *dev, struct device_attribute *attr, char *buf)
{
	elan_ktf_ts_hw_reset();
	return sprintf(buf, "Reset Touch Screen Controller \n");
}

static ssize_t show_enable_irq(struct device *dev, struct device_attribute *attr, char *buf)
{
	work_lock = 0;
	enable_irq(global_ts->client->irq);
	wake_unlock(&global_ts->wakelock);
	return sprintf(buf, "Enable IRQ \n");
}

static ssize_t show_disable_irq(struct device *dev, struct device_attribute *attr, char *buf)
{
	work_lock = 1;
#ifdef ESD_CHECK
	live_state = 1;
#endif
	disable_irq(global_ts->client->irq);
	wake_lock(&global_ts->wakelock);
	return sprintf(buf, "Disable IRQ \n");
}

static ssize_t show_calibrate(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	int ret = 0;

	ret = elan_ktf_ts_calibrate(client);
	return sprintf(buf, "%s\n", (ret == 0) ? "Calibrate Finish" : "Calibrate Fail");
}

static ssize_t show_tpid(struct device *dev, struct device_attribute *attr, char *buf)
{
	TP_ID_GPIO_VALUE = gpio_get_value(global_ts->tpid_gpio);
	if (TP_ID_GPIO_VALUE == TRULY_TP_ID) {
		TP_ID = TRULY_TP_ID;
		TP_FW_ID = TRULY_FW_ID;
		LOGI("This is truly module, TP ID = 0x%2.2X, TP FW ID = 0x%4.4X", TP_ID, TP_FW_ID);
	} else if (TP_ID_GPIO_VALUE == HOLITECH_TP_ID) {
		TP_ID = HOLITECH_TP_ID;
		TP_FW_ID = HOLITECH_FW_ID;
		LOGI("This is holitech module, TP ID = 0x%2.2X, TP FW ID = 0x%4.4X", TP_ID, TP_FW_ID);
	}
	return sprintf(buf, "%s\n", (TP_ID == 1) ? "Truly" : "Holitech");
}

static ssize_t show_fw_update(struct device *dev, struct device_attribute *attr, char *buf)
{
	LOGI("enter, Update firmware result = %s", fwUploadResult ? "success" : "failed");
	return sprintf(buf, "%s\n", fwUploadResult ? "success" : "failed");
}

static ssize_t store_fw_update(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int mode = 0;
	int ret;
	bool manualUpgrade = false;

	LOGI("enter, buf = %s", buf);

	sscanf(buf, "%d", &mode);
	manualUpgrade = mode == FW_UPDATE_MANUAL_MODE;

	if (global_ts->elan_is_suspend) {
		LOGI("In suspend state, can not update FW");
		fwUploadResult = false;
	} else {
		ret = FW_Update(dev, manualUpgrade);
	}

	return count;
}

static ssize_t show_checksum(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t ret;
	uint8_t sendCS[4] = {0x53, 0x70, 0x00, 0x01};
	uint8_t recvCS[4] = {0};
	uint8_t sendEP[4] = {0x53, 0xF4, 0x00, 0x01};
	uint8_t recvEP[4] = {0};
	char CheckSum[2] = {0};
	int rc;

	if (global_ts->elan_is_suspend) {
		LOGI("in suspend state, can not get checksum.");
		ret = sprintf(buf, "IN-SUSPEND-MODE\n");
		return ret;
	}

	if (!update_fw_flag) {
#ifdef ESD_CHECK
		live_state = 1;
#endif
		disable_irq(global_ts->client->irq);
	}

	rc = elan_ktf_ts_get_data(global_ts->client, sendCS, recvCS, 4, 4);
	if (rc < 0) {
		LOGE("recv CS failed! %2.2X %2.2X %2.2X %2.2X", recvCS[0] , recvCS[1] , recvCS[2] , recvCS[3]);
		return -1;
	} else
		LOGI("recv CS success, %2.2X %2.2X %2.2X %2.2X", recvCS[0] , recvCS[1] , recvCS[2] , recvCS[3]);

	rc = elan_ktf_ts_get_data(global_ts->client, sendEP, recvEP, 4, 4);
	if (rc < 0) {
		LOGE("recv EP failed! %2.2X %2.2X %2.2X %2.2X", recvEP[0] , recvCS[1] , recvEP[2] , recvEP[3]);
		return -1;
	} else
		LOGI("recv EP success, %2.2X %2.2X %2.2X %2.2X", recvEP[0] , recvEP[1] , recvEP[2] , recvEP[3]);

	if (!update_fw_flag)
		enable_irq(global_ts->client->irq);

	CheckSum[0]= ((recvEP[1] & 0x0F) << 4) | ((recvEP[2] & 0xF0) >> 4);
	CheckSum[1]= ((recvEP[2] & 0x0F) << 4) | ((recvEP[3] & 0xF0) >> 4);

	LOGI("EP CheckSum = 0x%2.2X%2.2X, CheckSum = 0x%2.2X%2.2X", CheckSum[0], CheckSum[1], recvCS[2] , recvCS[3]);
	ret = sprintf(buf, "EP0x%2.2X%2.2X-CS0x%2.2X%2.2X\n", CheckSum[0], CheckSum[1], recvCS[2] , recvCS[3]);
	return ret;
}

#ifdef GLOVEMODE
static ssize_t store_glove_mode(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int mode = 0;

	LOGI("enter, buf = %s", buf);
	sscanf(buf, "%d", &mode);

#ifdef ESD_CHECK
	live_state = 1;
#endif
	if (global_ts->elan_is_suspend || update_fw_flag) {
		LOGI("In suspend/Fw is updating, cannot enable/disable glove mode");
		if (update_fw_flag)
			need_check_touch_mode = true;
	} else {
		disable_irq(global_ts->client->irq);
		if (mode == 1) {
			glovemode_state = true;
			EnterGloveMode();
		} else if (mode == 0) {
			glovemode_state = false;
			ExitGloveMode();
		} else {
			LOGE("Buf value is failed");
		}
		enable_irq(global_ts->client->irq);
	}
	return count;
}

static ssize_t show_glove_mode(struct device *dev, struct device_attribute *attr, char *buf)
{
	LOGI("enter, glove mode state = %s", glovemode_state ? "true" : "false");
	return sprintf(buf, "%s\n", glovemode_state ? "true" : "false");
}
#endif

#ifdef ELAN_2WIREICE
static ssize_t show_2wire(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	struct i2c_client *client = to_i2c_client(dev);

	work_lock=1;
	disable_irq(global_ts->client->irq);
	wake_lock(&global_ts->wakelock);

	ret = TWO_WIRE_ICE(client);

	work_lock=0;
	enable_irq(global_ts->client->irq);
	wake_unlock(&global_ts->wakelock);

	return sprintf(buf, "Update Firmware by 2wire JTAG\n");
}
#endif

static ssize_t show_fw_version_value(struct device *dev, struct device_attribute *attr, char *buf)
{
	int len = 0;
	int rc = -1;

	LOGI("enter");
#ifdef ESD_CHECK
		live_state = 1;
#endif
	disable_irq(global_ts->client->irq);
	rc = __fw_packet_handler(global_ts->client);
	if (rc < 0) {
		LOGE("fw packet handler failed.");
		len += sprintf(buf + len, "0xFFFF-0xFFFF-0x%2.2X", TP_ID);
	} else {
		len += sprintf(buf + len, "0x%4.4X-0x%4.4X-0x%2.2X\n", FW_VERSION, FW_ID, TP_ID);
	}
	enable_irq(global_ts->client->irq);

	return len;
}

static ssize_t show_iap_mode(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", (RECOVERY == 0x80) ? "Recovery" : "Normal");
}

static int elan_ktf_smartwindow_switch(bool enable)
{
	uint8_t cmd_smartwindow_enable[] = {0x54, 0x25, 0x01, 0x01};
	uint8_t cmd_smartwindow_disable[] = {0x54, 0x25, 0x00, 0x01};
	int i;
	int finger_num = 10;

	mutex_lock(&global_ts->input_dev->mutex);
	if (enable) {
		if (KEY_MENU_STATE) {
			input_report_key(global_ts->input_dev, KEY_MENU, 0);
			KEY_MENU_STATE = false;
			LOGI("MENU KEY UP");
		}
		if (KEY_BACK_STATE) {
			input_report_key(global_ts->input_dev, KEY_BACK, 0);
			KEY_BACK_STATE = false;
			LOGI("BACK KEY UP");
		}
		if (KEY_HOME_STATE) {
			input_report_key(global_ts->input_dev, KEY_HOME, 0);
			KEY_HOME_STATE = false;
			LOGI("HOME KEY UP");
		}
		if (haspoint) {
			LOGI("All fingers up. NO PRESS.");
			for (i = 0; i < finger_num; i++) {
				if (point_flag[i] == 1) {
					point_flag[i] = 0;
				}
			}
			input_report_key(global_ts->input_dev, BTN_TOUCH, 0);
			haspoint = false;
		}
		input_mt_sync(global_ts->input_dev);
		input_sync(global_ts->input_dev);
		if ((i2c_master_send(global_ts->client, cmd_smartwindow_enable, sizeof(cmd_smartwindow_enable))) != sizeof(cmd_smartwindow_enable)) {
			LOGE("i2c master send failed");
			mutex_unlock(&global_ts->input_dev->mutex);
			return -EINVAL;
		} else {
			global_ts->smart_window = 1;
			LOGI("SmartWindow Enable.");
		}
	} else {
		if (!global_ts->elan_is_suspend) {
			if ((i2c_master_send(global_ts->client, cmd_smartwindow_disable, sizeof(cmd_smartwindow_disable))) != sizeof(cmd_smartwindow_disable)) {
				LOGE("i2c master send failed");
				mutex_unlock(&global_ts->input_dev->mutex);
				return -EINVAL;
			} else {
				global_ts->smart_window = 0;
				LOGI("SmartWindow Disable.");
			}
		} else {
			skip_smartwindow_mode_switch = true;
			LOGI("Skip smartWindow disable because of still in suspend");
		}
	}
	mutex_unlock(&global_ts->input_dev->mutex);

	return 0;
}

static void elan_ktf_smartwindow_switch_work(struct work_struct *work)
{
	if (!update_fw_flag) {
		if (global_ts->smart_window== 0) {
			elan_ktf_smartwindow_switch(false);
		} else if (global_ts->smart_window == 1) {
			elan_ktf_smartwindow_switch(true);
		} else {
			LOGE("set smart windows failed. value = %d", global_ts->smart_window);
		}
	} else {
		LOGI("Fw is updating, skip smart window switch");
		need_check_touch_mode = true;
	}
}

static int elan_ktf_smartwindow_query(void)
{
	uint8_t cmd_smartwindow_query[] = {0x53, 0x25, 0x00, 0x01};
	uint8_t resp_smartwindow_query[4] = {0};

#ifdef ESD_CHECK
	live_state = 1;
#endif
	disable_irq(global_ts->client->irq);
	if ((i2c_master_send(global_ts->client, cmd_smartwindow_query, sizeof(cmd_smartwindow_query))) != sizeof(cmd_smartwindow_query)) {
		LOGE("i2c master send failed");
		goto Err_SmartWindow;
	}

	elan_ktf_ts_poll(global_ts->client);

	if ((i2c_master_recv(global_ts->client, resp_smartwindow_query, sizeof(resp_smartwindow_query))) != sizeof(resp_smartwindow_query)) {
		LOGE("i2c master recv failed");
		goto Err_SmartWindow;
	}
	enable_irq(global_ts->client->irq);

	if (resp_smartwindow_query[2] == 0x01) {
		LOGI("SmartWindow ON.");
		return 1;
	} else if (resp_smartwindow_query[2] == 0x00) {
		LOGI("SmartWindow OFF.");
		return 0;
	} else {
		return -1;
	}

Err_SmartWindow:
	enable_irq(global_ts->client->irq);
	return -1;
}

static ssize_t show_smart_window(struct device *dev, struct device_attribute *attr, char *buf)
{
	int res = elan_ktf_smartwindow_query();
	if (res == 0)
		return sprintf(buf, "OFF\n");
	else if (res == 1)
		return sprintf(buf, "ON\n");
	else
		return sprintf(buf, "QueryFailed\n");

	return 0;
}

static ssize_t store_smart_window(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int set_smartwindow;
	int rc;

	if (size > 2)
		return -EINVAL;

	rc = kstrtoint(buf, 10, &set_smartwindow);
	if (rc != 0) {
		LOGE("failed");
		return rc;
	}

	if (update_fw_flag) {
		LOGI("Fw is updating, skip smart window switch");
		need_check_touch_mode = true;
	} else {
		if (set_smartwindow == 0) {
			global_ts->smart_window = 0;
			elan_ktf_smartwindow_switch(false);
		} else if (set_smartwindow == 1) {
			global_ts->smart_window = 1;
			elan_ktf_smartwindow_switch(true);
		} else {
			LOGE("set smart windows failed. value = %d", set_smartwindow);
		}
	}

	return size;
}

static int elan_ktf_powersupply_switch(int supplymode)
{
	uint8_t cmd_power_battery[] = {0x54, 0x24, 0x00, 0x01};
	uint8_t cmd_power_usb[] = {0x54, 0x24, 0x01, 0x01};
	uint8_t cmd_power_charger[] = {0x54, 0x24, 0x02, 0x01};

	if (!global_ts->elan_is_suspend) {
		mutex_lock(&global_ts->input_dev->mutex);
		switch (supplymode) {
			case POWER_BATTERY:
				if ((i2c_master_send(global_ts->client, cmd_power_battery, sizeof(cmd_power_battery))) != sizeof(cmd_power_battery)) {
					LOGE("i2c master send failed");
					mutex_unlock(&global_ts->input_dev->mutex);
					return -EINVAL;
				} else {
					LOGI("In Battery mode");
				}
				break;

			case POWER_USB:
				if ((i2c_master_send(global_ts->client, cmd_power_usb, sizeof(cmd_power_usb))) != sizeof(cmd_power_usb)) {
					LOGE("i2c master send failed");
					mutex_unlock(&global_ts->input_dev->mutex);
					return -EINVAL;
				} else {
					LOGI("In USB mode");
				}
				break;

			case POWER_CHARGER:
				if ((i2c_master_send(global_ts->client, cmd_power_charger, sizeof(cmd_power_charger))) != sizeof(cmd_power_charger)) {
					LOGE("i2c master send failed");
					mutex_unlock(&global_ts->input_dev->mutex);
					return -EINVAL;
				} else {
					LOGI("In Charger mode");
				}
				break;
			default:
				LOGI("supplymode not in case");
		}
		mutex_unlock(&global_ts->input_dev->mutex);
	} else {
		skip_powersupply_mode_switch = true;
		LOGI("Skip power supply mode switch because of still in suspend");
	}

	return 0;
}

static void elan_ktf_powersupply_switch_work(struct work_struct *work)
{
	if (!update_fw_flag) {
		elan_ktf_powersupply_switch(global_ts->power_supply);
	} else {
		need_check_touch_mode = true;
		LOGI("FW is updating, skip power supply switch");
	}
}

void elan_ktf_usb_detection(int supplymode)
{
	if (global_ts == NULL) {
		LOGE("global elan touch screen is null.");
		return;
	}

	if (global_ts->probe_success == 1) {
		//0:BATTERY, 1:USB, 2:ASUS CHARGER
		LOGI("USB power supply type detection = %d.", supplymode);
		global_ts->power_supply = supplymode;
		queue_work(global_ts->powersupply_work_queue, &global_ts->powersupply_work);
	} else {
		LOGE("elan touch screen probe failed.");
	}

}
EXPORT_SYMBOL(elan_ktf_usb_detection);

static int elan_ktf_powersupply_query(void)
{
	uint8_t cmd_powersupply_query[] = {0x53, 0x24, 0x00, 0x01};
	uint8_t resp_powersupply_query[4]  = {0};

#ifdef ESD_CHECK
	live_state = 1;
#endif
	disable_irq(global_ts->client->irq);
	if ((i2c_master_send(global_ts->client, cmd_powersupply_query, sizeof(cmd_powersupply_query))) != sizeof(cmd_powersupply_query)) {
		LOGE("i2c master send failed");
		goto Err_Power;
	}

	elan_ktf_ts_poll(global_ts->client);

	if ((i2c_master_recv(global_ts->client, resp_powersupply_query, sizeof(resp_powersupply_query))) != sizeof(resp_powersupply_query)) {
		LOGE("i2c master recv failed");
		goto Err_Power;
	}
	enable_irq(global_ts->client->irq);

	if (resp_powersupply_query[2] == 0x00) {
		LOGI("powersupply Battery.");
		return 0;
	} else if (resp_powersupply_query[2] == 0x01) {
		LOGI("powersupply USB.");
		return 1;
	} else if (resp_powersupply_query[2] == 0x02) {
		LOGI("powersupply Charge.");
		return 2;
	} else {
		return -1;
	}

Err_Power:
	enable_irq(global_ts->client->irq);
	return -1;
}

static ssize_t show_powersupply(struct device *dev, struct device_attribute *attr, char *buf)
{
	int res = elan_ktf_powersupply_query();
	if (res == 0)
		return sprintf(buf, "Battery\n");
	else if (res == 1)
		return sprintf(buf, "USB\n");
	else if (res == 2)
		return sprintf(buf, "AC\n");
	else
		return sprintf(buf, "QueryFailed\n");

	return 0;
}

static ssize_t store_powersupply(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int set_powersupply;
	int rc;

	if (size > 2)
		return -EINVAL;

	rc = kstrtoint(buf, 10, &set_powersupply);
	if (rc != 0) {
		LOGE("failed");
		return rc;
	}

	if (set_powersupply == 0) {
		elan_ktf_powersupply_switch(POWER_BATTERY);
	} else if (set_powersupply == 1) {
		elan_ktf_powersupply_switch(POWER_USB);
	} else if (set_powersupply == 2) {
		elan_ktf_powersupply_switch(POWER_CHARGER);
	} else {
		LOGE("set power mode failed. value = %d", set_powersupply);
	}

	return size;
}

static ssize_t show_psensor_enable_touch(struct device *dev, struct device_attribute *attr, char *buf)
{
	last_time_psensor = 0;
	return sprintf(buf, "1\n");
}

static ssize_t show_psensor_disable_touch(struct device *dev, struct device_attribute *attr, char *buf)
{
	last_time_psensor = jiffies;
	return sprintf(buf, "0\n");
}

static DEVICE_ATTR(gpio_int, S_IRUGO, show_gpio_int, NULL);
static DEVICE_ATTR(reset, S_IRUGO, show_reset, NULL);
static DEVICE_ATTR(enable_irq, S_IRUGO, show_enable_irq, NULL);
static DEVICE_ATTR(disable_irq, S_IRUGO, show_disable_irq, NULL);
static DEVICE_ATTR(calibrate, S_IRUGO, show_calibrate, NULL);
static DEVICE_ATTR(fw_version, S_IRUGO, show_fw_version_value, NULL);
static DEVICE_ATTR(checksum, S_IRUGO, show_checksum, NULL);
static DEVICE_ATTR(tpid, S_IRUGO, show_tpid, NULL);
#ifdef GLOVEMODE
static DEVICE_ATTR(glovemode, S_IRUGO|S_IWUSR, show_glove_mode, store_glove_mode);
#endif
static DEVICE_ATTR(fw_update, S_IRUGO|S_IWUSR, show_fw_update, store_fw_update);
#ifdef ELAN_2WIREICE
static DEVICE_ATTR(2wire, S_IRUGO, show_2wire, NULL);
#endif
static DEVICE_ATTR(iap_mode, S_IRUGO, show_iap_mode, NULL);
static DEVICE_ATTR(smartwindow, S_IRUGO|S_IWUSR, show_smart_window, store_smart_window);
static DEVICE_ATTR(powersupply, S_IRUGO|S_IWUSR, show_powersupply, store_powersupply);
static DEVICE_ATTR(psensor_enable_touch, S_IRUGO, show_psensor_enable_touch, NULL);
static DEVICE_ATTR(psensor_disable_touch, S_IRUGO, show_psensor_disable_touch, NULL);

static struct attribute *elan_attributes[] = {
	&dev_attr_gpio_int.attr,
	&dev_attr_reset.attr,
	&dev_attr_enable_irq.attr,
	&dev_attr_disable_irq.attr,
	&dev_attr_calibrate.attr,
	&dev_attr_fw_version.attr,
	&dev_attr_fw_update.attr,
#ifdef GLOVEMODE
	&dev_attr_glovemode.attr,
#endif
	&dev_attr_checksum.attr,
	&dev_attr_tpid.attr,
#ifdef ELAN_2WIREICE
	&dev_attr_2wire.attr,
#endif
	&dev_attr_psensor_enable_touch.attr,
	&dev_attr_psensor_disable_touch.attr,
	&dev_attr_iap_mode.attr,
	&dev_attr_smartwindow.attr,
	&dev_attr_powersupply.attr,
	NULL
};

static struct attribute_group elan_attribute_group = {
	.name = DEVICE_NAME,
	.attrs = elan_attributes,
};

static int __hello_packet_handler(struct i2c_client *client)
{
	int rc;
	uint8_t buf_recv[8] = {0};

	rc = elan_ktf_ts_poll(client);
	if (rc < 0)
		LOGE("INT poll is hight");

	rc = i2c_master_recv(client, buf_recv, 8);
	if (rc != 8) {
		LOGE("i2c_master_recv error");
		return -1;
	}
	LOGI("hello packet %2x:%2X:%2x:%2x:%2x:%2x:%2x:%2x",
		buf_recv[0], buf_recv[1], buf_recv[2], buf_recv[3] , buf_recv[4], buf_recv[5], buf_recv[6], buf_recv[7]);

	if (buf_recv[0] == 0x55 && buf_recv[1] == 0x55 && buf_recv[2] == 0x80 && buf_recv[3] == 0x80) {
		LOGI("In Recovery Mode");
		RECOVERY = 0x80;
		return RECOVERY;
	}

	/*Some Elan init don't need Re-Calibration */
	mdelay(300);
	rc = elan_ktf_ts_poll(client);
	if (rc < 0)
		LOGE("INT poll is hight");

	rc = i2c_master_recv(client, buf_recv, 8);
	LOGI("Try Re-Calibration packet %2x:%2X:%2x:%2x", buf_recv[0], buf_recv[1], buf_recv[2], buf_recv[3]);
	if (rc != 8) {
		LOGE("i2c_master_recv error");
		return -1;
	}

	return 0;
}

static int __fw_packet_handler(struct i2c_client *client)
{
	struct elan_ktf_ts_data *ts = i2c_get_clientdata(client);
	int rc;
	int major, minor;
	uint8_t cmd[] = {CMD_R_PKT, 0x00, 0x00, 0x01};/* Get Firmware Version*/
	uint8_t cmd_x[] = {0x53, 0x60, 0x00, 0x00}; /* Get x resolution */
	uint8_t cmd_y[] = {0x53, 0x63, 0x00, 0x00}; /* Get y resolution */
	uint8_t cmd_id[] = {0x53, 0xf0, 0x00, 0x01}; /* Get firmware ID */
	uint8_t cmd_bc[] = {CMD_R_PKT, 0x01, 0x00, 0x01};/* Get BootCode Version*/
	uint8_t buf_recv[4] = {0};

	/* Firmware version */
	rc = elan_ktf_ts_get_data(client, cmd, buf_recv, 4, 4);
	if (rc < 0)
		LOGE("Get Firmware version error");
	major = ((buf_recv[1] & 0x0f) << 4) | ((buf_recv[2] & 0xf0) >> 4);
	minor = ((buf_recv[2] & 0x0f) << 4) | ((buf_recv[3] & 0xf0) >> 4);
	ts->fw_ver = major << 8 | minor;
	FW_VERSION = ts->fw_ver;

	//Firmware ID
	rc = elan_ktf_ts_get_data(client, cmd_id, buf_recv, 4, 4);
	if (rc < 0)
		LOGE("Get Firmware ID error");
	major = ((buf_recv[1] & 0x0f) << 4) | ((buf_recv[2] & 0xf0) >> 4);
	minor = ((buf_recv[2] & 0x0f) << 4) | ((buf_recv[3] & 0xf0) >> 4);
	ts->fw_id = major << 8 | minor;
	FW_ID = ts->fw_id;

	// Bootcode version
	rc = elan_ktf_ts_get_data(client, cmd_bc, buf_recv, 4, 4);
	if (rc < 0)
		LOGE("Get Bootcode version error");
	major = ((buf_recv[1] & 0x0f) << 4) | ((buf_recv[2] & 0xf0) >> 4);
	minor = ((buf_recv[2] & 0x0f) << 4) | ((buf_recv[3] & 0xf0) >> 4);
	ts->bc_ver = major << 8 | minor;

	// X Resolution
	rc = elan_ktf_ts_get_data(client, cmd_x, buf_recv, 4, 4);
	if (rc < 0)
		LOGE("Get X Resolution error");
	ts->x_resolution = ((buf_recv[2])) | ((buf_recv[3] & 0xf0) << 4);
	X_RESOLUTION = ts->x_resolution;

	// Y Resolution
	rc = elan_ktf_ts_get_data(client, cmd_y, buf_recv, 4, 4);
	if (rc < 0)
		LOGE("Get Y Resolution error");
	ts->y_resolution = ((buf_recv[2])) | ((buf_recv[3] & 0xf0) << 4);
	Y_RESOLUTION = ts->y_resolution;

	LOGI("Firmware version: 0x%4.4x", ts->fw_ver);
	LOGI("Firmware ID: 0x%4.4x", ts->fw_id);
	LOGI("Bootcode Version: 0x%4.4x", ts->bc_ver);
	LOGI("x resolution: %d, y resolution: %d", X_RESOLUTION, Y_RESOLUTION);
	return 0;
}

int CalculateCheckSum(const u8 *fw_data, int PageNum) {
	uint32_t sumdata = 0;
	int j, i;
	int pt = 0;
	uint16_t byte1, byte2, combined;
	uint16_t precs = 0;

	for (i = 1; i < PageNum-1; i++) {
		for (j = 0; j < PageSize; j = j+2) {
			pt = i*PageSize + j;
			if (j > 1 && j < 130) {
				byte1 = fw_data[pt];
				byte2 = fw_data[pt+1];
				combined = (byte2 << 8 | byte1);
				if (i == 243 && j == 22) {
					precs = combined;
				} else {
						sumdata = sumdata + combined;
				}
			}
		}
	}
	LOGI("sumdata = %8.8X, precs = %4.4X", sumdata, precs);
	sumdata = sumdata & 0x0000FFFF;

	if (sumdata != precs) return -1;
	return 0;
}

int getCheckSUM(struct i2c_client *client)
{
	uint8_t sendCS[4] = {0x53, 0x70, 0x00, 0x01};
	uint8_t recvCS[4] = {0};
	uint8_t sendEP[4] = {0x53, 0xF4, 0x00, 0x01};
	uint8_t recvEP[4] = {0};
	char CheckSum[2] = {0};
	int rc;

	if (!update_fw_flag) {
#ifdef ESD_CHECK
		live_state = 1;
#endif
		disable_irq(client->irq);
	}

	rc = elan_ktf_ts_get_data(client, sendCS, recvCS, 4, 4);
	if (rc < 0) {
		LOGE("recv CS failed! %2.2X %2.2X %2.2X %2.2X", recvCS[0] , recvCS[1] , recvCS[2] , recvCS[3]);
		if (!update_fw_flag)
			enable_irq(client->irq);
		return -1;
	} else
		LOGI("recv CS success, %2.2X %2.2X %2.2X %2.2X", recvCS[0] , recvCS[1] , recvCS[2] , recvCS[3]);

	rc = elan_ktf_ts_get_data(client, sendEP, recvEP, 4, 4);
	if (rc < 0) {
		LOGE("recv EP failed! %2.2X %2.2X %2.2X %2.2X", recvEP[0] , recvCS[1] , recvEP[2] , recvEP[3]);
		if (!update_fw_flag)
			enable_irq(client->irq);
		return -1;
	} else
		LOGI("recv EP success, %2.2X %2.2X %2.2X %2.2X", recvEP[0] , recvEP[1] , recvEP[2] , recvEP[3]);

	if (!update_fw_flag)
		enable_irq(client->irq);

	CheckSum[0]= ((recvEP[1] & 0x0F) << 4) | ((recvEP[2] & 0xF0) >> 4);
	CheckSum[1]= ((recvEP[2] & 0x0F) << 4) | ((recvEP[3] & 0xF0) >> 4);

	LOGI("EP CheckSum = 0x%2.2X%2.2X, CheckSum = 0x%2.2X%2.2X", CheckSum[0], CheckSum[1], recvCS[2] , recvCS[3]);

	if (!(recvCS[2] == CheckSum[0] && recvCS[3] == CheckSum[1])) {
		LOGE("Compare Checksum Failed!");
		return -1;
	} else
		LOGI("Compare Checksum PASS");

	return 0;
}

static inline int elan_ktf_pen_parse_xy(uint8_t *data, uint16_t *x, uint16_t *y, uint16_t *p)
{
	*x = *y = *p = 0;

	*x = data[3];
	*x <<= 8;
	*x |= data[2];

	*y = data[5];
	*y <<= 8;
	*y |= data[4];

	*p = data[7];
	*p <<= 8;
	*p |= data[6];

	return 0;
}

static inline int elan_ktf_ts_parse_xy(uint8_t *data, uint16_t *x, uint16_t *y)
{
	*x = *y = 0;

	*x = (data[0] & 0xf0);
	*x <<= 4;
	*x |= data[1];

	*y = (data[0] & 0x0f);
	*y <<= 8;
	*y |= data[2];

	return 0;
}

static int elan_ktf_ts_setup(struct i2c_client *client)
{
	int rc;

	rc = __hello_packet_handler(client);
	if (rc != 0x80) {
		rc = __fw_packet_handler(client);
		if (rc < 0)
			LOGE("fw_packet_handler fail, rc = %d", rc);
		LOGI("checking firmware done.");
		//Check for FW_VERSION, if 0x0000 means FW update fail!
		if (FW_VERSION == 0x00) {
			rc = 0x80;
			LOGE("FW_VERSION = 0x%x, last FW update fail", FW_VERSION);
		}
	}

	return rc;
}

static int elan_ktf_ts_calibrate(struct i2c_client *client)
{

#ifdef ELAN_HID_I2C
	uint8_t flash_key[37] = {0x04, 0x00, 0x23, 0x00, 0x03, 0x00, 0x04,CMD_W_PKT, 0xc0, 0xe1, 0x5a};
	uint8_t cal_cmd[37] = {0x04, 0x00, 0x23, 0x00, 0x03, 0x00, 0x04,CMD_W_PKT, 0x29, 0x00, 0x01};

	LOGI("Flash Key cmd");
	if ((i2c_master_send(client, flash_key, sizeof(flash_key))) != sizeof(flash_key)) {
		LOGE("i2c_master_send failed");
		return -EINVAL;
	}
	LOGI("Calibration cmd: %02x, %02x, %02x, %02x\n", cal_cmd[7], cal_cmd[8], cal_cmd[9], cal_cmd[10]);
	if ((i2c_master_send(client, cal_cmd, sizeof(cal_cmd))) != sizeof(cal_cmd)) {
		LOGE("i2c_master_send failed");
		return -EINVAL;
	}

#else
	uint8_t cmd[] = {CMD_W_PKT, 0x29, 0x00, 0x01};

	LOGI("enter");
	LOGI("dump cmd: %02x, %02x, %02x, %02x", cmd[0], cmd[1], cmd[2], cmd[3]);

	if ((i2c_master_send(client, cmd, sizeof(cmd))) != sizeof(cmd)) {
		LOGE("i2c_master_send failed");
		return -EINVAL;
	}
#endif
	return 0;
}

#ifdef ELAN_POWER_SOURCE
void touch_callback(unsigned cable_status) {
	now_usb_cable_status = cable_status;
}
#endif

static int elan_ktf_ts_recv_data(struct i2c_client *client, uint8_t *buf, int bytes_to_recv)
{

	int rc;
	if (buf == NULL)
		return -EINVAL;

	memset(buf, 0, bytes_to_recv);

	/* The ELAN_PROTOCOL support normanl packet format */
#ifdef ELAN_PROTOCOL
	rc = i2c_master_recv(client, buf, bytes_to_recv);
	if (rc != bytes_to_recv) {
		LOGE("i2c_master_recv failed");
		return -1;
	}
#else
	rc = i2c_master_recv(client, buf, bytes_to_recv);
	if (rc != 8) {
		LOGE("Read the first package error");
		mdelay(30);
		return -1;
	}
	LOGI("%x %x %x %x %x %x %x %x", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
	mdelay(1);

	//for five finger
	if (buf[0] == 0x6D) {
		rc = i2c_master_recv(client, buf+ 8, 8);
		if (rc != 8) {
			LOGE("Read the second package error");
			mdelay(30);
			return -1;
		}
		LOGI("%x %x %x %x %x %x %x %x", buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
		rc = i2c_master_recv(client, buf+ 16, 2);
		if (rc != 2) {
			LOGE("Read the third package error");
			mdelay(30);
			return -1;
		}
		mdelay(1);
		LOGI("%x %x", buf[16], buf[17]);
	}
#endif
	return rc;
}

#ifdef PROTOCOL_B
static int mTouchStatus[FINGER_NUM] = {0}; /* finger_num=10 */
void force_release_pos(struct i2c_client *client)
{
	struct elan_ktf_ts_data *ts = i2c_get_clientdata(client);
	int i;
	for (i = 0; i < FINGER_NUM; i++) {
		if (mTouchStatus[i] == 0)
			continue;
		input_mt_slot(ts->input_dev, i);
		input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 0);
		mTouchStatus[i] = 0;
	}

	input_sync(ts->input_dev);
}

static inline int elan_ktf_hid_parse_xy(uint8_t *data, uint16_t *x, uint16_t *y)
{
	*x = *y = 0;

	*x = (data[6]);
	*x <<= 8;
	*x |= data[5];

	*y = (data[10]);
	*y <<= 8;
	*y |= data[9];

	return 0;
}

static void elan_ktf_ts_report_data(struct i2c_client *client, uint8_t *buf)
{
	struct elan_ktf_ts_data *ts = i2c_get_clientdata(client);
	struct input_dev *idev = ts->input_dev;
	uint16_t x = 0, y = 0, touch_size, pressure_size;
	uint16_t fbits = 0;
	uint8_t i, num;
	uint16_t active = 0;
	uint8_t idx, btn_idx;
	int finger_num;
	int finger_id;
	int pen_hover = 0;
	int pen_down = 0;
	uint16_t p = 0;
	static uint8_t size_index[10] = {35, 35, 36, 36, 37, 37, 38, 38, 39, 39};

	/* for 10 fingers */
	if (buf[0] == TEN_FINGERS_PKT) {
		finger_num = 10;
		num = buf[2] & 0x0f;
		fbits = buf[2] & 0x30;
		fbits = (fbits << 4) | buf[1];
		idx=3;
		btn_idx=33;
	} else if ((buf[0] == MTK_FINGERS_PKT) || (buf[0] == FIVE_FINGERS_PKT)) {
		/* for 5 fingers */
		finger_num = 5;
		num = buf[1] & 0x07;
		fbits = buf[1] >>3;
		idx = 2;
		btn_idx = 17;
	} else {
		/* for 2 fingers */
		finger_num = 2;
		num = buf[7] & 0x03;// for elan old 5A protocol the finger ID is 0x06
		fbits = buf[7] & 0x03;
		idx=1;
		btn_idx=7;
	}

	switch (buf[0]) {
		case MTK_FINGERS_PKT:
		case TWO_FINGERS_PKT:
		case FIVE_FINGERS_PKT:
		case TEN_FINGERS_PKT:
			for (i = 0; i < finger_num; i++) {
				active = fbits & 0x01;
				if (active || mTouchStatus[i]) {
					input_mt_slot(ts->input_dev, i);
					input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, active);
					if (active) {
						elan_ktf_ts_parse_xy(&buf[idx], &x, &y);
						touch_size = ((i & 0x01) ? buf[size_index[i]] : (buf[size_index[i]] >> 4)) & 0x0F;
						pressure_size = touch_size << 4; // shift left touch size value to 4 bits for max pressure value 255
						input_report_abs(idev, ABS_MT_TOUCH_MAJOR, 100);
						input_report_abs(idev, ABS_MT_PRESSURE, 100);
						input_report_abs(idev, ABS_MT_POSITION_X, x);
						input_report_abs(idev, ABS_MT_POSITION_Y, y);
						if(unlikely(gPrint_point))
							LOGI("finger id = %d, x = %d, y = %d, size = %d, press = %d", i, x, y, touch_size, pressure_size);
					}
				}
				mTouchStatus[i] = active;
				fbits = fbits >> 1;
				idx += 3;
			}
			if (num == 0) {
				input_report_key(idev, BTN_TOUCH, 0);
				force_release_pos(client);
			}
			input_sync(idev);
			break;
		case PEN_PKT:
			pen_hover = buf[1] & 0x1;
			pen_down = buf[1] & 0x03;
			input_mt_slot(ts->input_dev, 0);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, pen_hover);
			if (pen_hover) {
				elan_ktf_pen_parse_xy(&buf[0], &x, &y, &p);
				if (pen_down == 0x01) {
					/* report hover function */
					input_report_abs(idev, ABS_MT_PRESSURE, 0);
					input_report_abs(idev, ABS_MT_DISTANCE, 15);
					LOGI("[pen] Hover DISTANCE = 15");
				}
				else {
					input_report_abs(idev, ABS_MT_TOUCH_MAJOR, 20);
					input_report_abs(idev, ABS_MT_PRESSURE, p);
					LOGI("[pen] PEN PRESSURE = %d", p);
				}
				input_report_abs(idev, ABS_MT_POSITION_X, x);
				input_report_abs(idev, ABS_MT_POSITION_Y, y);
			}
			if (unlikely(gPrint_point)) {
				LOGI("[pen] %x %x %x %x %x %x %x %x", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
				LOGI("[pen] x = %d, y = %d, p = %d", x, y, p);
			}
			if (pen_down == 0) {
				input_report_key(idev, BTN_TOUCH, 0);
				force_release_pos(client);
			}
			input_sync(idev);
			break;
		case ELAN_HID_PKT:
			finger_num = buf[62];
			if (finger_num > 5)
				finger_num = 5; /* support 5 fingers */
			idx=3;
			num = 5;
			for (i = 0; i < finger_num; i++) {
				if ((buf[idx] & 0x03) == 0x00)
					active = 0; /* 0x03: finger down, 0x00 finger up */
				else
					active = 1;

				if ((buf[idx] & 0x03) == 0)
					num --;
				finger_id = (buf[idx] & 0xfc) >> 2;
				input_mt_slot(ts->input_dev, finger_id);
				input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, active);
				if (active) {
					elan_ktf_hid_parse_xy(&buf[idx], &x, &y);
					input_report_abs(idev, ABS_MT_TOUCH_MAJOR, 100);
					input_report_abs(idev, ABS_MT_PRESSURE, 100);
					input_report_abs(idev, ABS_MT_POSITION_X, x);
					input_report_abs(idev, ABS_MT_POSITION_Y, y);
					LOGI("[hid] i = %d, finger_id = %d, x = %d, y = %d, Finger NO = %d", i, finger_id, x, y, finger_num);
				}
				mTouchStatus[i] = active;
				idx += 11;
			}
			if (num == 0) {
				input_report_key(idev, BTN_TOUCH, 0); //for all finger up
				force_release_pos(client);
			}
			input_sync(idev);
			break ;
		case IamLive_PKT:
			if (debug_trace)
				LOGI("I am Live PKT %x %x %x %x", buf[0], buf[1], buf[2], buf[3]);
			break;
		case SmartWake_PKT:
			{
				LOGI("Smart Wakeup %x %x %x %x", buf[0], buf[1], buf[2], buf[3]);
				switch (buf[1]) {

					case GESTURE_E: //e
					break;

					case GESTURE_Z: //Z
					break;

					case GESTURE_V: //v
					break;

					case GESTURE_W: //w
					break;

					case GESTURE_C: //c
					break;

					case GESTURE_S: //s
					break;
				}
			}
			break;
		default:
			LOGE("unknown packet type: %2x %2x %2x %2x", buf[0], buf[1], buf[2], buf[3]);
			break;
	}
}
#endif

#ifdef PROTOCOL_A
static void elan_ktf_ts_report_data(struct i2c_client *client, uint8_t *buf)
{
	struct elan_ktf_ts_data *ts = i2c_get_clientdata(client);
	struct input_dev *idev = ts->input_dev;
	uint16_t x, y;
	uint16_t fbits = 0;
	uint8_t i, num, reported = 0;
	uint8_t idx, btn_idx;
	int finger_num;

	if (buf[0] != IamLive_PKT && last_time_psensor != 0) {
		if (time_after(jiffies, last_time_psensor+1*HZ)) {
			last_time_psensor = 0;
		} else {
			LOGI("Skip report because of p sensor is enabled");
			return;
		}
	}

	/* for 10 fingers */
	if (buf[0] == TEN_FINGERS_PKT) {
		finger_num = 10;
		num = buf[2] & 0x0f;
		fbits = buf[2] & 0x30;
		fbits = (fbits << 4) | buf[1];
		idx = 3;
		btn_idx = 33;
	} else if ((buf[0] == MTK_FINGERS_PKT) || (buf[0] == FIVE_FINGERS_PKT)) {
		/* for 5 fingers */
		finger_num = 5;
		num = buf[1] & 0x07;
		fbits = buf[1] >>3;
		idx = 2;
		btn_idx = 17;
	} else {
		/* for 2 fingers */
		finger_num = 2;
		num = buf[7] & 0x03; // for elan old 5A protocol the finger ID is 0x06
		fbits = buf[7] & 0x03;
		idx = 1;
		btn_idx = 7;
	}

	switch (buf[0]) {
		case MTK_FINGERS_PKT:
		case TWO_FINGERS_PKT:
		case FIVE_FINGERS_PKT:
		case TEN_FINGERS_PKT:
			if (num == 0) {
				input_report_key(idev, BTN_TOUCH, 0);
#ifdef ELAN_BUTTON
				if (buf[btn_idx] == 0x21) {
					if (!KEY_BACK_STATE)
						LOGI("BACK KEY DOWN");
					KEY_BACK_STATE = true;
					input_report_key(idev, KEY_BACK, 1);
				} else if (buf[btn_idx] == 0x41) {
					if (!KEY_HOME_STATE)
						LOGI("HOME KEY DOWN");
					KEY_HOME_STATE = true;
					input_report_key(idev, KEY_HOME, 1);
				} else if (buf[btn_idx] == 0x81) {
					if (!KEY_MENU_STATE)
						LOGI("MENU KEY DOWN");
					KEY_MENU_STATE = true;
					input_report_key(idev, KEY_MENU, 1); //recent key
				} else {
					if (KEY_BACK_STATE || KEY_HOME_STATE || KEY_MENU_STATE) {
						if (KEY_BACK_STATE) {
							input_report_key(idev, KEY_BACK, 0);
							KEY_BACK_STATE = false;
							LOGI("BACK KEY UP");
						}
						if (KEY_HOME_STATE) {
							input_report_key(idev, KEY_HOME, 0);
							KEY_HOME_STATE = false;
							LOGI("HOME KEY UP");
						}
						if (KEY_MENU_STATE) {
							input_report_key(idev, KEY_MENU, 0);
							KEY_MENU_STATE = false;
							LOGI("MENU KEY UP");
						}
					} else {
						if (!unlikely(gPrint_point) && haspoint) {
							for (i = 0; i < finger_num; i++) {
								if (point_flag[i] == 1) {
									point_flag[i] = 0;
									LOGI("P%d UP", i+1);
								}
							}
						}
						haspoint = false;
						LOGI("NO PRESS");
						input_mt_sync(idev);
					}
				}
#endif
			} else {
				input_report_key(idev, BTN_TOUCH, 1);
				for (i = 0; i < finger_num; i++) {
					if ((fbits & 0x01)) {
						elan_ktf_ts_parse_xy(&buf[idx], &x, &y);
						input_report_abs(idev, ABS_MT_TRACKING_ID, i);
						input_report_abs(idev, ABS_MT_TOUCH_MAJOR, 100);
						input_report_abs(idev, ABS_MT_PRESSURE, 80);
						input_report_abs(idev, ABS_MT_POSITION_X, x);
						input_report_abs(idev, ABS_MT_POSITION_Y, y);
						input_mt_sync(idev);
						reported++;
						if(unlikely(gPrint_point))
							LOGI("finger id = %d, x = %d, y = %d", i, x, y);
						else {
							if (point_flag[i] == 0) {
								point_flag[i] = 1;
								LOGI("P%d DOWN, x = %d, y = %d", i+1, x, y);
								haspoint = true;
							}
						}
					} else {
						if (!unlikely(gPrint_point) && point_flag[i] == 1) {
							point_flag[i] = 0;
							LOGI("P%d UP", i+1);
						}
					}
					fbits = fbits >> 1;
					idx += 3;
				}
			}
			if (reported)
				input_sync(idev);
			else {
				input_mt_sync(idev);
				input_sync(idev);
			}
			break;
		case IamLive_PKT:
			if (debug_trace)
				LOGI("I am Live PKT%2x %2x %2x %2x", buf[0], buf[1], buf[2], buf[3]);
			break;
#if defined(GESTUREMODE)
		case SmartWake_PKT:
			{
				switch (buf[1]) {
					case GESTURE_E: //e
						LOGI("Gesture E Wakeup");
						input_report_key(idev, KEY_E, 1);
						input_sync(idev);
						input_report_key(idev, KEY_E, 0);
						input_sync(idev);
						break;

					case GESTURE_Z: //Z
						LOGI("Gesture Z Wakeup");
						input_report_key(idev, KEY_Z, 1);
						input_sync(idev);
						input_report_key(idev, KEY_Z, 0);
						input_sync(idev);
						break;

					case GESTURE_V: //v
						LOGI("Gesture V Wakeup");
						input_report_key(idev, KEY_V, 1);
						input_sync(idev);
						input_report_key(idev, KEY_V, 0);
						input_sync(idev);
						break;

					case GESTURE_W: //w
						LOGI("Gesture W Wakeup");
						input_report_key(idev, KEY_W, 1);
						input_sync(idev);
						input_report_key(idev, KEY_W, 0);
						input_sync(idev);
						break;

					case GESTURE_C: //c
						LOGI("Gesture C Wakeup");
						input_report_key(idev, KEY_C, 1);
						input_sync(idev);
						input_report_key(idev, KEY_C, 0);
						input_sync(idev);
						break;

					case GESTURE_S: //s
						LOGI("Gesture S Wakeup");
						input_report_key(idev, KEY_S, 1);
						input_sync(idev);
						input_report_key(idev, KEY_S, 0);
						input_sync(idev);
						break;

					case GESTURE_DOUBLECLICK: //double tap
						LOGI("Gesture D-Click Wakeup");
						input_report_key(idev, KEY_POWER, 1);
						input_sync(idev);
						input_report_key(idev, KEY_POWER, 0);
						input_sync(idev);
						break;

					default:
						LOGE("unknown gesture type: %2.2X %2.2X %2.2X %2.2X", buf[0], buf[1], buf[2], buf[3]);
						break;
				}
			}
			break;
#endif
		default:
			if (buf[0] == CALIB_PKT && buf[1] == CALIB_PKT && buf[2] == CALIB_PKT && buf[3] == CALIB_PKT) {
				LOGI("Calibration packet, %2.2X %2.2X %2.2X %2.2X", buf[0], buf[1], buf[2], buf[3]);
			} else if (buf[0] == HELLO_PKT && buf[1] == HELLO_PKT && buf[2] == HELLO_PKT && buf[3] == HELLO_PKT) {
				LOGI("Hello packet, %2.2X %2.2X %2.2X %2.2X", buf[0], buf[1], buf[2], buf[3]);
			} else if (buf[0] == HELLO_PKT && buf[1] == HELLO_PKT && buf[2] == 0x80 && buf[3] == 0x80) {
				LOGI("Recovery packet, %2.2X %2.2X %2.2X %2.2X", buf[0], buf[1], buf[2], buf[3]);
				RECOVERY = 0x80;
			} else {
				LOGE("unknown packet type: %2.2X %2.2X %2.2X %2.2X", buf[0], buf[1], buf[2], buf[3]);
			}
			break;
	}
}
#endif

static irqreturn_t elan_ktf_ts_irq_handler(int irq, void *dev_id)
{
	int rc;
	struct elan_ktf_ts_data *ts = dev_id;
#ifdef ELAN_BUFFER_MODE
	uint8_t buf[4+PACKET_SIZE] = {0};
	uint8_t buf1[PACKET_SIZE] = {0};
#else
	uint8_t buf[PACKET_SIZE] = {0};
#endif
	/* [Arima_5816][bozhi_lin] fix touch gesture not work in suspend state 20151012 +++ */
#if defined(GESTUREMODE)
	struct device *dev = ts->input_dev->dev.parent;

	if (dev->power.is_suspended) {
		if ((ts->wakeup_dclick) || (ts->wakeup_gesture)) {
			LOGI("power is suspended, sleep 1 ms");
			msleep(1);
			if (dev->power.is_suspended) {
				LOGI("power is still suspended, return");
				return IRQ_HANDLED;
			} else {
				LOGI("power is not suspended, continue");
			}
		}
	}
#endif
	/* [Arima_5816][bozhi_lin] fix touch gesture not work in suspend state 20151012 --- */

	if (gpio_get_value(ts->irq_gpio)) {
		LOGE("Detected the jitter on INT pin");
		return IRQ_HANDLED;
	}

#ifdef ESD_CHECK
	live_state = 1;
#endif

#ifdef ELAN_BUFFER_MODE
	rc = elan_ktf_ts_recv_data(ts->client, buf, 4+PACKET_SIZE);
	if (rc < 0) {
		LOGE("Received the packet Error");
		return IRQ_HANDLED;
	}
#else
	rc = elan_ktf_ts_recv_data(ts->client, buf, PACKET_SIZE);
	if (rc < 0) {
		LOGE("Received the packet Error");
		return IRQ_HANDLED;
	}
#endif

#ifndef ELAN_BUFFER_MODE
	elan_ktf_ts_report_data(ts->client, buf);
#else
	elan_ktf_ts_report_data(ts->client, buf+4);

	// Second package
	if (((buf[0] == 0x63) || (buf[0] == 0x66)) && ((buf[1] == 2) || (buf[1] == 3))) {
		rc = elan_ktf_ts_recv_data(ts->client, buf1, PACKET_SIZE);
		if (rc < 0)
			return IRQ_HANDLED;
		elan_ktf_ts_report_data(ts->client, buf1);
		// Final package
		if (buf[1] == 3) {
			rc = elan_ktf_ts_recv_data(ts->client, buf1, PACKET_SIZE);
			if (rc < 0)
				return IRQ_HANDLED;
			elan_ktf_ts_report_data(ts->client, buf1);
		}
	}
#endif

	return IRQ_HANDLED;
}

static int elan_ktf_ts_register_interrupt(struct i2c_client *client)
{
	struct elan_ktf_ts_data *ts = i2c_get_clientdata(client);
	int err = 0;

	err = request_threaded_irq(client->irq, NULL, elan_ktf_ts_irq_handler, IRQF_TRIGGER_LOW | IRQF_ONESHOT, client->name, ts);
	if (err) {
		LOGE("request_irq %d failed", client->irq);
	}

	return err;
}

#ifdef _ENABLE_DBG_LEVEL
static int ektf_proc_read(char *buffer, char **buffer_location, off_t offset, int buffer_length, int *eof, void *data)
{
	int ret;

	LOGI("call proc_read");

	if (offset > 0)  /* we have finished to read, return 0 */
		ret  = 0;
	else
		ret = sprintf(buffer, "Debug Level: Release Date: %s\n","2011/10/05");

	return ret;
}


static int ektf_proc_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
	char procfs_buffer_size = 0;
	int i, ret = 0;
	unsigned char procfs_buf[PROC_FS_MAX_LEN+1] = {0};
	unsigned int command;

	procfs_buffer_size = count;
	if (procfs_buffer_size > PROC_FS_MAX_LEN )
		procfs_buffer_size = PROC_FS_MAX_LEN+1;

	if (copy_from_user(procfs_buf, buffer, procfs_buffer_size)) {
		LOGE("proc_write faied at copy_from_user");
		return -EFAULT;
	}

	command = 0;
	for (i = 0; i < procfs_buffer_size-1; i++) {
		if (procfs_buf[i] >= '0' && procfs_buf[i] <= '9')
			command |= (procfs_buf[i]-'0');
		else if (procfs_buf[i] >= 'A' && procfs_buf[i] <= 'F')
			command |= (procfs_buf[i]-'A'+10);
		else if (procfs_buf[i] >= 'a' && procfs_buf[i] <= 'f')
			command |= (procfs_buf[i]-'a'+10);

		if (i != procfs_buffer_size-2)
			command <<= 4;
	}

	command = command&0xFFFFFFFF;
	switch (command) {
		case 0xF1:
			gPrint_point = 1;
			break;
		case 0xF2:
			gPrint_point = 0;
			break;
		case 0xFF:
			ret = elan_ktf_ts_calibrate(global_ts->client);
			break;
	}
	LOGI("Run command: 0x%08X  result:%d", command, ret);

	return count;
}
#endif

#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	if (evdata && evdata->data && global_ts && global_ts->client) {
		if (event == FB_EVENT_BLANK) {
			blank = evdata->data;
			if (*blank == FB_BLANK_UNBLANK) {
				elan_ktf_ts_resume(global_ts->client);
			} else if (*blank == FB_BLANK_POWERDOWN) {
				elan_ktf_ts_suspend(global_ts->client, PMSG_SUSPEND);
			}
		}
	}

	return 0;
}
#endif

static int elan_ktf_ts_regulator_configure(struct elan_ktf_ts_data *elan_data, bool on)
{
	int retval;

	if (on == false)
		goto hw_shutdown;

	elan_data->vdd = regulator_get(&elan_data->client->dev,"vdd");
	if (IS_ERR(elan_data->vdd)) {
		LOGE("Failed to get vdd regulator");
		return PTR_ERR(elan_data->vdd);
	}

	if (regulator_count_voltages(elan_data->vdd) > 0) {
		retval = regulator_set_voltage(elan_data->vdd, ELAN_VTG_MIN_UV, ELAN_VTG_MAX_UV);
		if (retval) {
			LOGE("regulator set_vtg failed retval =%d", retval);
			goto err_set_vtg_vdd;
		}
	}

	if (elan_data->i2c_pull_up) {
		elan_data->vcc_i2c = regulator_get(&elan_data->client->dev, "vcc_i2c");
		if (IS_ERR(elan_data->vcc_i2c)) {
			LOGE("Failed to get i2c regulator");
			retval = PTR_ERR(elan_data->vcc_i2c);
			goto err_get_vtg_i2c;
		}

		if (regulator_count_voltages(elan_data->vcc_i2c) > 0) {
			retval = regulator_set_voltage(elan_data->vcc_i2c,ELAN_I2C_VTG_MIN_UV, ELAN_I2C_VTG_MAX_UV);
			if (retval) {
				LOGE("reg set i2c vtg failed retval =%d",retval);
				goto err_set_vtg_i2c;
			}
		}
	}
	return 0;

err_set_vtg_i2c:
	if (elan_data->i2c_pull_up)
		regulator_put(elan_data->vcc_i2c);
err_get_vtg_i2c:
	if (regulator_count_voltages(elan_data->vdd) > 0)
		regulator_set_voltage(elan_data->vdd, 0, ELAN_VTG_MAX_UV);
err_set_vtg_vdd:
	regulator_put(elan_data->vdd);
	return retval;

hw_shutdown:
	if (regulator_count_voltages(elan_data->vdd) > 0)
		regulator_set_voltage(elan_data->vdd, 0, ELAN_VTG_MAX_UV);
	regulator_put(elan_data->vdd);
	if (elan_data->i2c_pull_up) {
		if (regulator_count_voltages(elan_data->vcc_i2c) > 0)
			regulator_set_voltage(elan_data->vcc_i2c, 0, ELAN_I2C_VTG_MAX_UV);
		regulator_put(elan_data->vcc_i2c);
	}
	return 0;
};

static int reg_set_optimum_mode_check(struct regulator *reg, int load_uA)
{
	return (regulator_count_voltages(reg) > 0) ? regulator_set_optimum_mode(reg, load_uA) : 0;
}

static int elan_ktf_ts_power_on(struct elan_ktf_ts_data *elan_data, bool on)
{
	int retval;

	if (on == false)
		goto power_off;

	retval = reg_set_optimum_mode_check(elan_data->vdd, ELAN_ACTIVE_LOAD_UA);
	if (retval < 0) {
		LOGE("Regulator vdd set_opt failed rc=%d", retval);
		return retval;
	}

	retval = regulator_enable(elan_data->vdd);
	if (retval) {
		LOGE("Regulator vdd enable failed rc=%d", retval);
		goto error_reg_en_vdd;
	}

	if (elan_data->i2c_pull_up) {
		retval = reg_set_optimum_mode_check(elan_data->vcc_i2c, ELAN_I2C_LOAD_UA);
		if (retval < 0) {
			LOGE("Regulator vcc_i2c set_opt failed rc=%d", retval);
			goto error_reg_opt_i2c;
		}

		retval = regulator_enable(elan_data->vcc_i2c);
		if (retval) {
			LOGE("Regulator vcc_i2c enable failed rc=%d", retval);
			goto error_reg_en_vcc_i2c;
		}
	}
	return 0;

error_reg_en_vcc_i2c:
	if (elan_data->i2c_pull_up)
		reg_set_optimum_mode_check(elan_data->vdd, 0);
error_reg_opt_i2c:
	regulator_disable(elan_data->vdd);
error_reg_en_vdd:
	reg_set_optimum_mode_check(elan_data->vdd, 0);
	return retval;

power_off:
	reg_set_optimum_mode_check(elan_data->vdd, 0);
	regulator_disable(elan_data->vdd);
	if (elan_data->i2c_pull_up) {
		reg_set_optimum_mode_check(elan_data->vcc_i2c, 0);
		regulator_disable(elan_data->vcc_i2c);
	}
	return 0;
}

static int elan_ktf_ts_parse_dt(struct device *dev, struct elan_ktf_i2c_platform_data *elan_pdata)
{
	struct device_node *np = dev->of_node;

	elan_pdata->i2c_pull_up = of_property_read_bool(np,"elan,i2c-pull-up");

	/* reset, irq gpio info */
	elan_pdata->reset_gpio = of_get_named_gpio_flags(np,"elan,reset-gpio", 0, &elan_pdata->reset_flags);
	elan_pdata->irq_gpio = of_get_named_gpio_flags(np,"elan,irq-gpio", 0, &elan_pdata->irq_flags);
	elan_pdata->tpid_gpio = of_get_named_gpio_flags(np,"elan,tpid-gpio", 0, &elan_pdata->tpid_flags);
	LOGI("Reset gpio = %d, IRQ gpio = %d, TPID gpio = %d", elan_pdata->reset_gpio, elan_pdata->irq_gpio, elan_pdata->tpid_gpio);
	return 0;
}

/* [Arima_5816][bozhi_lin] enable elan touch gesture and double-click mode 20151006 +++ */
#if defined(GESTUREMODE)
static int elan_ktf_dclick_switch(bool enable)
{
	uint8_t cmd_dclick_disable[] = {CMD_W_PKT, 0x30, 0xF0, 0x01};
	uint8_t cmd_dclick_enable[] = {CMD_W_PKT, 0x30, 0xF1, 0x01};

	if (enable) {
		if ((i2c_master_send(global_ts->client, cmd_dclick_enable, sizeof(cmd_dclick_enable))) != sizeof(cmd_dclick_enable)) {
			LOGE("dclick enable: i2c_master_send failed");
			return -EINVAL;
		}
	} else {
		if ((i2c_master_send(global_ts->client, cmd_dclick_disable, sizeof(cmd_dclick_disable))) != sizeof(cmd_dclick_disable)) {
			LOGE("dclick disable: i2c_master_send failed");
			return -EINVAL;
		}
	}
	return 0;
}

static ssize_t elan_ktf_wakeup_dclick_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct elan_ktf_ts_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "D-Click %s\n", data->wakeup_dclick ? "Enabled" : "Disabled");
}

static ssize_t elan_ktf_wakeup_dclick_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct elan_ktf_ts_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int rc;

	LOGI("enter, buf = %s", buf);

	if (data->elan_is_suspend) {
		LOGI("in suspend state, can not change.");
		return -EINVAL;
	}

	if (size > 2)
		return -EINVAL;

	mutex_lock(&data->input_dev->mutex);
	rc = kstrtoul(buf, 10, &val);
	if (rc != 0) {
		data->wakeup_dclick = false;
		LOGE("string to unsigned long failed, Disable d-click");
		mutex_unlock(&data->input_dev->mutex);
		return rc;
	}
	if (val) {
		data->wakeup_dclick = true;
		LOGI("Set d-click enabled");
	} else {
		data->wakeup_dclick = false;
		LOGI("Set d-click disabled");
	}
	mutex_unlock(&data->input_dev->mutex);

	return size;
}

static int elan_ktf_gesture_mode_switch(int set_mode)
{
	uint8_t cmd_gesture_w_enable[]	= {CMD_W_PKT, 0x30, 0x51, 0x01};
	uint8_t cmd_gesture_w_disable[]	= {CMD_W_PKT, 0x30, 0x50, 0x01};
	uint8_t cmd_gesture_s_enable[]	= {CMD_W_PKT, 0x30, 0x81, 0x01};
	uint8_t cmd_gesture_s_disable[]	= {CMD_W_PKT, 0x30, 0x80, 0x01};
	uint8_t cmd_gesture_e_enable[]	= {CMD_W_PKT, 0x30, 0x01, 0x01};
	uint8_t cmd_gesture_e_disable[]	= {CMD_W_PKT, 0x30, 0x00, 0x01};
	uint8_t cmd_gesture_c_enable[]	= {CMD_W_PKT, 0x30, 0x71, 0x01};
	uint8_t cmd_gesture_c_disable[]	= {CMD_W_PKT, 0x30, 0x70, 0x01};
	uint8_t cmd_gesture_z_enable[]	= {CMD_W_PKT, 0x30, 0x11, 0x01};
	uint8_t cmd_gesture_z_disable[]	= {CMD_W_PKT, 0x30, 0x10, 0x01};
	uint8_t cmd_gesture_v_enable[]	= {CMD_W_PKT, 0x30, 0x31, 0x01};
	uint8_t cmd_gesture_v_disable[]	= {CMD_W_PKT, 0x30, 0x30, 0x01};

	if (set_mode & GESTURE_W_ENABLE) {
		if ((i2c_master_send(global_ts->client, cmd_gesture_w_enable, sizeof(cmd_gesture_w_enable))) != sizeof(cmd_gesture_w_enable)) {
			LOGE("gesture_w enable: i2c_master_send failed");
			return -EINVAL;
		}
	} else {
		if ((i2c_master_send(global_ts->client, cmd_gesture_w_disable, sizeof(cmd_gesture_w_disable))) != sizeof(cmd_gesture_w_disable)) {
			LOGE("gesture_w disable: i2c_master_send failed");
			return -EINVAL;
		}
	}

	if (set_mode & GESTURE_S_ENABLE) {
		if ((i2c_master_send(global_ts->client, cmd_gesture_s_enable, sizeof(cmd_gesture_s_enable))) != sizeof(cmd_gesture_s_enable)) {
			LOGE("gesture_s enable: i2c_master_send failed");
			return -EINVAL;
		}
	} else {
		if ((i2c_master_send(global_ts->client, cmd_gesture_s_disable, sizeof(cmd_gesture_s_disable))) != sizeof(cmd_gesture_s_disable)) {
			LOGE("gesture_s disable: i2c_master_send failed");
			return -EINVAL;
		}
	}

	if (set_mode & GESTURE_E_ENABLE) {
		if ((i2c_master_send(global_ts->client, cmd_gesture_e_enable, sizeof(cmd_gesture_e_enable))) != sizeof(cmd_gesture_e_enable)) {
			LOGE("gesture_e enable: i2c_master_send failed");
			return -EINVAL;
		}
	} else {
		if ((i2c_master_send(global_ts->client, cmd_gesture_e_disable, sizeof(cmd_gesture_e_disable))) != sizeof(cmd_gesture_e_disable)) {
			LOGE("gesture_e disable: i2c_master_send failed");
			return -EINVAL;
		}
	}

	if (set_mode & GESTURE_C_ENABLE) {
		if ((i2c_master_send(global_ts->client, cmd_gesture_c_enable, sizeof(cmd_gesture_c_enable))) != sizeof(cmd_gesture_c_enable)) {
			LOGE("gesture_c enable: i2c_master_send failed");
			return -EINVAL;
		}
	} else {
		if ((i2c_master_send(global_ts->client, cmd_gesture_c_disable, sizeof(cmd_gesture_c_disable))) != sizeof(cmd_gesture_c_disable)) {
			LOGE("gesture_c disable: i2c_master_send failed");
			return -EINVAL;
		}
	}

	if (set_mode & GESTURE_Z_ENABLE) {
		if ((i2c_master_send(global_ts->client, cmd_gesture_z_enable, sizeof(cmd_gesture_z_enable))) != sizeof(cmd_gesture_z_enable)) {
			LOGE("gesture_z enable: i2c_master_send failed");
			return -EINVAL;
		}
	} else {
		if ((i2c_master_send(global_ts->client, cmd_gesture_z_disable, sizeof(cmd_gesture_z_disable))) != sizeof(cmd_gesture_z_disable)) {
			LOGE("gesture_z disable: i2c_master_send failed");
			return -EINVAL;
		}
	}

	if (set_mode & GESTURE_V_ENABLE) {
		if ((i2c_master_send(global_ts->client, cmd_gesture_v_enable, sizeof(cmd_gesture_v_enable))) != sizeof(cmd_gesture_v_enable)) {
			LOGE("gesture_v enable: i2c_master_send failed");
			return -EINVAL;
		}
	} else {
		if ((i2c_master_send(global_ts->client, cmd_gesture_v_disable, sizeof(cmd_gesture_v_disable))) != sizeof(cmd_gesture_v_disable)) {
			LOGE("gesture_v disable: i2c_master_send failed");
			return -EINVAL;
		}
	}

	return 0;
}

static ssize_t elan_ktf_wakeup_gesture_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct elan_ktf_ts_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "Gesture %s, Gesture type 0x%x\n", data->wakeup_gesture ? "Enabled" : "Disabled", data->wakeup_gesture_type);
}

static ssize_t elan_ktf_wakeup_gesture_type_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct elan_ktf_ts_data *data = dev_get_drvdata(dev);
	int set_gesture_type;
	int rc;

	LOGI("enter, buf = %s", buf);

	if (data->elan_is_suspend) {
		LOGI("in suspend state, can not change.");
		return -EINVAL;
	}

	if (size > 8)
		return -EINVAL;

	mutex_lock(&data->input_dev->mutex);
	if (buf[0] == '1') {
		rc = kstrtoint(buf, 2, &set_gesture_type);
		if (rc != 0) {
			data->wakeup_gesture = false;
			data->wakeup_gesture_type = 0;
			LOGE("string to int failed, Disable gesture, type = 0x%x", data->wakeup_gesture_type);
			mutex_unlock(&data->input_dev->mutex);
			return rc;
		}
		data->wakeup_gesture = true;
		data->wakeup_gesture_type = set_gesture_type & GESTURE_MASK;
		LOGI("Set gesture enabled, type = 0x%x", data->wakeup_gesture_type);
	} else {
		data->wakeup_gesture = false;
		data->wakeup_gesture_type = 0;
		LOGI("Set gesture disabled, type = 0x%x", data->wakeup_gesture_type);
	}
	mutex_unlock(&data->input_dev->mutex);

	return size;
}
static DEVICE_ATTR(wakeup_dclick, 0664, elan_ktf_wakeup_dclick_show, elan_ktf_wakeup_dclick_store);
static DEVICE_ATTR(wakeup_gesture_type, 0664, elan_ktf_wakeup_gesture_type_show, elan_ktf_wakeup_gesture_type_store);
#endif
/* [Arima_5816][bozhi_lin] enable elan touch gesture and double-click mode 20151006 --- */

#ifdef ESD_CHECK
static void elan_ktf_ts_check_work_func(struct work_struct *work)
{
	int res = 0;

	if (update_fw_flag == 0) {
		if (live_state == 1) {
			live_state = 0;
			schedule_delayed_work(&global_ts->check_work, msecs_to_jiffies(2500));
			return;
		}

		LOGE("touch IC maybe crash");
		elan_ktf_ts_hw_reset();

		disable_irq(global_ts->client->irq);
		res = __hello_packet_handler(global_ts->client);
		if (res != 0) {
			LOGE("Receive hello package fail, res = %d", res);
		}
		schedule_delayed_work(&global_ts->check_work, msecs_to_jiffies(2500));
		enable_irq(global_ts->client->irq);
	} else if (update_fw_flag == 1) {
		schedule_delayed_work(&global_ts->check_work, msecs_to_jiffies(2500));
	}
}
#endif

static ssize_t elan_ktf_switch_name(struct switch_dev *sdev, char *buf)
{
	int rc = -1;
	int count = 0;

#ifdef ESD_CHECK
		live_state = 1;
#endif

	disable_irq(global_ts->client->irq);
	rc = __fw_packet_handler(global_ts->client);
	if (rc < 0) {
		LOGE("fw packet handler failed.");
		count += sprintf(buf + count, "VERSION:0xFFFF-ID:0xFFFF-TP:0x%2.2X", TP_ID);
	} else {
		count += sprintf(buf + count, "VERSION:0x%4.4X-ID:0x%4.4X-TP:0x%2.2X", FW_VERSION, FW_ID, TP_ID);
	}
	enable_irq(global_ts->client->irq);
	return count;
}

static int elan_ktf_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int err = 0;
	struct elan_ktf_i2c_platform_data *pdata;

	LOGI("start to probe.");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		LOGE("i2c check functionality error");
		err = -ENODEV;
		goto err_check_functionality_failed;
	}

	global_ts = kzalloc(sizeof(struct elan_ktf_ts_data), GFP_KERNEL);
	if (global_ts == NULL) {
		LOGE("allocate elan_ktf_ts_data failed");
		err = -ENOMEM;
		goto err_alloc_data_failed;
	}

	global_ts->client = client;
	i2c_set_clientdata(client, global_ts);
	mutex_init(&ktf_mutex);
	/* james: maybe remove */
	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata) {
			LOGE("Failed to allocate memory");
			return -ENOMEM;
		}

		err = elan_ktf_ts_parse_dt(&client->dev, pdata);
		if (err)
			return err;
	} else {
		pdata = client->dev.platform_data;
	}
	if (likely(pdata != NULL)) {
		global_ts->irq_gpio= pdata->irq_gpio;
		global_ts->reset_gpio = pdata->reset_gpio;
		global_ts->tpid_gpio = pdata->tpid_gpio;
		global_ts->i2c_pull_up = pdata->i2c_pull_up;
	}

	err = elan_ktf_ts_regulator_configure(global_ts, true);
	if (err < 0) {
		LOGE("Failed to configure regulators");
		goto err_reg_configure;
	}

	err = elan_ktf_ts_power_on(global_ts, true);
	if (err < 0) {
		LOGE("Failed to power on");
		goto err_power_device;
	}

	if (gpio_is_valid(global_ts->irq_gpio)) {
		/* configure touchscreen irq gpio */
		err = gpio_request(global_ts->irq_gpio, "elan_irq_gpio");
		if (err) {
			LOGE("unable to request gpio [%d]", global_ts->irq_gpio);
			goto err_irq_gpio_req;
		}
		err = gpio_direction_input(global_ts->irq_gpio);
		if (err) {
			LOGE("unable to set direction for gpio [%d]", global_ts->irq_gpio);
			goto err_irq_gpio_dir;
		}
		client->irq = gpio_to_irq(global_ts->irq_gpio);
	} else {
		LOGE("irq gpio not provided");
		goto err_irq_gpio_req;
	}

	if (gpio_is_valid(global_ts->reset_gpio)) {
		/* configure touchscreen reset out gpio */
		err = gpio_request(global_ts->reset_gpio, "elan_reset_gpio");
		if (err) {
			LOGE("unable to request gpio [%d]", global_ts->reset_gpio);
			goto err_reset_gpio_req;
		}
		err = gpio_direction_output(global_ts->reset_gpio, 1);
		if (err) {
			LOGE("unable to set direction for gpio [%d]", global_ts->reset_gpio);
			goto err_reset_gpio_dir;
		}
	} else {
		LOGE("reset gpio not provided");
		goto err_reset_gpio_req;
	}

	if (gpio_is_valid(global_ts->tpid_gpio)) {
		err = gpio_request(global_ts->tpid_gpio, "elan_tpid_gpio");
		if (err) {
			LOGE("unable to request gpio [%d]", global_ts->tpid_gpio);
			goto err_tpid_gpio_req;
		}
		TP_ID_GPIO_VALUE = gpio_get_value(global_ts->tpid_gpio);
		if (TP_ID_GPIO_VALUE == TRULY_TP_ID) {
			TP_ID = TRULY_TP_ID;
			TP_FW_ID = TRULY_FW_ID;
			LOGI("This is truly module");
		} else if (TP_ID_GPIO_VALUE == HOLITECH_TP_ID) {
			TP_ID = HOLITECH_TP_ID;
			TP_FW_ID = HOLITECH_FW_ID;
			LOGI("This is holitech module");
		} else {
			LOGE("Unknown tp module!");
			goto err_tpid_gpio_req;
		}
	} else {
		LOGE("tp id gpio not provided");
		goto err_tpid_gpio_req;
	}

	elan_ktf_ts_hw_reset();
	err = elan_ktf_ts_setup(client);
	if (err < 0) {
		LOGE("No Elan chip inside");
		goto err_ktf_ts_setup_failed;
	}

	wake_lock_init(&global_ts->wakelock, WAKE_LOCK_SUSPEND, "elan-touchscreen");
	global_ts->input_dev = input_allocate_device();
	if (global_ts->input_dev == NULL) {
		err = -ENOMEM;
		LOGE("Failed to allocate input device");
		goto err_input_dev_alloc_failed;
	}
	global_ts->input_dev->name = "elan-touchscreen";
	/* [Arima_5816][bozhi_lin] fix touch gesture not work in suspend state 20151012 +++ */
#if defined(GESTUREMODE)
	global_ts->input_dev->dev.parent = &client->dev;
#endif
	/* [Arima_5816][bozhi_lin] fix touch gesture not work in suspend state 20151012 --- */

#ifdef PROTOCOL_A
	set_bit(BTN_TOUCH, global_ts->input_dev->keybit);
#endif
#ifdef ELAN_BUTTON
	set_bit(KEY_BACK, global_ts->input_dev->keybit);
	set_bit(KEY_MENU, global_ts->input_dev->keybit); //recent key
	set_bit(KEY_HOME, global_ts->input_dev->keybit);
#endif
	/* [Arima_5816][bozhi_lin] enable elan touch gesture and double-click mode 20151006 +++ */
#if defined(GESTUREMODE)
	set_bit(KEY_C, global_ts->input_dev->keybit);
	set_bit(KEY_E, global_ts->input_dev->keybit);
	set_bit(KEY_S, global_ts->input_dev->keybit);
	set_bit(KEY_V, global_ts->input_dev->keybit);
	set_bit(KEY_W, global_ts->input_dev->keybit);
	set_bit(KEY_Z, global_ts->input_dev->keybit);
	set_bit(KEY_POWER, global_ts->input_dev->keybit);
#endif
	/* [Arima_5816][bozhi_lin] enable elan touch gesture and double-click mode 20151006 --- */

#ifdef PROTOCOL_B
	input_mt_init_slots(global_ts->input_dev, FINGER_NUM);
#endif

#ifdef PROTOCOL_A
	input_set_abs_params(global_ts->input_dev, ABS_X, 0,  X_RESOLUTION, 0, 0);
	input_set_abs_params(global_ts->input_dev, ABS_Y, 0,  Y_RESOLUTION, 0, 0);
	input_set_abs_params(global_ts->input_dev, ABS_TOOL_WIDTH, 0, MAX_FINGER_SIZE, 0, 0);
	input_set_abs_params(global_ts->input_dev, ABS_MT_TRACKING_ID, 0, FINGER_NUM, 0, 0);
#endif
	input_set_abs_params(global_ts->input_dev, ABS_MT_POSITION_Y, 0, Y_RESOLUTION, 0, 0);
	input_set_abs_params(global_ts->input_dev, ABS_MT_POSITION_X, 0, X_RESOLUTION, 0, 0);
	input_set_abs_params(global_ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, MAX_FINGER_SIZE, 0, 0);
	input_set_abs_params(global_ts->input_dev, ABS_PRESSURE, 0, MAX_FINGER_SIZE, 0, 0);
	input_set_abs_params(global_ts->input_dev, ABS_MT_PRESSURE, 0, MAX_FINGER_SIZE, 0, 0);
	input_set_abs_params(global_ts->input_dev, ABS_MT_DISTANCE, 0, MAX_FINGER_SIZE, 0, 0);

	__set_bit(EV_ABS, global_ts->input_dev->evbit);
	__set_bit(EV_SYN, global_ts->input_dev->evbit);
	__set_bit(EV_KEY, global_ts->input_dev->evbit);

	err = input_register_device(global_ts->input_dev);
	if (err) {
		LOGE("unable to register %s input device", global_ts->input_dev->name);
		goto err_input_register_device_failed;
	}

	if (elan_ktf_ts_register_interrupt(global_ts->client)) {
		LOGE("register INT failed.");
		goto err_input_register_device_failed;
	}

	LOGI("Start %s in interrupt mode",global_ts->input_dev->name);

	global_ts->firmware.minor = MISC_DYNAMIC_MINOR;
	global_ts->firmware.name = "elan-iap";
	global_ts->firmware.fops = &elan_touch_fops;
	global_ts->firmware.mode = S_IFREG|S_IRWXUGO;

	if (misc_register(&global_ts->firmware) < 0)
		LOGI("misc_register failed!");

	/* register sysfs */
	if (sysfs_create_group(&client->dev.kobj, &elan_attribute_group)) {
		LOGE("sysfs create group error");
		goto err_input_register_device_failed;
	}

	global_ts->powersupply_work_queue = create_singlethread_workqueue("elan_powersupply_wq");
	if (!global_ts->powersupply_work_queue) {
		LOGE("create powersupply workqueue failed");
		goto err_create_wq_failed;
	}
	INIT_WORK(&global_ts->powersupply_work, elan_ktf_powersupply_switch_work);

	global_ts->smartwindow_work_queue = create_singlethread_workqueue("elan_smartwindow_wq");
	if (!global_ts->smartwindow_work_queue) {
		LOGE("create smartwindow workqueue failed");
		goto err_create_wq_failed;
	}
	INIT_WORK(&global_ts->smartwindow_work, elan_ktf_smartwindow_switch_work);

#ifdef ESD_CHECK
	INIT_DELAYED_WORK(&global_ts->check_work, elan_ktf_ts_check_work_func);
	schedule_delayed_work(&global_ts->check_work, msecs_to_jiffies(2500));
#endif

#ifdef _ENABLE_DBG_LEVEL
	dbgProcFile = create_proc_entry(PROC_FS_NAME, 0600, NULL);
	if (dbgProcFile == NULL) {
		remove_proc_entry(PROC_FS_NAME, NULL);
		LOGE("Could not initialize /proc/%s", PROC_FS_NAME);
	} else {
		dbgProcFile->read_proc = ektf_proc_read;
		dbgProcFile->write_proc = ektf_proc_write;
		LOGI("/proc/%s created", PROC_FS_NAME);
	}
#endif

#if defined(CONFIG_FB)
	global_ts->fb_notif.notifier_call = fb_notifier_callback;
	err = fb_register_client(&global_ts->fb_notif);
	if (err)
		LOGE("Unable to register fb_notifier: %d", err);
#endif

	/* Register Switch file */
	global_ts->touch_sdev.name = "touch";
	global_ts->touch_sdev.print_name = elan_ktf_switch_name;
	if(switch_dev_register(&global_ts->touch_sdev) < 0) {
		LOGE("switch_dev_register for touch failed!");
	}
	switch_set_state(&global_ts->touch_sdev, 0);

	/* [Arima_5816][bozhi_lin] enable elan touch gesture and double-click mode 20151006 +++ */
#if defined(GESTUREMODE)
	err = device_create_file(&client->dev, &dev_attr_wakeup_dclick);
	if (err) {
		LOGE("sys file creation failed");
		goto err_input_register_device_failed;
	}

	err = device_create_file(&client->dev, &dev_attr_wakeup_gesture_type);
	if (err) {
		LOGE("sys file creation failed");
		goto free_wakeup_dclick_sys;
	}
#endif
	/* [Arima_5816][bozhi_lin] enable elan touch gesture and double-click mode 20151006 --- */

	/* [Arima_5816][bozhi_lin] fix touch gesture not work in suspend state 20151012 +++ */
#if defined(GESTUREMODE)
	device_init_wakeup(&client->dev, 1);
#endif
	/* [Arima_5816][bozhi_lin] fix touch gesture not work in suspend state 20151012 --- */
	global_ts->probe_success = 1;

	LOGI("probe completed.");
	return 0;

	/* [Arima_5816][bozhi_lin] enable elan touch gesture and double-click mode 20151006 +++ */
#if defined(GESTUREMODE)
free_wakeup_dclick_sys:
	device_remove_file(&client->dev, &dev_attr_wakeup_dclick);
#endif
	/* [Arima_5816][bozhi_lin] enable elan touch gesture and double-click mode 20151006 --- */
err_create_wq_failed:
	if (global_ts->powersupply_work_queue) {
		destroy_workqueue(global_ts->powersupply_work_queue);
	}
	if (global_ts->smartwindow_work_queue) {
		destroy_workqueue(global_ts->smartwindow_work_queue);
	}
err_input_register_device_failed:
#if defined(CONFIG_FB)
	if (fb_unregister_client(&global_ts->fb_notif))
		LOGE("Error occurred while unregistering fb_notifier.");
#endif /* CONFIG_FB */
	if (global_ts->input_dev)
		input_free_device(global_ts->input_dev);
#ifdef _ENABLE_DBG_LEVEL
	remove_proc_entry(PROC_FS_NAME, NULL);
#endif
err_ktf_ts_setup_failed:
err_input_dev_alloc_failed:
err_reset_gpio_dir:
	if (gpio_is_valid(global_ts->reset_gpio))
		gpio_free(global_ts->reset_gpio);
err_irq_gpio_dir:
	if (gpio_is_valid(global_ts->irq_gpio))
		gpio_free(global_ts->irq_gpio);
err_reset_gpio_req:
err_irq_gpio_req:
err_tpid_gpio_req:
	elan_ktf_ts_power_on(global_ts, false);
err_power_device:
	elan_ktf_ts_regulator_configure(global_ts, false);
err_reg_configure:
	input_free_device(global_ts->input_dev);
	global_ts->input_dev = NULL;
	kfree(global_ts);

err_alloc_data_failed:
err_check_functionality_failed:

	return err;
}

static int elan_ktf_ts_remove(struct i2c_client *client)
{
	struct elan_ktf_ts_data *ts = i2c_get_clientdata(client);

#if defined(CONFIG_FB)
	if (fb_unregister_client(&ts->fb_notif))
		LOGE("Error occurred while unregistering fb_notifier.");
#endif /* CONFIG_FB */

	free_irq(client->irq, ts);
	input_unregister_device(ts->input_dev);

	if (gpio_is_valid(ts->reset_gpio))
		gpio_free(ts->reset_gpio);
	if (gpio_is_valid(ts->irq_gpio))
		gpio_free(ts->irq_gpio);

	elan_ktf_ts_power_on(ts, false);
	elan_ktf_ts_regulator_configure(ts, false);

	kfree(ts);

	/* [Arima_5816][bozhi_lin] enable elan touch gesture and double-click mode 20151006 +++ */
#if defined(GESTUREMODE)
	device_remove_file(&client->dev, &dev_attr_wakeup_dclick);
	device_remove_file(&client->dev, &dev_attr_wakeup_gesture_type);
#endif
	/* [Arima_5816][bozhi_lin] enable elan touch gesture and double-click mode 20151006 --- */

	return 0;
}

static int elan_ktf_ts_set_power_state(struct i2c_client *client, int state)
{
	uint8_t cmd[] = {CMD_W_PKT, 0x50, 0x00, 0x01};

	cmd[1] |= (state << 3);

	if ((i2c_master_send(client, cmd, sizeof(cmd))) != sizeof(cmd)) {
		LOGE("i2c_master_send failed");
		return -EINVAL;
	}
	return 0;
}

static int elan_ktf_ts_get_power_state(struct i2c_client *client)
{
	int rc = 0;
	uint8_t cmd[] = {CMD_R_PKT, 0x50, 0x00, 0x01};
	uint8_t buf[4], power_state;

	rc = elan_ktf_ts_get_data(client, cmd, buf, 4, 4);
	if (rc < 0) {
		LOGE("elan_ktf_ts_get_data failed");
		return rc;
	}

	power_state = buf[1];
	if (power_state == 0x58) {
		LOGI("power state = Normal/Idle");
		return 1;
	} else {
		LOGI("power state = Deep Sleep, %0X", power_state);
		return 0;
	}
}

static void elan_ktf_check_touch_mode(void) {
	int rc = 0, len = 0;
#if defined(GESTUREMODE)
	uint8_t gesture_mod_cmd[] = {CMD_W_PKT, 0x40, 0x01, 0x01};
#endif
	if (global_ts->probe_success == 1) {
		if (need_check_touch_mode) {
			LOGI("Start check touch mode");
			/* A: check glove mode */
#ifdef ESD_CHECK
			live_state = 1;
#endif
			disable_irq(global_ts->client->irq);
			if (glovemode_state == 1)
				EnterGloveMode();
			else if (glovemode_state == 0)
				ExitGloveMode();
			else
				LOGE("Error glovemode state = %d", glovemode_state);
			enable_irq(global_ts->client->irq);

			/* B: check flip cover mode */
			if (global_ts->smart_window == 1)
				elan_ktf_smartwindow_switch(true);
			else if (global_ts->smart_window == 0)
				elan_ktf_smartwindow_switch(false);
			else
				LOGE("Error smart window state = %d", global_ts->smart_window);

			/* C: check power supply mode */
			elan_ktf_powersupply_switch(global_ts->power_supply);

			/* D: check suspend gesture and dclick mode*/
			if (global_ts->elan_is_suspend == 1) {
#ifdef ESD_CHECK
				cancel_delayed_work_sync(&global_ts->check_work);
#endif
#if defined(GESTUREMODE)
				LOGI("d-click = %d, gesture type = 0x%x", global_ts->wakeup_dclick, global_ts->wakeup_gesture_type);
				if ((global_ts->wakeup_dclick) || (global_ts->wakeup_gesture)) {
					rc = elan_ktf_dclick_switch(global_ts->wakeup_dclick);
					if (rc != 0)
						LOGE("dclick enable/disable failed, wakeup dclick = %d", global_ts->wakeup_dclick);

					rc = elan_ktf_gesture_mode_switch(global_ts->wakeup_gesture_type);
					if (rc != 0)
						LOGE("gesture enable/disable failed, wakeup gesture = 0x%x", global_ts->wakeup_gesture_type);

					len = i2c_master_send(global_ts->client, gesture_mod_cmd, sizeof(gesture_mod_cmd));
					if (len != sizeof(gesture_mod_cmd)) {
						LOGE("enter gesture mode: i2c_master_send fail, len = %d", len);
						need_check_touch_mode = false;
						return;
					}
					if (device_may_wakeup(&global_ts->client->dev))
						enable_irq_wake(global_ts->client->irq);
				} else {
					disable_irq(global_ts->client->irq);
					rc = elan_ktf_ts_set_power_state(global_ts->client, PWR_STATE_DEEP_SLEEP);
				}
#else
				disable_irq(global_ts->client->irq);
				rc = elan_ktf_ts_set_power_state(global_ts->client, PWR_STATE_DEEP_SLEEP);
#endif
			}
			need_check_touch_mode = false;
		}
	} else {
		LOGE("elan touch screen probe failed.");
	}
}

static int elan_ktf_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct elan_ktf_ts_data *ts = i2c_get_clientdata(client);
	/* [Arima_5816][bozhi_lin] enable elan touch gesture and double-click mode 20151006 +++ */
#if defined(GESTUREMODE)
	int len = 0;
	uint8_t gesture_mod_cmd[] = {CMD_W_PKT, 0x40, 0x01, 0x01};
#endif
	int rc = 0;
	/* [Arima_5816][bozhi_lin] enable elan touch gesture and double-click mode 20151006 --- */

#if defined(CONFIG_FB)
	if (ts->elan_is_suspend) {
		LOGI("Already in suspend state.");
		return 0;
	}
#endif
	/* The power_lock can be removed when firmware upgrade procedure will not be enter into suspend mode. */
	if (power_lock == 0) {

#ifdef ESD_CHECK
		cancel_delayed_work_sync(&ts->check_work);
#endif
		/* [Arima_5816][bozhi_lin] enable elan touch gesture and double-click mode 20151006 +++ */
#if defined(GESTUREMODE)
		LOGI("d-click = %d, gesture type = 0x%x", ts->wakeup_dclick, ts->wakeup_gesture_type);
		if ((ts->wakeup_dclick) || (ts->wakeup_gesture)) {
			rc = elan_ktf_dclick_switch(ts->wakeup_dclick);
			if (rc != 0)
				LOGE("dclick enable/disable failed, wakeup dclick = %d", ts->wakeup_dclick);

			rc = elan_ktf_gesture_mode_switch(ts->wakeup_gesture_type);
			if (rc != 0)
				LOGE("gesture enable/disable failed, wakeup gesture = 0x%x", ts->wakeup_gesture_type);

			len = i2c_master_send(client, gesture_mod_cmd, sizeof(gesture_mod_cmd));
			if (len != sizeof(gesture_mod_cmd)) {
				LOGE("enter gesture mode: i2c_master_send fail, len = %d", len);
				return 0;
			}
			/* [Arima_5816][bozhi_lin] fix touch gesture not work in suspend state 20151012 +++ */
			if (device_may_wakeup(&client->dev))
				enable_irq_wake(client->irq);
			/* [Arima_5816][bozhi_lin] fix touch gesture not work in suspend state 20151012 --- */
		} else {
			disable_irq(client->irq);
			rc = elan_ktf_ts_set_power_state(client, PWR_STATE_DEEP_SLEEP);
		}
#else
		disable_irq(client->irq);
		rc = elan_ktf_ts_set_power_state(client, PWR_STATE_DEEP_SLEEP);
#endif
		/* [Arima_5816][bozhi_lin] enable elan touch gesture and double-click mode 20151006 --- */
	} else {
		LOGI("Fw is updateing, skip suspend flow");
		need_check_touch_mode = true;
	}
#if defined(CONFIG_FB)
	ts->elan_is_suspend = 1;
#endif
	return 0;
}

static int elan_ktf_ts_resume(struct i2c_client *client)
{
	struct elan_ktf_ts_data *ts = i2c_get_clientdata(client);
	int i;
	int finger_num = 10;
	int HW_reset = 0;
#if defined(GESTUREMODE)
	int len = 0;
	uint8_t gesture_cmd_disable[] = {CMD_W_PKT, 0x40, 0x00, 0x01};
	uint8_t calibration_cmd[] = {CMD_W_PKT, 0x29, 0x01, 0x01};
#endif

#if defined(CONFIG_FB)
	if (!ts->elan_is_suspend) {
		LOGI("Already in awake state.");
		return 0;
	}
#endif
	/* The power_lock can be removed when firmware upgrade procedure will not be enter into suspend mode. */
	if (power_lock == 0) {
		if (haspoint) {
			LOGI("All fingers up. NO PRESS.");
			for (i = 0; i < finger_num; i++) {
				if (point_flag[i] == 1) {
					point_flag[i] = 0;
				}
			}
			input_report_key(ts->input_dev, BTN_TOUCH, 0);
			input_mt_sync(ts->input_dev);
			input_sync(ts->input_dev);
			haspoint = false;
		}

#if defined(GESTUREMODE)
		if ((ts->wakeup_dclick) || (ts->wakeup_gesture)) {
			len = i2c_master_send(client, gesture_cmd_disable, sizeof(gesture_cmd_disable));
			if(len != sizeof(gesture_cmd_disable))
				LOGE("Exit gesture mode fail, len = %d", len);
			len = i2c_master_send(client, calibration_cmd, sizeof(calibration_cmd));
			if(len != sizeof(calibration_cmd))
				LOGE("Calibration cmd fail, len = %d", len);
			disable_irq(client->irq);
		} else {
			len = elan_ktf_ts_set_power_state(client, PWR_STATE_NORMAL);
		}
		len = elan_ktf_ts_get_power_state(client);
		if (len != PWR_STATE_NORMAL) {
			HW_reset = 1;
			LOGE("Wake up failed");
			elan_ktf_ts_hw_reset();
			mdelay(100);
			len = __hello_packet_handler(global_ts->client);
			if (len != 0) {
					LOGE("Receive hello package fail, len = %d", len);
			}
#if defined(GLOVEMODE)
			if (glovemode_state)
				EnterGloveMode();
#endif
		}
#else
		len = elan_ktf_ts_set_power_state(client, PWR_STATE_NORMAL);
		len = elan_ktf_ts_get_power_state(client);
		if (len != PWR_STATE_NORMAL) {
			HW_reset = 1;
			LOGE("Wake up failed");
			elan_ktf_ts_hw_reset();
			mdelay(100);
			len = __hello_packet_handler(global_ts->client);
			if (len != 0) {
					LOGE("Receive hello package fail, res = %d", res);
			}
#if defined(GLOVEMODE)
			if (glovemode_state)
				EnterGloveMode();
#endif
		}
#endif

#if defined(GESTUREMODE)
		if ((ts->wakeup_dclick) || (ts->wakeup_gesture)) {
			if (device_may_wakeup(&client->dev))
				disable_irq_wake(client->irq);
			enable_irq(client->irq);
		} else {
			enable_irq(client->irq);
		}
#else
		enable_irq(client->irq);
#endif

#ifdef ESD_CHECK
		schedule_delayed_work(&global_ts->check_work, msecs_to_jiffies(2500));
#endif
	}

#if defined(CONFIG_FB)
	ts->elan_is_suspend = 0;
#endif
	if (HW_reset == 1 || skip_powersupply_mode_switch) {
		skip_powersupply_mode_switch = false;
		queue_work(global_ts->powersupply_work_queue, &global_ts->powersupply_work);
	}
	if (HW_reset == 1 || skip_smartwindow_mode_switch) {
		skip_smartwindow_mode_switch = false;
		queue_work(global_ts->smartwindow_work_queue, &global_ts->smartwindow_work);
	}
	return 0;
}

#if defined(CONFIG_FB)
static const struct dev_pm_ops elan_ktf_ts_dev_pm_ops = {
};
#else
static const struct dev_pm_ops elan_ktf_ts_dev_pm_ops = {
	.suspend = elan_ktf_ts_suspend,
	.resume = elan_ktf_ts_resume,
};
#endif

static const struct i2c_device_id elan_ktf_ts_id[] = {
	{ELAN_KTF_NAME, 0},
	{}
};

#ifdef CONFIG_OF
static struct of_device_id elan_ktf_ts_match_table[] = {
	{.compatible = "elan,ktf2k_ts",},
	{},
};
#else
#define elan_ktf_ts_match_table NULL
#endif

static struct i2c_driver ektf_ts_driver = {
	.probe		= elan_ktf_ts_probe,
	.remove		= elan_ktf_ts_remove,
	.id_table	= elan_ktf_ts_id,
	.driver		= {
		.name = ELAN_KTF_NAME,
		.of_match_table = elan_ktf_ts_match_table,
		.owner = THIS_MODULE,
#if defined(CONFIG_PM)
		.pm = &elan_ktf_ts_dev_pm_ops,
#endif
	},
};

static int __devinit elan_ktf_ts_init(void)
{
	return i2c_add_driver(&ektf_ts_driver);
}

static void __exit elan_ktf_ts_exit(void)
{
	i2c_del_driver(&ektf_ts_driver);
}

module_init(elan_ktf_ts_init);
module_exit(elan_ktf_ts_exit);

MODULE_DESCRIPTION("ELAN KTF2K Touchscreen Driver");
MODULE_LICENSE("GPL");
