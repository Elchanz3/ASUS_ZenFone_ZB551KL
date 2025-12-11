/*
 * Simple synchronous userspace interface to SPI devices
 *
 * Copyright (C) 2006 SWAPP
 *	Andrea Paterniani <a.paterniani@swapp-eng.it>
 * Copyright (C) 2007 David Brownell (simplification, cleanup)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <linux/regulator/consumer.h>


//Add by Fred NVram Backup chip id 20150716+++
#include <linux/types.h>
#include <linux/kobject.h>
//Add by Fred NVram Backup chip id 20150716---
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>

#include <asm/uaccess.h>
#define SPIDEV_LOG(fmt, args...) {}
//#define SPIDEV_LOG(fmt, args...) printk("[SPI-UT]: [%s]:[%d]" fmt "\n", __func__, __LINE__, ##args)
#define SPIDEV_ERR(fmt, args...) printk("[SPI-UT]: [%s]:[%d]" fmt "\n", __func__, __LINE__, ##args)

//#define TRACE_MSG_BUFF
/*
 * This supports access to SPI devices using normal userspace I/O calls.
 * Note that while traditional UNIX/POSIX I/O semantics are half duplex,
 * and often mask message boundaries, full SPI support requires full duplex
 * transfers.  There are several kinds of internal message boundaries to
 * handle chipselect management and other protocol options.
 *
 * SPI has a character major number assigned.  We allocate minor numbers
 * dynamically using a bitmask.  You must use hotplug tools, such as udev
 * (or mdev with busybox) to create and destroy the /dev/spidevB.C device
 * nodes, since there is no fixed association of minor numbers with any
 * particular SPI bus or device.
 */
#define SPIDEV_MAJOR			155	/* assigned */
#define N_SPI_MINORS			32	/* ... up to 256 */

static DECLARE_BITMAP(minors, N_SPI_MINORS);


//Add by Fred NVram Backup chip id 20150716+++
static int sysfs_status=0;
static unsigned char Chip_Id_BackUp=0;
struct kobject *soc_kobj = NULL;
//Add by Fred NVram Backup chip id 20150716---
/* Bit masks for spi_device.mode management.  Note that incorrect
 * settings for some settings can cause *lots* of trouble for other
 * devices on a shared bus:
 *
 *  - CS_HIGH ... this device will be active when it shouldn't be
 *  - 3WIRE ... when active, it won't behave as it should
 *  - NO_CS ... there will be no explicit message boundaries; this
 *	is completely incompatible with the shared bus model
 *  - READY ... transfers may proceed when they shouldn't.
 *
 * REVISIT should changing those flags be privileged?
 */
#define SPI_MODE_MASK		(SPI_CPHA | SPI_CPOL | SPI_CS_HIGH \
				| SPI_LSB_FIRST | SPI_3WIRE | SPI_LOOP \
				| SPI_NO_CS | SPI_READY)

struct spidev_data {
	dev_t			devt;
	spinlock_t		spi_lock;
	struct spi_device	*spi;
	struct list_head	device_entry;

	/* buffer is NULL unless this device is open (users > 0) */
	struct mutex		buf_lock;
	unsigned		users;
	u8			*buffer;
};

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);
#define MAX_SPI_BUFFER_SIZE 200000//96256*2 // 1280 * 188
static unsigned bufsiz = MAX_SPI_BUFFER_SIZE + 1024;
module_param(bufsiz, uint, S_IRUGO);
MODULE_PARM_DESC(bufsiz, "data bytes in biggest supported SPI message");

static int spidev_remove(struct spi_device *spi);

/////////////////////////////////////////////
#include <linux/delay.h>
#include <linux/gpio.h>
static struct spi_device *sony_dev_spi;

unsigned char g_buffer_array[MAX_SPI_BUFFER_SIZE+1024] = {0};
#define SONY_HW_DRVNAME "Sony_hw"

#define RESET_GPIO    36
#define POWER_ENABLE_GPIO  50
#define SWITCH_GPIO   69


struct regulator *dtv_vio = NULL;

void sony_gpio_init(void)
{
  	int err;
  	SPIDEV_LOG("tracy:sony_gpio_init");

  	err = gpio_request(RESET_GPIO,"sony_dtv_reset");
  	if(err)
		SPIDEV_ERR("gpio_request RESET_GPIO failed \n");
  	err = gpio_request(SWITCH_GPIO,"sony_dtv_switch");
  	if(err)
		SPIDEV_ERR("gpio_request SWITCH_GPIO failed \n");
  	err = gpio_request(POWER_ENABLE_GPIO,"sony_dtv_power_en");
  	if(err)
		SPIDEV_ERR("gpio_request POWER_ENABLE_GPIO failed \n");
}
//[DTV][kent][20151115][begin]free gpio to suspend mode for reducing current consumption 0.04ma
void sony_gpio_request(void)
{
	int err;
	err = gpio_request(RESET_GPIO,"sony_dtv_reset");
	if(err)
		SPIDEV_ERR("gpio_request RESET_GPIO failed \n");
}

void sony_gpio_free(void)
{
    gpio_free(RESET_GPIO);
  	SPIDEV_LOG("gpio_free RESET_GPIO \n");
}
//[DTV][kent][20151115][end]free gpio to suspend mode for reducing current consumption 0.04ma

void sony_reset(int on_off)
{
	//n_Reset
	SPIDEV_LOG("%s %d on_off %d\n",__func__,__LINE__,on_off);
	if(on_off ==1)
	{
	gpio_direction_output(RESET_GPIO,0);
	mdelay(50);
	gpio_direction_output(RESET_GPIO,1);
	mdelay(60);
	}else
	{
		gpio_direction_output(RESET_GPIO,0);
	}
}

