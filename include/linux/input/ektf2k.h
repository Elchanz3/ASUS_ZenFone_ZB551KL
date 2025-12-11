#ifndef _LINUX_ELAN_KTF2K_H
#define _LINUX_ELAN_KTF2K_H

#if 1
#define ELAN_X_MAX      768
#define ELAN_Y_MAX      1344
#else
#define ELAN_X_MAX      1280
#define ELAN_Y_MAX      2112
#endif

#define ELAN_KTF2K_NAME "elan-ktf2k"

struct elan_ktf2k_i2c_platform_data {
	uint16_t version;
	int abs_x_min;
	int abs_x_max;
	int abs_y_min;
	int abs_y_max;
	bool i2c_pull_up;
	int irq_gpio;
	u32 irq_flags;
	u32 reset_flags;
	int reset_gpio;
// [All][Main][TP][32783]20140106,Eric, Add the vender and version informations.
	int hw_det_gpio;
	u32 hw_det_gpio_flags;
// [All][Main][TP][32783]end
	int mode_check_gpio;
	int (*power)(int on);
};

#endif /* _LINUX_ELAN_KTF2K_H */
