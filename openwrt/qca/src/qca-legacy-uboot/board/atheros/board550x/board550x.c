/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
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

#include <common.h>
#include <command.h>
#include <asm/mipsregs.h>
#include <asm/addrspace.h>
#include <config.h>
#include <version.h>
#include <atheros.h>

extern int ath_ddr_initial_config(uint32_t refresh);
extern int ath_ddr_find_size(void);

#ifdef COMPRESSED_UBOOT
#	define prmsg(...)
#	define args		char *s
#	define board_str(a)	do {			\
	char ver[] = "0";				\
	strcpy(s, a " - JET 1.");			\
	ver[0] += ath_reg_rd(RST_REVISION_ID_ADDRESS)	\
						& 0xf;	\
	strcat(s, ver);					\
} while (0)
#else
#	define prmsg	printf
#	define args		void
#	define board_str(a)				\
	printf(a " - JET 1.%d", ath_reg_rd		\
			(RST_REVISION_ID_ADDRESS) & 0xf)
#endif

void
ath_usb1_initial_config(void)
{
#define unset(a)	(~(a))

	ath_reg_wr_nf(SWITCH_CLOCK_SPARE_ADDRESS,
		ath_reg_rd(SWITCH_CLOCK_SPARE_ADDRESS) |
		SWITCH_CLOCK_SPARE_USB_REFCLK_FREQ_SEL_SET(5));
	udelay(1000);

	ath_reg_rmw_set(RST_RESET_ADDRESS,
				RST_RESET_USB_PHY_SUSPEND_OVERRIDE_SET(1));
	udelay(1000);
	ath_reg_rmw_clear(RST_RESET_ADDRESS, RST_RESET_USB_PHY_RESET_SET(1));
	udelay(1000);
	ath_reg_rmw_clear(RST_RESET_ADDRESS, RST_RESET_USB_PHY_ARESET_SET(1));
	udelay(1000);
	ath_reg_rmw_clear(RST_RESET_ADDRESS, RST_RESET_USB_HOST_RESET_SET(1));
	udelay(1000);

	ath_reg_rmw_clear(RST_RESET_ADDRESS, RST_RESET_USB_PHY_PLL_PWD_EXT_SET(1));
	udelay(10);

	ath_reg_rmw_set(RST_RESET2_ADDRESS, RST_RESET2_USB1_EXT_PWR_SEQ_SET(1));
	udelay(10);
}

void
ath_usb2_initial_config(void)
{
	if (is_drqfn()) {
		return;
	}

	ath_reg_rmw_set(RST_RESET2_ADDRESS, RST_RESET2_USB2_MODE_SET(1));
	udelay(10);
	ath_reg_rmw_set(RST_RESET2_ADDRESS,
				RST_RESET2_USB_PHY2_SUSPEND_OVERRIDE_SET(1));
	udelay(1000);
	ath_reg_rmw_clear(RST_RESET2_ADDRESS, RST_RESET2_USB_PHY2_RESET_SET(1));
	udelay(1000);
	ath_reg_rmw_clear(RST_RESET2_ADDRESS, RST_RESET2_USB_PHY2_ARESET_SET(1));
	udelay(1000);
	ath_reg_rmw_clear(RST_RESET2_ADDRESS, RST_RESET2_USB_HOST2_RESET_SET(1));
	udelay(1000);

	ath_reg_rmw_clear(RST_RESET2_ADDRESS, RST_RESET2_USB_PHY2_PLL_PWD_EXT_SET(1));
	udelay(10);

	ath_reg_rmw_set(RST_RESET2_ADDRESS, RST_RESET2_USB2_EXT_PWR_SEQ_SET(1));
	udelay(10);
}

#if defined(CONFIG_PRODUCT_A9V6)

#define TP_GPIO_LED_STATUS		17
#define TP_GPIO_LED_WLAN_2G		15
#define TP_GPIO_LED_WLAN_5G		20
#define TP_GPIO_LED_WAN_GREEN	4
#define TP_GPIO_LED_WAN_ORANGE	3
#define TP_GPIO_LED_USB			0
#define TP_GPIO_LED_WPS			1

#define TP_GPIO_BTN_RESET		16
#define TP_GPIO_BTN_WPS			18

/* active high */
static unsigned inline led_ah_mask()
{
	return 	(1 << TP_GPIO_LED_WLAN_5G) | 
			(1 << TP_GPIO_LED_USB);
}

static unsigned inline led_al_mask()
{
	return 	(1 << TP_GPIO_LED_WLAN_2G) | 
			(1 << TP_GPIO_LED_STATUS) |
			(1 << TP_GPIO_LED_WAN_GREEN) | 
			(1 << TP_GPIO_LED_WAN_ORANGE) | 
			(1 << TP_GPIO_LED_WPS);
}

static unsigned inline get_led_gpio_mask()
{
	return led_ah_mask() | led_al_mask();
}

static unsigned inline get_btn_gpio_mask()
{
	return (1 << TP_GPIO_BTN_RESET) | (1 << TP_GPIO_BTN_WPS);
}

/* some gpios are active high, 
	so add a wrapper to turn on/off all led 
	wan orange led is always keep off 
*/
static void inline turn_all_led(int onoff)
{
	unsigned old_value = ath_reg_rd(GPIO_OUT_ADDRESS);
	old_value &= ~ get_led_gpio_mask();
	
	if (onoff) {
		old_value |= led_ah_mask();
	}
	else {
		old_value |= led_al_mask();
	}
	old_value |= (1 << TP_GPIO_LED_WAN_ORANGE);
	ath_reg_wr(GPIO_OUT_ADDRESS, old_value);
}

/* gpio 4 default mux is CLK_OBS5
	NO DOCUMENT found FOR MUX value.
	So here copy GPIO 5 mux to GPIO 4.
 */