void sony_ant_switch(int on_off)
{
	SPIDEV_LOG("%s %d \n",__func__,__LINE__);
	if(on_off ==1)
		gpio_direction_output(SWITCH_GPIO,1);
	else
		gpio_direction_output(SWITCH_GPIO,0);
}
void ant_switch_power(int on_off)
{
	int retval;
	if(on_off)
	{
		if (dtv_vio==NULL)
		{
			dtv_vio = regulator_get(&sony_dev_spi->dev, "vio_l27");
			if (IS_ERR(dtv_vio)) {
				pr_err("In %s, vio supply is not provided\n", __func__);
				dtv_vio =NULL;
				return;
			} else {
				pr_err("In %s, vio supply is SUCCESS\n", __func__);
				retval = regulator_set_voltage(dtv_vio,
						2050000,2050000);
				if (retval < 0) {
					pr_err("set_vol(%p) fail %d\n",dtv_vio , retval);
				}
			}
			retval = regulator_enable(dtv_vio);
			if (retval < 0) {
				regulator_put(dtv_vio);
				dtv_vio =NULL;
				pr_err("reg enable(%p) failed.rc=%d\n", dtv_vio, retval);
			}
		}
	}else
	{
		if (dtv_vio!=NULL)
		{
			retval = regulator_disable(dtv_vio);
			if (retval < 0) {
					pr_err("reg disable(%p) failed.rc=%d\n", dtv_vio, retval);
				}
			regulator_put(dtv_vio);
			dtv_vio = NULL;
		}
	}
}
void sony_gpio_power_on(void)
{

	SPIDEV_LOG("%s %d \n",__func__,__LINE__);
	gpio_direction_output(POWER_ENABLE_GPIO,1);

}

void sony_gpio_power_off(void)
{
	SPIDEV_LOG("%s %d \n",__func__,__LINE__);
	gpio_direction_output(POWER_ENABLE_GPIO,0);

}


int sony_spi_read(u8 *buf, size_t len)
{

	struct spi_message msg;
	struct spi_transfer	transfer[2];
	unsigned char status = 0;
	int r_len;
	//unsigned char temp_buf[256] = {0};
	//unsigned char txArry[32] = {0};
       SPIDEV_LOG("tracy:sony_spi_read");
	memset(&msg, 0, sizeof(msg));
	memset(transfer, 0, sizeof(transfer));

	spi_message_init(&msg);
	msg.spi = sony_dev_spi;
#if 0
	transfer[0].tx_buf = (unsigned char *)NULL;
	transfer[0].rx_buf = (unsigned char *)buf;
	transfer[0].len = len;
	transfer[0].bits_per_word = 8;
	transfer[0].delay_usecs = 0;
#else
	//transfer[1].tx_buf = txArry;
	//transfer[1].tx_buf = g_buffer_array;
	transfer[1].rx_buf = (unsigned char *)buf;
	transfer[1].len = len;
	transfer[1].bits_per_word = 8;
	transfer[1].delay_usecs = 0;
#endif

	spi_message_add_tail(&(transfer[1]), &msg);
	status = spi_sync(sony_dev_spi, &msg);

	if (status==0)
	{
		//r_len = msg.actual_length;
		r_len = len;
	}
	else
	{
		r_len =status;
	}

	return r_len;
}

int sony_spi_write(u8 *buf, size_t len)
{

	struct spi_message msg;
	struct spi_transfer	transfer[2];
	unsigned char status = 0;
    int w_len;
	//unsigned char rxArry[32] = {0};

	memset(&msg, 0, sizeof(msg));
	memset(transfer, 0, sizeof(transfer));

	spi_message_init(&msg);
	msg.spi = sony_dev_spi;
	SPIDEV_LOG("tracy:sony_spi_write");
#if 0
	transfer[0].tx_buf = (unsigned char *)buf;
	transfer[0].rx_buf = (unsigned char *)NULL;
	transfer[0].len = len;
	transfer[0].bits_per_word = 8;
	transfer[0].delay_usecs = 0;
#else
	transfer[0].tx_buf = (unsigned char *)buf;
	//transfer[0].rx_buf = (unsigned char *)NULL;
	//transfer[0].rx_buf = rxArry;
	//transfer[0].rx_buf = g_buffer_array;
	transfer[0].len = len;
	transfer[0].bits_per_word = 8;
	transfer[0].delay_usecs = 0;
#endif

	spi_message_add_tail(&(transfer[0]), &msg);
	status = spi_sync(sony_dev_spi, &msg);

	if (status==0)
		w_len = len;
	else
		w_len =status;

	return w_len;
}
int sony_spi_cmd3(u8 *buf, size_t len)
{

	struct spi_message msg;
	struct spi_transfer	transfer[2];
	unsigned char status = 0;
	 unsigned char chip_id = 0;
	//int w_len;
	//unsigned char rxArry[32] = {0};
       SPIDEV_LOG("tracy:sony_spi_cmd3");
	memset(&msg, 0, sizeof(msg));
	memset(transfer, 0, sizeof(transfer));

	spi_message_init(&msg);
	msg.spi = sony_dev_spi;
#if 0
	transfer[0].tx_buf = (unsigned char *)buf;
	transfer[0].rx_buf = (unsigned char *)NULL;
	transfer[0].len = len;
	transfer[0].bits_per_word = 8;
	transfer[0].delay_usecs = 0;
#else
	transfer[0].tx_buf = (unsigned char *)buf;
	//transfer[0].rx_buf = (unsigned char *)NULL;
	//transfer[0].rx_buf = rxArry;
	//transfer[0].rx_buf = g_buffer_array;
	transfer[0].len = len;
	transfer[0].bits_per_word = 8;
	transfer[0].delay_usecs = 0;
#endif

	spi_message_add_tail(&(transfer[0]), &msg);
	//status = spi_sync(sony_dev_spi, &msg);


	//transfer[1].tx_buf = txArry;
	//transfer[1].tx_buf = g_buffer_array;
	transfer[1].rx_buf = &chip_id ;
	transfer[1].len = 1;
	transfer[1].bits_per_word = 8;
	transfer[1].delay_usecs = 0;

	spi_message_add_tail(&(transfer[1]), &msg);
	status = spi_sync(sony_dev_spi, &msg);

	//Add by Fred NVram Backup chip id 20150716+++
	Chip_Id_BackUp = chip_id;
	//Add by Fred NVram Backup chip id 20150716---
  SPIDEV_LOG("tuner chip id : %d status %d",chip_id,status);

	return 0;
}

