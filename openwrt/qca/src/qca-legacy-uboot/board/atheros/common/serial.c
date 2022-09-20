/* 
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 * 
 */

#include <asm/addrspace.h>
#include <asm/types.h>
#include <config.h>
#include <atheros.h>

#if defined(CONFIG_TP_FAST_BOOT)
#include <asm/u-boot.h>
#include <asm/global_data.h>
DECLARE_GLOBAL_DATA_PTR;
#endif

int serial_init(void)
{
//#if !defined(CONFIG_ATH_EMULATION)
	uint32_t div, val;

	div = ath_uart_freq() / (16 * CONFIG_BAUDRATE);
#if defined(CONFIG_SCO_SLAVE_CONNECTED)
	val = ath_reg_rd(GPIO_OE_ADDRESS) & (~0xcbf410u);
#elif defined(CONFIG_MACH_QCA956x)
	val = ath_reg_rd(GPIO_OE_ADDRESS) & 0xbbfdf6;
#else
	val = ath_reg_rd(GPIO_OE_ADDRESS) & (~0xcffc10u);
#endif

#if defined(CONFIG_ATH_SPI_NAND_CS_GPIO)
	val |= 1 << CONFIG_ATH_SPI_NAND_CS_GPIO;
#endif
#if defined(CONFIG_ATH_SPI_CS1_GPIO)
	val |= 1 << CONFIG_ATH_SPI_CS1_GPIO;
#endif
	ath_reg_wr(GPIO_OE_ADDRESS, val);

#ifdef CONFIG_MACH_QCA956x

#if defined(UART_RX20_TX22)

	val = (ath_reg_rd(GPIO_OE_ADDRESS) & (~0x400000));
	ath_reg_wr(GPIO_OE_ADDRESS, val);

	ath_reg_rmw_clear(GPIO_OUT_FUNCTION5_ADDRESS,
			GPIO_OUT_FUNCTION5_ENABLE_GPIO_22_MASK);

	ath_reg_rmw_set(GPIO_OUT_FUNCTION5_ADDRESS,
			GPIO_OUT_FUNCTION5_ENABLE_GPIO_22_SET(0x16));

	ath_reg_rmw_clear(GPIO_IN_ENABLE0_ADDRESS,
			GPIO_IN_ENABLE0_UART_SIN_MASK);
   
	ath_reg_rmw_set(GPIO_IN_ENABLE0_ADDRESS,
			GPIO_IN_ENABLE0_UART_SIN_SET(0x14));
#elif defined(UART_RX18_TX22)
	val = (ath_reg_rd(GPIO_OE_ADDRESS) & (~0x400000)) | 0x40000;
	ath_reg_wr(GPIO_OE_ADDRESS, val);

	ath_reg_rmw_clear(GPIO_OUT_FUNCTION5_ADDRESS,
			GPIO_OUT_FUNCTION5_ENABLE_GPIO_22_MASK);
	ath_reg_rmw_set(GPIO_OUT_FUNCTION5_ADDRESS,
			GPIO_OUT_FUNCTION5_ENABLE_GPIO_22_SET(0x16));
	ath_reg_rmw_clear(GPIO_IN_ENABLE0_ADDRESS,
			GPIO_IN_ENABLE0_UART_SIN_MASK);
  
	ath_reg_rmw_set(GPIO_IN_ENABLE0_ADDRESS,
			GPIO_IN_ENABLE0_UART_SIN_SET(0x12));

#elif defined(UART_RX18_TX20)
	val = (ath_reg_rd(GPIO_OE_ADDRESS) & (~0x100000)) | 0x40000;
	ath_reg_wr(GPIO_OE_ADDRESS, val);

	val = ath_reg_rd(GPIO_OUT_ADDRESS) | 0xeffff6;
	ath_reg_wr(GPIO_OUT_ADDRESS, val);

	ath_reg_rmw_clear(GPIO_OUT_FUNCTION5_ADDRESS,
			GPIO_OUT_FUNCTION5_ENABLE_GPIO_20_MASK);
	ath_reg_rmw_set(GPIO_OUT_FUNCTION5_ADDRESS,
			GPIO_OUT_FUNCTION5_ENABLE_GPIO_20_SET(0x16));
	ath_reg_rmw_clear(GPIO_IN_ENABLE0_ADDRESS,
			GPIO_IN_ENABLE0_UART_SIN_MASK);
  
	ath_reg_rmw_set(GPIO_IN_ENABLE0_ADDRESS,
			GPIO_IN_ENABLE0_UART_SIN_SET(0x12));

#elif defined(UART_RX24_TX20)
	// Turn off LED before XLNA swap to GPO
	val = ath_reg_rd(GPIO_OUT_ADDRESS) | 0xaffff6;
	ath_reg_wr(GPIO_OUT_ADDRESS, val);
	//Switch GPI and GPO and XPA, XLNA
	ath_reg_wr(GPIO_FUNCTION_ADDRESS, 0x8000);

	val = (ath_reg_rd(GPIO_OE_ADDRESS) & (~0x100000)) | 0x1000000;
	ath_reg_wr(GPIO_OE_ADDRESS, val);

	ath_reg_rmw_set(GPIO_OUT_FUNCTION5_ADDRESS,
			GPIO_OUT_FUNCTION5_ENABLE_GPIO_20_SET(0x16));
	ath_reg_rmw_clear(GPIO_IN_ENABLE0_ADDRESS,
			GPIO_IN_ENABLE0_UART_SIN_SET(0xff));

	ath_reg_rmw_set(GPIO_IN_ENABLE0_ADDRESS,
			GPIO_IN_ENABLE0_UART_SIN_SET(0x18));
  
#elif defined(TEST_BOARD_UART)
	//Switch GPI and GPO and XPA1, ANTC
	ath_reg_wr(GPIO_FUNCTION_ADDRESS, 0xc000);

	val = ath_reg_rd(GPIO_OE_ADDRESS) & (~0x2000);
	ath_reg_wr(GPIO_OE_ADDRESS, val);

	ath_reg_rmw_clear(GPIO_OUT_FUNCTION3_ADDRESS,
			GPIO_OUT_FUNCTION3_ENABLE_GPIO_13_MASK);

	ath_reg_rmw_set(GPIO_OUT_FUNCTION3_ADDRESS,
			GPIO_OUT_FUNCTION3_ENABLE_GPIO_13_SET(0x16));
	
	ath_reg_rmw_clear(GPIO_IN_ENABLE0_ADDRESS,
			GPIO_IN_ENABLE0_UART_SIN_SET(0xff));

	ath_reg_rmw_set(GPIO_IN_ENABLE0_ADDRESS,
			GPIO_IN_ENABLE0_UART_SIN_SET(0x17));

#else
	val = (ath_reg_rd(GPIO_OE_ADDRESS) & (~0x100000)) | 0x80000;
	ath_reg_wr(GPIO_OE_ADDRESS, val);

	ath_reg_rmw_set(GPIO_OUT_FUNCTION5_ADDRESS,
			GPIO_OUT_FUNCTION5_ENABLE_GPIO_20_SET(0x16));
	ath_reg_rmw_clear(GPIO_IN_ENABLE0_ADDRESS,
			GPIO_IN_ENABLE0_UART_SIN_SET(0xff));

	ath_reg_rmw_set(GPIO_IN_ENABLE0_ADDRESS,
			GPIO_IN_ENABLE0_UART_SIN_SET(0x13));

#endif

	val = ath_reg_rd(GPIO_OUT_ADDRESS) | 0xaffff6;
	ath_reg_wr(GPIO_OUT_ADDRESS, val);

	val = ath_reg_rd(GPIO_SPARE_ADDRESS);
	ath_reg_wr(GPIO_SPARE_ADDRESS, (val | 0x8402));

#elif CONFIG_MACH_QCN550x
#if defined(UART_RX18_TX19)
	val = (ath_reg_rd(GPIO_OE_ADDRESS) & (~0x080000)) | 0x40000;
	ath_reg_wr(GPIO_OE_ADDRESS, val);

	ath_reg_rmw_clear(GPIO_OUT_FUNCTION4_ADDRESS,
			GPIO_OUT_FUNCTION4_ENABLE_GPIO_19_MASK);
	ath_reg_rmw_set(GPIO_OUT_FUNCTION4_ADDRESS,
			GPIO_OUT_FUNCTION4_ENABLE_GPIO_19_SET(0x16));

#if defined(CONFIG_PRODUCT_A9V6) && defined(CFG_DOUBLE_BOOT_FACTORY)
	ath_reg_rmw_clear(GPIO_IN_ENABLE0_ADDRESS,
			GPIO_IN_ENABLE0_UART_SIN_MASK);

	ath_reg_rmw_set(GPIO_IN_ENABLE0_ADDRESS,
			GPIO_IN_ENABLE0_UART_SIN_SET(0x12));
#endif
#endif
#else
	ath_reg_rmw_set(GPIO_OUT_FUNCTION2_ADDRESS,
			GPIO_OUT_FUNCTION2_ENABLE_GPIO_10_SET(0x16));

	ath_reg_rmw_clear(GPIO_IN_ENABLE0_ADDRESS,
			GPIO_IN_ENABLE0_UART_SIN_SET(0xff));

	ath_reg_rmw_set(GPIO_IN_ENABLE0_ADDRESS,
			GPIO_IN_ENABLE0_UART_SIN_SET(0x9));

	val = ath_reg_rd(GPIO_OUT_ADDRESS) | 0xcffc10u;
	ath_reg_wr(GPIO_OUT_ADDRESS, val);

	val = ath_reg_rd(GPIO_SPARE_ADDRESS);
	ath_reg_wr(GPIO_SPARE_ADDRESS, (val | 0x8402));

	ath_reg_wr(GPIO_OUT_ADDRESS, 0x2f);
#endif
	/*
	 * set DIAB bit
	 */
	ath_uart_wr(OFS_LINE_CONTROL, 0x80);

	/* set divisor */
	ath_uart_wr(OFS_DIVISOR_LSB, (div & 0xff));
	ath_uart_wr(OFS_DIVISOR_MSB, ((div >> 8) & 0xff));

	/* clear DIAB bit*/
	ath_uart_wr(OFS_LINE_CONTROL, 0x00);

	/* set data format */
	ath_uart_wr(OFS_DATA_FORMAT, 0x3);

	ath_uart_wr(OFS_INTR_ENABLE, 0);

//#endif

	return 0;
}