static void inline set_gpio_mux()
{
	unsigned v = ath_reg_rd(GPIO_OUT_FUNCTION1_ADDRESS);
	v &= ~ GPIO_OUT_FUNCTION1_ENABLE_GPIO_4_MASK;
	unsigned v2 = GPIO_OUT_FUNCTION1_ENABLE_GPIO_5_GET(v);
	v |= v2;
	ath_reg_wr(GPIO_OUT_FUNCTION1_ADDRESS, v);
}

/* Archer A9 WPS button and UART RX share GPIO18
 */

void select_gpio_18_func(int select)
{
	if (select == GPIO18_USED_AS_WPS) 
	{
		ath_reg_rmw_clear(GPIO_IN_ENABLE0_ADDRESS,
				GPIO_IN_ENABLE0_UART_SIN_MASK);
	}
	else if (select == GPIO18_USED_AS_UART0SIN)
	{
		ath_reg_rmw_clear(GPIO_IN_ENABLE0_ADDRESS,
				GPIO_IN_ENABLE0_UART_SIN_MASK);

		ath_reg_rmw_set(GPIO_IN_ENABLE0_ADDRESS,
				GPIO_IN_ENABLE0_UART_SIN_SET(0x12));
	}
}

#endif

void ath_gpio_config(void)
{
#if defined(CONFIG_PRODUCT_A9V6)
    //disable JTAG
    ath_reg_rmw_set(GPIO_FUNCTION_ADDRESS, (1 << 1));

    // set GPIO2 and GPIO5 as input
    ath_reg_rmw_set(GPIO_OE_ADDRESS, get_btn_gpio_mask());
    // set GPIO output
    ath_reg_rmw_clear(GPIO_OE_ADDRESS,  get_led_gpio_mask());
		
#if defined(CFG_DOUBLE_BOOT_FACTORY) //only first boot turn on leds
	set_gpio_mux();
    turn_all_led(1);
#else
	turn_all_led(0);
    ath_reg_rmw_clear(GPIO_OUT_ADDRESS, 1 << TP_GPIO_LED_USB); // turn off usb
	ath_reg_rmw_clear(GPIO_OUT_ADDRESS, 1 << TP_GPIO_LED_STATUS);  //turn on  power led
#endif
#endif

#if defined(CONFIG_CUS249)
    /* Turn on System LED GPIO18 for CUS249 */
    ath_reg_rmw_clear(GPIO_OUT_ADDRESS, (1 << 18));
#endif
	/* Turn off JUMPST_LED and 5Gz LED during bootup */
//	ath_reg_rmw_set(GPIO_OE_ADDRESS, (1 << 15));
//	ath_reg_rmw_set(GPIO_OE_ADDRESS, (1 << 12));
}

int
ath_mem_config(void)
{
	unsigned int type, reg32, *tap;
	extern uint32_t *ath_ddr_tap_cal(void);

#if !defined(CONFIG_ATH_EMULATION)

#if !defined(CONFIG_ATH_NAND_BR)
	type = ath_ddr_initial_config(CFG_DDR_REFRESH_VAL);
	tap = ath_ddr_tap_cal();
//	tap = (uint32_t *)0xbd001f10;
//	prmsg("Tap (low, high) = (0x%x, 0x%x)\n", tap[0], tap[1]);

	tap = (uint32_t *)TAP_CONTROL_0_ADDRESS;
	prmsg("Tap values = (0x%x, 0x%x, 0x%x, 0x%x)\n",
		tap[0], tap[1], tap[2], tap[3]);

	/* Take WMAC out of reset */
	reg32 = ath_reg_rd(RST_RESET_ADDRESS);
	reg32 = reg32 & ~RST_RESET_RTC_RESET_SET(1);
	ath_reg_wr_nf(RST_RESET_ADDRESS, reg32);
#endif

#if defined(CONFIG_USB)
	ath_usb1_initial_config();
	ath_usb2_initial_config();
#else
    //turn off not support interface register
    reg32 = ath_reg_rd(RST_RESET_ADDRESS);
    reg32 = reg32 | RST_RESET_USB_PHY_PLL_PWD_EXT_SET(1);
    ath_reg_wr_nf(RST_RESET_ADDRESS, reg32);
    reg32 = ath_reg_rd(RST_CLKGAT_EN_ADDRESS);
    reg32 = reg32 & ~(RST_CLKGAT_EN_PCIE_EP_SET(1) | RST_CLKGAT_EN_PCIE_RC_SET(1) |
            RST_CLKGAT_EN_PCIE_RC2_SET(1) | RST_CLKGAT_EN_CLK100_PCIERC_SET(1) |
            RST_CLKGAT_EN_CLK100_PCIERC2_SET(1) | RST_CLKGAT_EN_USB1_SET(1) |
            RST_CLKGAT_EN_USB2_SET(1));
    ath_reg_wr_nf(RST_CLKGAT_EN_ADDRESS, reg32);
    reg32 = ath_reg_rd(RST_RESET2_ADDRESS);
    reg32 = reg32 | RST_RESET2_USB_PHY2_PLL_PWD_EXT_SET(1);
    ath_reg_wr_nf(RST_RESET2_ADDRESS, reg32);

    ath_reg_wr_nf(BIAS4_ADDRESS, 0x6df6ffe0);
    ath_reg_wr_nf(BIAS5_ADDRESS, 0x7ffffffe);
#endif

	ath_gpio_config();
#endif /* !defined(CONFIG_ATH_EMULATION) */

	return ath_ddr_find_size();
}

long int initdram(int board_type)
{
	return (ath_mem_config());
}

int	checkboard(args)
{
	board_str(CONFIG_BOARD_NAME);
	return 0;
}