void sony_spi_read_chip_id(void)
{

	int ret_size;

	unsigned char writeBank[5] = { 0x0F, 0, 1, 0, 0 };
	unsigned char writeData[6] = { 0x0B, 0xFD, 1, 0, 0, 0 };

	SPIDEV_LOG("tracy:ssony_spi_read_chip_id");
	//chip.inp.hlp.write(0x61, b, len);
	ret_size = sony_spi_write(writeBank,5);
	SPIDEV_LOG("writeBank ret_size 1= %d\n",ret_size);
	ret_size = sony_spi_cmd3(writeData,6);
	SPIDEV_LOG("writeData ret_size 1= %d\n",ret_size);
	/**
		Wait for complete
	**/

}
void sony_spi_read_chip_id2(void)
{

	int retry;
	unsigned char sta = 0;
	int ret_size;

	unsigned char writeBank[5] = { 0x0F, 0, 1, 0, 0 };
	unsigned char writeData[6] = { 0x0B, 0xFD, 1, 0, 0, 0 };

	//chip.inp.hlp.write(0x61, b, len);
	ret_size = sony_spi_write(writeBank,5);
	SPIDEV_LOG("writeBank ret_size 1= %d\n",ret_size);
	ret_size = sony_spi_write(writeData,2);
	SPIDEV_LOG("writeData ret_size 1= %d\n",ret_size);
	/**
		Wait for complete
	**/
	retry = 10;

	do {
		//chip.inp.hlp.read(0x61, &sta, 1);
		SPIDEV_LOG("chip id retry = %d\n",retry);

		//ret_size = sony_spi_read(&sta,1);
		ret_size = sony_spi_read(&sta,2);
		if (sta == 0xff)
			break;
		else
			SPIDEV_LOG("\n Thunder sta = %d ,ret_size= %d\n",sta,ret_size);
			//spi_dbg("\n Thunder sta = %d %d %d %d ,ret_size= %d\n",(int)sta[0],(int)sta[1],(int)sta[2],(int)sta[3],ret_size);

		mdelay(3);
	}while (retry--);

	SPIDEV_LOG("\nchip id ret_size 2= %d\n",ret_size);
	SPIDEV_LOG("chip id sta = %d\n",sta);
	//spi_dbg("chip id sta = %d\n",(int)sta[0]);

//	if (sta== 0xff) {
//		/**
//			Read the Count
//		**/
//		//chip.inp.hlp.read(0x61, b, 3);
//		sony_spi_read(b,3);
//		len = b[2] | (b[1] << 8) | (b[0] << 16);
//		if (len == 4) {
//			//chip.inp.hlp.read(0x61, (uint8_t *)&val, 4);
//			sony_spi_read((unsigned char*)&val,4);
//			b[0] = 0xff;
//			//chip.inp.hlp.write(0x61, b, 1);
//			sony_spi_write(b,1);
//
//			//spi_dbg("===============================\n",val);
//      SPIDEV_LOG("===============================\n");
//      SPIDEV_LOG("sony Chip ID = [0x%x] on SPI\n",(int) val);
//      SPIDEV_LOG("sony Chip ID on SPI\n");
//      SPIDEV_LOG("===============================\n");
//		} else {
//			//spi_dbg("Error, SPI bus, bad count (%d)\n", len);
//			SPIDEV_LOG("Error, SPI bus, bad count \n");
//		}
//	}
// 	else {
// 		SPIDEV_LOG("Error, SPI bus, not complete\n");
//	}

}
/////////////////////////////////////////////////////////////////////



/*-------------------------------------------------------------------------*/

/*
 * We can't use the standard synchronous wrappers for file I/O; we
 * need to protect against async removal of the underlying spi_device.
 */
static void spidev_complete(void *arg)
{
	complete(arg);
}

static ssize_t
spidev_sync(struct spidev_data *spidev, struct spi_message *message)
{
	DECLARE_COMPLETION_ONSTACK(done);
	int status;

	message->complete = spidev_complete;
	message->context = &done;

	spin_lock_irq(&spidev->spi_lock);
	if (spidev->spi == NULL)
		status = -ESHUTDOWN;
	else
		status = spi_async(spidev->spi, message);
	spin_unlock_irq(&spidev->spi_lock);

	if (status == 0) {
		wait_for_completion(&done);
		status = message->status;
		if (status == 0)
			status = message->actual_length;
	}
	return status;
}

static inline ssize_t
spidev_sync_write(struct spidev_data *spidev, size_t len)
{
	struct spi_transfer	t = {
			.tx_buf		= g_buffer_array,//spidev->buffer,
			.len		= len,
		};
	struct spi_message	m;


	//t.rx_buf = g_buffer_array;
	t.bits_per_word = 8;
	t.delay_usecs = 0;

	spi_message_init(&m);
	m.spi = sony_dev_spi;
	spi_message_add_tail(&t, &m);
	return spidev_sync(spidev, &m);
}

static inline ssize_t
spidev_sync_read(struct spidev_data *spidev, size_t len)
{
	struct spi_transfer	t = {
			.rx_buf		= g_buffer_array,//spidev->buffer,
			.len		= len,
		};
	struct spi_message	m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spidev_sync(spidev, &m);
}

/*-------------------------------------------------------------------------*/

/* Read-only message with current device setup */
static ssize_t
spidev_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct spidev_data	*spidev;
	ssize_t			status = 0;

        SPIDEV_LOG(" tracy:spidev_read");
	/* chipselect only toggles at start or end of operation */
	if (count > bufsiz)
	{
		SPIDEV_ERR("error: memory out of buffer");
		return -EMSGSIZE;
	}
	spidev = filp->private_data;

	mutex_lock(&spidev->buf_lock);
	status = spidev_sync_read(spidev, count);
	if (status > 0) {
		unsigned long	missing;

		missing = copy_to_user(buf, g_buffer_array/*spidev->buffer*/, status);
		if (missing == status)
			status = -EFAULT;
		else
		{
			SPIDEV_ERR("error: status %d",(int)status);
			status = status - missing;
		}
	}
	SPIDEV_ERR("status %d",(int)status);
	mutex_unlock(&spidev->buf_lock);

	return status;
}