/*
 * Herbert <yuanjianpeng@tp-link.com.cn>
 * I tried FIFO mode but that doesn't work.
 * Here is the solution for NON-FIFO mode.
 * The 16550A UART design is old-fashioned ?
 * Any read of LSR will clear the Data Ready bit
 * So we must record it for later check, or
 * the Data Ready flag is lost!
 */
 
#if defined(CONFIG_TP_FAST_BOOT)
int serial_tstc (void)
{
	if (! gd->lsr_dr_flag && 
		(ath_uart_rd(OFS_LINE_STATUS) & 0x1))
		gd->lsr_dr_flag = 1;
	return gd->lsr_dr_flag;
}

u8 serial_getc(void)
{
	while(!serial_tstc());
	gd->lsr_dr_flag = 0;
	return ath_uart_rd(OFS_RCV_BUFFER);
}

void serial_putc(u8 byte)
{
	if (byte == '\n') serial_putc ('\r');
	while (1) {
		unsigned int lsr = ath_uart_rd(OFS_LINE_STATUS);
		if (lsr & 0x1) 
			gd->lsr_dr_flag = 1;
		if (lsr & 0x20)
			break;
	}
	ath_uart_wr(OFS_SEND_BUFFER, byte);
}
#else
int serial_tstc (void)
{
	return(ath_uart_rd(OFS_LINE_STATUS) & 0x1);
}

u8 serial_getc(void)
{
	while(!serial_tstc());
	
	return ath_uart_rd(OFS_RCV_BUFFER);
}


void serial_putc(u8 byte)
{
	if (byte == '\n') serial_putc ('\r');
	while (((ath_uart_rd(OFS_LINE_STATUS)) & 0x20) == 0x0);
	ath_uart_wr(OFS_SEND_BUFFER, byte);
}
#endif

void serial_setbrg (void)
{
}

void serial_puts (const char *s)
{
	while (*s)
	{
		serial_putc (*s++);
	}
}
