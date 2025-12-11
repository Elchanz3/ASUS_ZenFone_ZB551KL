#ifndef _LINUX_ELAN_KTF_H
#define _LINUX_ELAN_KTF_H
#define ELAN_X_MAX			800
#define ELAN_Y_MAX			1280
#define L2500_ADDR			0x7bd0
#define EKTF2100_ADDR		0x7bd0
#define EKTF2200_ADDR		0x7bd0
#define EKTF3100_ADDR		0x7c16
#define FW_ADDR				L2500_ADDR
#define ELAN_KTF_NAME "elan_ktf"

struct elan_ktf_i2c_platform_data {
	uint16_t version;
	int abs_x_min;
	int abs_x_max;
	int abs_y_min;
	int abs_y_max;
	bool i2c_pull_up;
	int irq_gpio;
	int tpid_gpio;
	u32 irq_flags;
	u32 reset_flags;
	u32 tpid_flags;
	int reset_gpio;
	int mode_check_gpio;
	int (*power)(int on);
};

void elan_ktf_usb_detection(int supplymode);

#endif /* _LINUX_ELAN_KTF_H */