/* Write-only message with current device setup */
static ssize_t
spidev_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *f_pos)
{
	struct spidev_data	*spidev;
	ssize_t			status = 0;
	unsigned long		missing;

        SPIDEV_LOG(" tracy:spidev_write");
	/* chipselect only toggles at start or end of operation */
	if (count > bufsiz)
	{
		SPIDEV_ERR("error: memory out of buffer");
		return -EMSGSIZE;
	}
	spidev = filp->private_data;

	mutex_lock(&spidev->buf_lock);
	missing = copy_from_user(g_buffer_array/*spidev->buffer*/, buf, count);
	if (missing == 0) {
		//status = spidev_sync_write(spidev, count);
		status =  sony_spi_write(g_buffer_array/*spidev->buffer*/,count);
	} else
	{
		status = -EFAULT;
		SPIDEV_ERR("error: status %d",(int)status);
	}
	SPIDEV_LOG("status %d",(int)status);
	mutex_unlock(&spidev->buf_lock);

	return status;
}
#define SPI_4B_ALIGN 0x4
#define PACKET_SIZE  0x400
//#define ALIG_4_BYTE
static int spidev_message(struct spidev_data *spidev,
		struct spi_ioc_transfer *u_xfers, unsigned n_xfers)
{
	struct spi_message	msg;
	struct spi_transfer	*k_xfers;
	struct spi_transfer	*k_tmp;
	struct spi_ioc_transfer *u_tmp;
	unsigned		n, total;
	u8			*buf;
	int			status = -EFAULT;
#ifdef ALIG_4_BYTE
	uintptr_t buf_addr;
#endif

	SPIDEV_LOG(" spi_message_init");
	k_xfers = kcalloc(n_xfers, sizeof(*k_tmp), GFP_KERNEL);
	if (k_xfers == NULL)
		return -ENOMEM;
    //kent
	memset(k_xfers, 0, n_xfers*sizeof(*k_tmp));
	memset(&msg, 0, sizeof(msg));
	spi_message_init(&msg);
    msg.spi = sony_dev_spi;
	/* Construct spi_message, copying any tx data to bounce buffer.
	 * We walk the array of user-provided transfers, using each one
	 * to initialize a kernel version of the same transfer.
	 */
	buf = g_buffer_array;//spidev->buffer;
	//memset(buf, 0, bufsiz);  //[dtv][Kent][151211]move this to increase performace
	total = 0;
	for (n = n_xfers, k_tmp = k_xfers, u_tmp = u_xfers;
			n;
			n--, k_tmp++, u_tmp++) {

		k_tmp->len = u_tmp->len;

		total += k_tmp->len;
		if (total > bufsiz) {
			status = -EMSGSIZE;
			SPIDEV_ERR(" EMSGSIZE error total %d pkg length %d",total,k_tmp->len);
			goto done;
		}

		if (u_tmp->rx_buf)
		{
#ifdef ALIG_4_BYTE
		buf_addr = (uintptr_t)(void*)buf;

		if(buf_addr % (SPI_4B_ALIGN ))
		{
			buf_addr += ( SPI_4B_ALIGN - (buf_addr%(SPI_4B_ALIGN)) );
			SPIDEV_LOG(" ALIGN 4 addr to %p to %p",buf,(void*)buf_addr);
			buf =(u8*)buf_addr;
		}
#endif
			k_tmp->rx_buf = buf;
			if (!access_ok(VERIFY_WRITE, (u8 __user *)
						(uintptr_t) u_tmp->rx_buf,
						u_tmp->len))
			{
				SPIDEV_ERR(" access_ok failed");
				goto done;
			}
			//k_tmp->tx_buf = g_buffer_array;//[dtv][Kent][151211]qualcomm do not use this
			
			//[dtv][Kent][151211][begin]qualcomm do not need 1024 pack, MTK need it
			//1024
//			if(!(k_tmp->len < PACKET_SIZE))
//			{
//				if(k_tmp->len % 1024)
//				{
//					k_tmp->len +=(1024 - (k_tmp->len % 1024) );
//					SPIDEV_LOG(" ALIGN 1024 buff length to %d ",k_tmp->len);
//				}
//			}
			//[dtv][Kent][151211][end]qualcomm do not need 1024 pack, MTK need it
		}

		if (u_tmp->tx_buf)
		{
#ifdef ALIG_4_BYTE
		buf_addr = (uintptr_t)(void*)buf;

		if(buf_addr % (SPI_4B_ALIGN ))
		{
			buf_addr += ( SPI_4B_ALIGN - (buf_addr%(SPI_4B_ALIGN)) );
			SPIDEV_LOG(" ALIGN 4 addr to %p to %p",buf,(void*)buf_addr);
			buf =(u8*)buf_addr;
		}
#endif
			k_tmp->tx_buf = buf;
			if (copy_from_user(buf, (const u8 __user *)
						(uintptr_t) u_tmp->tx_buf,
					u_tmp->len))
			{
				SPIDEV_ERR(" tx copy failed ");
				goto done;
			}

			//k_tmp->rx_buf = g_buffer_array;//[dtv][Kent][151211]qualcomm do not use this
		}


		buf += k_tmp->len;



		k_tmp->cs_change = !!u_tmp->cs_change;
		k_tmp->bits_per_word = u_tmp->bits_per_word;
		k_tmp->delay_usecs = u_tmp->delay_usecs;
		k_tmp->speed_hz = u_tmp->speed_hz;
#if 0
		dev_dbg(&spidev->spi->dev,
			"  xfer len %d %s%s%s%dbits %u usec %uHz\n",
			u_tmp->len,
			u_tmp->rx_buf ? "rx " : "",
			u_tmp->tx_buf ? "tx " : "",
			u_tmp->cs_change ? "cs " : "",
			u_tmp->bits_per_word ? : spidev->spi->bits_per_word,
			u_tmp->delay_usecs,
			u_tmp->speed_hz ? : spidev->spi->max_speed_hz);
#endif
		SPIDEV_LOG("  xfer len %d %s%s%s%dbits %u usec %uHz\n",
					u_tmp->len,
					u_tmp->rx_buf ? "rx " : "",
					u_tmp->tx_buf ? "tx " : "",
					u_tmp->cs_change ? "cs " : "",
					u_tmp->bits_per_word ? : spidev->spi->bits_per_word,
					u_tmp->delay_usecs,
					u_tmp->speed_hz ? : spidev->spi->max_speed_hz);

		spi_message_add_tail(k_tmp, &msg);
		//kent

	}

//kent
#if 1
	//status = spidev_sync(spidev, &msg);
	status = spi_sync(sony_dev_spi,&msg);
	if (status < 0)
	{
		SPIDEV_ERR(" spidev_sync failed ");
		goto done;
	}else
		SPIDEV_LOG("send len %d ",status);
#else
	status = spi_sync(sony_dev_spi, &msg);
	if (status < 0)
	{
		SPIDEV_LOG(" spidev_sync failed ");
	}
#endif

	/* copy any rx data out of bounce buffer */
#if 0
	for (n = n_xfers, u_tmp = u_xfers; n; n--, u_tmp++) {
		if (u_tmp->rx_buf) {
			if (__copy_to_user((u8 __user *)
					(uintptr_t) u_tmp->rx_buf, buf,
					u_tmp->len)) {
				status = -EFAULT;
				SPIDEV_LOG(" rd copy failed ");
				goto done;
			}
		}
		buf += u_tmp->len;
	}
#else
	total = 0;
	for (n = n_xfers, k_tmp = k_xfers, u_tmp = u_xfers;
		 n;
		 n--, k_tmp++, u_tmp++) {
		if (u_tmp->rx_buf) {
			if (__copy_to_user((u8 __user *)
					(uintptr_t) u_tmp->rx_buf, k_tmp->rx_buf,
					u_tmp->len)) {
				status = -EFAULT;
				SPIDEV_ERR(" rd copy failed ");
				goto done;
			}
#ifdef TRACE_MSG_BUFF
			else{
				int loop_i,loop_j,loop_ptr=0;
				SPIDEV_LOG("Rx buffer: length %d \n",u_tmp->len);
				buf = k_tmp->rx_buf;

				if(u_tmp->len>100)
				{
					for (loop_i=0;loop_i<3;loop_i++)
					{
						printk("[SPI-UT]:[rx] [%s]:[%d] begin 3" , __func__, __LINE__);
						for (loop_j=0;loop_j<20;loop_j++)
							printk("[%02x]",buf[loop_ptr++]);
						printk("\n");
					}
					loop_ptr =u_tmp->len - 40;
					for (loop_i=0;loop_i<2;loop_i++)
					{
						printk("[SPI-UT]:[rx] [%s]:[%d] end 2" , __func__, __LINE__);
						for (loop_j=0;loop_j<20;loop_j++)
							printk("[%02x]",buf[loop_ptr++]);
						printk("\n");
					}

				}
				else{
					loop_i=u_tmp->len/20;
					for (;loop_i>0;loop_i--)
					{
						printk("[SPI-UT]:[rx] [%s]:[%d]" , __func__, __LINE__);
						for (loop_j=0;loop_j<20;loop_j++)
							printk("[%02x]",buf[loop_ptr++]);
						printk("\n");
					}
					printk("[SPI-UT]:[rx] [%s]:[%d]" , __func__, __LINE__);
					for (loop_j=u_tmp->len%20;loop_j>0;loop_j--)
						printk("[%02x]",buf[loop_ptr++]);
					printk("\n");
				}
			}
#endif
		}
#ifdef TRACE_MSG_BUFF
		else
		{
			int loop_i,loop_j,loop_ptr=0;
			SPIDEV_LOG("Tx buffer: length %d \n",u_tmp->len);
			buf =(u8*) k_tmp->tx_buf;

			if(u_tmp->len>100)
				loop_i=5;
			else
				loop_i=u_tmp->len/20;
			for (;loop_i>0;loop_i--)
			{
				printk("[SPI-UT]:[tx] [%s]:[%d]" , __func__, __LINE__);
				for (loop_j=0;loop_j<20;loop_j++)
					printk("[%02x]",buf[loop_ptr++]);
				printk("\n");
			}
			printk("[SPI-UT]:[tx] [%s]:[%d]" , __func__, __LINE__);
			for (loop_j=u_tmp->len%20;loop_j>0;loop_j--)
				printk("[%02x]",buf[loop_ptr++]);
			printk("\n");
		}
#endif
		total += u_tmp->len;
	}
#endif
	status = total;

done:
	kfree(k_xfers);
	return status;
}

static long
spidev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int			err = 0;
	int			retval = 0;
	struct spidev_data	*spidev;
	struct spi_device	*spi;
	u32			tmp;
	unsigned		n_ioc;
	struct spi_ioc_transfer	*ioc;

	/* Check type and command number */
	SPIDEV_LOG(" E");
	if (_IOC_TYPE(cmd) != SPI_IOC_MAGIC)
		return -ENOTTY;

	/* Check access direction once here; don't repeat below.
	 * IOC_DIR is from the user perspective, while access_ok is
	 * from the kernel perspective; so they look reversed.
	 */

	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE,
				(void __user *)arg, _IOC_SIZE(cmd));
	if (err == 0 && _IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ,
				(void __user *)arg, _IOC_SIZE(cmd));
	if (err)
		return -EFAULT;

	/* guard against device removal before, or while,
	 * we issue this ioctl.
	 */
	spidev = filp->private_data;
	spin_lock_irq(&spidev->spi_lock);
	spi = spi_dev_get(spidev->spi);
	spin_unlock_irq(&spidev->spi_lock);
	SPIDEV_LOG(" check device");
	if (spi == NULL)
		return -ESHUTDOWN;

	/* use the buffer lock here for triple duty:
	 *  - prevent I/O (from us) so calling spi_setup() is safe;
	 *  - prevent concurrent SPI_IOC_WR_* from morphing
	 *    data fields while SPI_IOC_RD_* reads them;
	 *  - SPI_IOC_MESSAGE needs the buffer locked "normally".
	 */
	mutex_lock(&spidev->buf_lock);

	switch (cmd) {
	//20150806 tracy add for dtv power on/off when phone sleep/wakeup start
	case SPI_IOC_WR_TUNER_POWER:
		SPIDEV_LOG(" SPI_IOC_WR_TUNER_POWER");
		retval = __get_user(tmp, (u8 __user *)arg);
		SPIDEV_LOG(" SPI_IOC_WR_TUNER_POWER retval= %d \n",retval);
		if (retval == 0) {
			if(tmp == SPI_TUNER_POWER_ON) {
				SPIDEV_LOG(" SPI_IOC_WR_TUNER_POWER ON");
				sony_gpio_power_on();
			} else {
				SPIDEV_LOG(" SPI_IOC_WR_TUNER_POWER OFF");
				sony_gpio_power_off();
			}
		}
		sony_spi_read_chip_id();
		break;
	//20150806 tracy add for dtv power on/off when phone sleep/wakeup end
	/* read requests */
	case SPI_IOC_RD_MODE:
		SPIDEV_LOG(" SPI_IOC_RD_MODE");
		retval = __put_user(spi->mode & SPI_MODE_MASK,
					(__u8 __user *)arg);
		break;
	case SPI_IOC_RD_LSB_FIRST:
		SPIDEV_LOG(" SPI_IOC_RD_LSB_FIRST");
		retval = __put_user((spi->mode & SPI_LSB_FIRST) ?  1 : 0,
					(__u8 __user *)arg);
		break;
	case SPI_IOC_RD_BITS_PER_WORD:
		retval = __put_user(spi->bits_per_word, (__u8 __user *)arg);
		SPIDEV_LOG(" SPI_IOC_RD_BITS_PER_WORD %d", spi->bits_per_word);
		break;
	case SPI_IOC_RD_MAX_SPEED_HZ:

		retval = __put_user(spi->max_speed_hz, (__u32 __user *)arg);
		SPIDEV_LOG(" SPI_IOC_RD_MAX_SPEED_HZ %d ",spi->max_speed_hz);
		break;

	/* write requests */
	case SPI_IOC_WR_MODE:
		SPIDEV_LOG(" SPI_IOC_WR_MODE");
		retval = __get_user(tmp, (u8 __user *)arg);
		SPIDEV_LOG(" SPI_IOC_WR_MODE %d",tmp);
		if (retval == 0) {
			u8	save = spi->mode;

			if (tmp & ~SPI_MODE_MASK) {
				retval = -EINVAL;
				break;
			}

			tmp |= spi->mode & ~SPI_MODE_MASK;
			spi->mode = (u8)tmp;
			retval = spi_setup(spi);
			if (retval < 0)
				spi->mode = save;
			else
				dev_dbg(&spi->dev, "spi mode %02x\n", tmp);
		}
		sony_spi_read_chip_id();
		break;
	case SPI_IOC_WR_LSB_FIRST:
		SPIDEV_LOG(" SPI_IOC_WR_LSB_FIRST");
		retval = __get_user(tmp, (__u8 __user *)arg);
		if (retval == 0) {
			u8	save = spi->mode;

			if (tmp)
				spi->mode |= SPI_LSB_FIRST;
			else
				spi->mode &= ~SPI_LSB_FIRST;
			retval = spi_setup(spi);
			if (retval < 0)
				spi->mode = save;
			else
				dev_dbg(&spi->dev, "%csb first\n",
						tmp ? 'l' : 'm');
		}
		break;
	case SPI_IOC_WR_BITS_PER_WORD:
		retval = __get_user(tmp, (__u8 __user *)arg);
		SPIDEV_LOG(" SPI_IOC_WR_BITS_PER_WORD %d",tmp);
		if (retval == 0) {
			u8	save = spi->bits_per_word;

			spi->bits_per_word = tmp;
			retval = spi_setup(spi);
			if (retval < 0)
				spi->bits_per_word = save;
			else
				dev_dbg(&spi->dev, "%d bits per word\n", tmp);
		}
		break;
	case SPI_IOC_WR_MAX_SPEED_HZ:

		retval = __get_user(tmp, (__u32 __user *)arg);
			SPIDEV_LOG(" SPI_IOC_WR_MAX_SPEED_HZ %d",tmp);

		if (retval == 0) {
			u32	save = spi->max_speed_hz;

			//[DTV][kent][151211][begin]fixd on 25Mhz becasue there are error above 30MHZ.
			//spi->max_speed_hz = tmp;
			spi->max_speed_hz = 25000000;
			//[DTV][kent][151211][end]fixd on 25Mhz becasue there are error above 30MHZ.

			retval = spi_setup(spi);
			if (retval < 0)
				spi->max_speed_hz = save;
			else
				dev_dbg(&spi->dev, "%d Hz (max)\n", tmp);
		}
		sony_spi_read_chip_id();
		break;

	default:

		/* segmented and/or full-duplex I/O request */
		if (_IOC_NR(cmd) != _IOC_NR(SPI_IOC_MESSAGE(0))
				|| _IOC_DIR(cmd) != _IOC_WRITE) {
			retval = -ENOTTY;
			SPIDEV_ERR(" command error %d",cmd);
			break;
		}

		tmp = _IOC_SIZE(cmd);
		if ((tmp % sizeof(struct spi_ioc_transfer)) != 0) {
			retval = -EINVAL;
			SPIDEV_ERR("cmd size error %d",tmp);
			break;
		}

		n_ioc = tmp / sizeof(struct spi_ioc_transfer);

		if (n_ioc == 0)
		{
			SPIDEV_LOG(" message is zero");
			break;
		}
		/* copy into scratch area */
		ioc = kmalloc(tmp, GFP_KERNEL);
		if (!ioc) {
			retval = -ENOMEM;
			SPIDEV_ERR(" memory not enough");
			break;
		}
		if (__copy_from_user(ioc, (void __user *)arg, tmp)) {
			kfree(ioc);
			retval = -EFAULT;
			SPIDEV_ERR(" copy failed ");
			break;
		}

		/* translate to spi_message, execute */
		retval = spidev_message(spidev, ioc, n_ioc);
		kfree(ioc);
		break;
	}

	mutex_unlock(&spidev->buf_lock);
	spi_dev_put(spi);
	SPIDEV_LOG(" X");
	return retval;
}
//Add by Fred NVram Backup chip id 20150716+++
/*----------------------------------------------------------------------------*/
#if 1
static ssize_t spidev_show_chip_id(struct device *dev, struct device_attribute *attr, char *buf)
{
    ssize_t len = 0;
    	len += snprintf(buf + len, PAGE_SIZE - len, "%d", Chip_Id_BackUp);
    return len;

}
#endif
/*----------------------------------------------------------------------------*/
static struct device_attribute spidev_chip_id =__ATTR(show_chip_id,S_IROTH | S_IWOTH, spidev_show_chip_id, NULL);
/*----------------------------------------------------------------------------*/
static struct attribute * spidev_attr_list[] =
{
    &spidev_chip_id.attr,
    NULL,
};

static struct attribute_group spidev_attr_grp={
  .attrs = spidev_attr_list,
};
//Add by Fred NVram Backup chip id 20150716---
#ifdef CONFIG_COMPAT
static long
spidev_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return spidev_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#else
#define spidev_compat_ioctl NULL
#endif /* CONFIG_COMPAT */

static int spidev_open(struct inode *inode, struct file *filp)
{
	struct spidev_data	*spidev;
	int			status = -ENXIO,retval;

	SPIDEV_LOG(" E");
	SPIDEV_LOG("tracy:spidev_open");
	mutex_lock(&device_list_lock);

	list_for_each_entry(spidev, &device_list, device_entry) {
		if (spidev->devt == inode->i_rdev) {
			status = 0;
			break;
		}
	}
	if (status == 0) {
		 //[dtv][Kent][151211][begin]use globe buffer, avoid alloc memeory error
//		if (!spidev->buffer) {
//			spidev->buffer = kmalloc(bufsiz, GFP_KERNEL);
//			if (!spidev->buffer) {
//				dev_dbg(&spidev->spi->dev, "open/ENOMEM\n");
//				status = -ENOMEM;
//			}
//		}
		//[dtv][Kent][151211][end]use globe buffer, avoid alloc memeory error
		if (status == 0) {
			//kent
			if(spidev->users == 0)
			{
//				sony_gpio_init();
				//[DTV][kent][15111501][begin]free gpio to suspend mode for reducing current consumption 0.04ma
				sony_gpio_request();
				//[DTV][kent][15111501][end]free gpio to suspend mode for reducing current consumption 0.04ma
				sony_gpio_power_on();
				//[5816][Camera][KentChou][15111002][Begin] reduce 0.4ma suspend current on DTV
				ant_switch_power(1);
				//[5816][Camera][KentChou][15111002][end] reduce 0.4ma suspend current on DTV
				sony_ant_switch(1);
				sony_reset(1);

			}
			spidev->users++;
			filp->private_data = spidev;
			nonseekable_open(inode, filp);
		}
	} else
		pr_debug("spidev: nothing for minor %d\n", iminor(inode));

	mutex_unlock(&device_list_lock);
	////


	sony_dev_spi->mode = (SPI_MODE_0) ;
	sony_dev_spi->bits_per_word = 8 ;
	sony_dev_spi->max_speed_hz = 25000000;

	retval = spi_setup( sony_dev_spi );
	if( retval != 0 )
	{
		SPIDEV_LOG( "ERROR : %d\n", retval );
	}
	else {
		SPIDEV_LOG( "Done : %d\n", retval );
	}

	sony_spi_read_chip_id();

	SPIDEV_LOG(" X");
	return status;
}

static int spidev_release(struct inode *inode, struct file *filp)
{
	struct spidev_data	*spidev;
	int			status = 0;
	SPIDEV_LOG(" E");
	mutex_lock(&device_list_lock);
	spidev = filp->private_data;
	filp->private_data = NULL;

	/* last close? */
	spidev->users--;
	if (!spidev->users) {
		int		dofree;
		//kent
		//sony_gpio_init();
		sony_gpio_power_off();
		//[5816][Camera][KentChou][15111003][Begin] reduce 0.4ma suspend current on DTV
		ant_switch_power(0);
		//[5816][Camera][KentChou][15111003][end] reduce 0.4ma suspend current on DTV
		sony_ant_switch(0);
		sony_reset(0);
		//[DTV][kent][15111502][begin]free gpio to suspend mode for reducing current consumption 0.04ma
		sony_gpio_free();
		//[DTV][kent][15111502][end]free gpio to suspend mode for reducing current consumption 0.04ma
		//[dtv][Kent][151211][begin]use globe buffer, avoid alloc memeory error
		//kfree(spidev->buffer);
		//spidev->buffer = NULL;
		//[dtv][Kent][151211][end]use globe buffer, avoid alloc memeory error

		/* ... after we unbound from the underlying device? */
		spin_lock_irq(&spidev->spi_lock);
		dofree = (spidev->spi == NULL);
		spin_unlock_irq(&spidev->spi_lock);

		if (dofree)
			kfree(spidev);
	}
	mutex_unlock(&device_list_lock);
	SPIDEV_LOG(" X");
	return status;
}

static const struct file_operations spidev_fops = {
	.owner =	THIS_MODULE,
	/* REVISIT switch to aio primitives, so that userspace
	 * gets more complete API coverage.  It'll simplify things
	 * too, except for the locking.
	 */
	.write =	spidev_write,
	.read =		spidev_read,
	.unlocked_ioctl = spidev_ioctl,
	.compat_ioctl = spidev_compat_ioctl,
	.open =		spidev_open,
	.release =	spidev_release,
	.llseek =	no_llseek,
};

/*-------------------------------------------------------------------------*/

/* The main reason to have this class is to make mdev/udev create the
 * /dev/spidevB.C character device nodes exposing our userspace API.
 * It also simplifies memory management.
 */

static struct class *spidev_class;

/*-------------------------------------------------------------------------*/

static int spidev_probe(struct spi_device *spi)
{
	struct spidev_data	*spidev;
	int			status;
	unsigned long		minor;

	/* Allocate driver data */
	spidev = kzalloc(sizeof(*spidev), GFP_KERNEL);
	if (!spidev)
		return -ENOMEM;
    //kent
       SPIDEV_LOG("tracy:spidev_probe");



	memset(spidev, 0, sizeof(*spidev));

	/* Initialize the driver data */
	spidev->spi = spi;
	spin_lock_init(&spidev->spi_lock);
	mutex_init(&spidev->buf_lock);

	INIT_LIST_HEAD(&spidev->device_entry);

	/* If we can allocate a minor number, hook up this device.
	 * Reusing minors is fine so long as udev or mdev is working.
	 */
	mutex_lock(&device_list_lock);
	minor = find_first_zero_bit(minors, N_SPI_MINORS);
	if (minor < N_SPI_MINORS) {
		struct device *dev;

		spidev->devt = MKDEV(SPIDEV_MAJOR, minor);
		dev = device_create(spidev_class, &spi->dev, spidev->devt,
				    spidev, "spidev%d.%d",
				    spi->master->bus_num, spi->chip_select);
		status = PTR_RET(dev);
	} else {
		dev_dbg(&spi->dev, "no minor number available!\n");
		status = -ENODEV;
	}
	if (status == 0) {
		set_bit(minor, minors);
		list_add(&spidev->device_entry, &device_list);
	}
	mutex_unlock(&device_list_lock);

//Add by Fred NVram Backup chip id 20150716+++
  //soc_kobj = kobject_create_and_add("spidev_attr_list",NULL);
  status = sysfs_create_group(&spi->dev.kobj, &spidev_attr_grp);
  sysfs_status=1;
//Add by Fred NVram Backup chip id 20150716---
	if (status == 0)
		spi_set_drvdata(spi, spidev);
	else
		kfree(spidev);

	/////
	//kent
	sony_dev_spi = spi;

//20150828 tracy modify for DTV power on until run DTV APK start
	sony_gpio_init();
	sony_gpio_power_on();
	sony_reset(1);
	sony_spi_read_chip_id();
	sony_gpio_power_off();
	sony_reset(0);
//20150828 tracy modify for DTV power on until run DTV APK end
    if(Chip_Id_BackUp == 98)
    {
    	//[5816][Camera][KentChou][15111001][Begin] reduce 0.4ma suspend current on DTV
    	//ant_switch_power(1);
    	//[5816][Camera][KentChou][15111001][End] reduce 0.4ma suspend current on DTV
    }else
    {
    	SPIDEV_ERR("chip id error %d",Chip_Id_BackUp);
    	status = -ENODEV;
    	//spidev_remove(spi);
    }
    sony_gpio_free();
	return status;
}

static int spidev_remove(struct spi_device *spi)
{
	struct spidev_data	*spidev = spi_get_drvdata(spi);

	/* make sure ops on existing fds can abort cleanly */
	spin_lock_irq(&spidev->spi_lock);
	spidev->spi = NULL;
	spi_set_drvdata(spi, NULL);
	spin_unlock_irq(&spidev->spi_lock);

	/* prevent new opens */
	mutex_lock(&device_list_lock);
	list_del(&spidev->device_entry);
	device_destroy(spidev_class, spidev->devt);
	clear_bit(MINOR(spidev->devt), minors);
	if (spidev->users == 0)
		kfree(spidev);
	mutex_unlock(&device_list_lock);

	return 0;
}

static const struct of_device_id spidev_dt_ids[] = {
	{ .compatible = "rohm,dh2228fv" },
	{},
};

MODULE_DEVICE_TABLE(of, spidev_dt_ids);

static struct spi_driver spidev_spi_driver = {
	.driver = {
		.name =		"spidev",
		.bus	= &spi_bus_type,
		.owner =	THIS_MODULE,
		//.of_match_table = of_match_ptr(spidev_dt_ids),
	},
	.probe =	spidev_probe,
	.remove =	spidev_remove,

	/* NOTE:  suspend/resume methods are not necessary here.
	 * We don't do anything except pass the requests to/from
	 * the underlying controller.  The refrigerator handles
	 * most issues; the controller driver handles the rest.
	 */
};

/*-------------------------------------------------------------------------*/
static struct spi_board_info sony_spi[] = {
    {
        .modalias      = "spidev",
        .platform_data = NULL,
        .bus_num       = 0,
        .max_speed_hz  = 25000000,
        .chip_select   = 0,
        .mode          = SPI_MODE_0,
    },
};

static int __init spidev_init(void)
{
	int status;


	status = spi_register_board_info(sony_spi, ARRAY_SIZE(sony_spi));
	if (status < 0)
		return status;

	/* Claim our 256 reserved device numbers.  Then register a class
	 * that will key udev/mdev to add/remove /dev nodes.  Last, register
	 * the driver which manages those device numbers.
	 */
	 SPIDEV_LOG("tracy:spidev_init");
	BUILD_BUG_ON(N_SPI_MINORS > 256);
	status = register_chrdev(SPIDEV_MAJOR, "spi", &spidev_fops);
	if (status < 0)
		return status;

	spidev_class = class_create(THIS_MODULE, "spidev");
	if (IS_ERR(spidev_class)) {
		unregister_chrdev(SPIDEV_MAJOR, spidev_spi_driver.driver.name);
		return PTR_ERR(spidev_class);
	}

	status = spi_register_driver(&spidev_spi_driver);
	if (status < 0) {
		class_destroy(spidev_class);
		unregister_chrdev(SPIDEV_MAJOR, spidev_spi_driver.driver.name);
	}
	return status;
}
module_init(spidev_init);

static void __exit spidev_exit(void)
{
	spi_unregister_driver(&spidev_spi_driver);
	class_destroy(spidev_class);
//Add by Fred NVram Backup chip id 20150716+++
	if(sysfs_status)
	{
	  sysfs_status=0;
	  kobject_put(soc_kobj);
	  sysfs_remove_group(soc_kobj, &spidev_attr_grp);
  }
//Add by Fred NVram Backup chip id 20150716---
	unregister_chrdev(SPIDEV_MAJOR, spidev_spi_driver.driver.name);
}
module_exit(spidev_exit);

MODULE_AUTHOR("Andrea Paterniani, <a.paterniani@swapp-eng.it>");
MODULE_DESCRIPTION("User mode SPI device interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:spidev");
