/*
 * Jz4750 common routines
 *
 *  Copyright (c) 2006
 *  Ingenic Semiconductor, <jlwei@ingenic.cn>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <config.h>

#if defined(CONFIG_JZ4750) || defined(CONFIG_JZ4750D) || defined(CONFIG_JZ4750L)

#include <common.h>
#include <command.h>
#ifdef CONFIG_JZ4750
#include <asm/jz4750.h>
#elif defined(CONFIG_JZ4750D)
#include <asm/jz4750d.h>
#elif defined(CONFIG_JZ4750L)
#include <asm/jz4750l.h>
#endif

extern void board_early_init(void);

#define MHZ (1000 * 1000)

static inline unsigned int pll_calc_m_n_od(unsigned int speed, unsigned int xtal)
{
	const int pll_m_max = 0x1ff;
	const int pll_n_max = 0x1f;

	int od[] = {1, 2, -1, 4};

	unsigned int plcr_m_n_od = 0;
	unsigned int distance;
	unsigned int tmp, raw;

	int i, j, k;
	int m, n;

	distance = 0xFFFFFFFF;

	for (i = 0; i < sizeof (od) / sizeof(int); i++) {
		/* Limit: 100MHZ <= CLK_OUT * OD <= 500MHZ */
		if (od[i] == -1 
			|| (speed * od[i]) < (100 * MHZ) 
			|| (speed * od[i]) > (500 * MHZ)
			)
			continue;

		for (k = 0; k <= pll_n_max; k++) {
			n = k + 2;
			
			/* Limit: 1MHZ <= XIN/N <= 15MHZ */
			if ((xtal / n) < (1 * MHZ) || (xtal / n) > (15 * MHZ))
				continue;

			for (j = 0; j <= pll_m_max; j++) {
				m = j + 2;

				raw = xtal * m / n;
				tmp = raw / od[i];

				tmp = (tmp > speed) ? (tmp - speed) : (speed - tmp);

				if (tmp < distance) {
					distance = tmp;
					
					plcr_m_n_od = (j << CPM_CPPCR_PLLM_BIT) 
						| (k << CPM_CPPCR_PLLN_BIT)
						| (i << CPM_CPPCR_PLLOD_BIT);

					if (!distance)	/* Match. */
						return plcr_m_n_od;
				}
			}
		}
	}

	return plcr_m_n_od;
}

/* PLL output clock = EXTAL * NF / (NR * NO)
 *
 * NF = FD + 2, NR = RD + 2
 * NO = 1 (if OD = 0), NO = 2 (if OD = 1 or 2), NO = 4 (if OD = 3)
 */
void pll_init(void)
{
	register unsigned int cfcr, plcr1;
	int n2FR[33] = {
		0, 0, 1, 2, 3, 0, 4, 0, 5, 0, 0, 0, 6, 0, 0, 0,
		7, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0,
		9
	};

        /** divisors, 
	 *  for jz4750,  I:H:P:M:L;
	 *  for jz4750d ,I:H0:P:M:H1.
         */
#ifdef CONFIG_JZ4750D
	int div[5] = {1, 3, 3, 3, 2}; /* IPU Field Mode for TVE output works at H0 : H1 = 2 : 3 in JZ4755. */
#else
	int div[5] = {1, 3, 3, 3, 3}; 
#endif	
	int pllout2;

	cfcr = 	CPM_CPCCR_PCS |
		(n2FR[div[0]] << CPM_CPCCR_CDIV_BIT) | 
		(n2FR[div[1]] << CPM_CPCCR_HDIV_BIT) | 
		(n2FR[div[2]] << CPM_CPCCR_PDIV_BIT) |
		(n2FR[div[3]] << CPM_CPCCR_MDIV_BIT) |
#ifdef CONFIG_JZ4750
		(n2FR[div[4]] << CPM_CPCCR_LDIV_BIT);
#else
		(n2FR[div[4]] << CPM_CPCCR_H1DIV_BIT);
#endif

	if (CFG_EXTAL > 16000000)
		cfcr |= CPM_CPCCR_ECS;
	else
		cfcr &= ~CPM_CPCCR_ECS;

	pllout2 = (cfcr & CPM_CPCCR_PCS) ? CFG_CPU_SPEED : (CFG_CPU_SPEED / 2);

#ifdef CONFIG_JZ4750
	/* Init USB Host clock, pllout2 must be n*48MHz */
	REG_CPM_UHCCDR = pllout2 / 48000000 - 1;
#endif
	plcr1 = pll_calc_m_n_od(CFG_CPU_SPEED, CFG_EXTAL);
	plcr1 |= (0x20 << CPM_CPPCR_PLLST_BIT)	/* PLL stable time */
		 | CPM_CPPCR_PLLEN;             /* enable PLL */          

	/* init PLL */
	REG_CPM_CPCCR = cfcr;
	REG_CPM_CPPCR = plcr1;
}

void pll_add_test(int new_freq)
{
	register unsigned int cfcr, plcr1;
	int n2FR[33] = {
		0, 0, 1, 2, 3, 0, 4, 0, 5, 0, 0, 0, 6, 0, 0, 0,
		7, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0,
		9
	};
	int div[5] = {1, 6, 6, 6, 6}; /* divisors of I:S:P:M:L */
	int nf, pllout2;

	cfcr = 	(n2FR[div[0]] << CPM_CPCCR_CDIV_BIT) | 
		(n2FR[div[1]] << CPM_CPCCR_HDIV_BIT) | 
		(n2FR[div[2]] << CPM_CPCCR_PDIV_BIT) |
		(n2FR[div[3]] << CPM_CPCCR_MDIV_BIT) |
#ifdef CONFIG_JZ4750
		(n2FR[div[4]] << CPM_CPCCR_LDIV_BIT);
#else
		(n2FR[div[4]] << CPM_CPCCR_H1DIV_BIT);
#endif

	if (CFG_EXTAL > 16000000)
		cfcr |= CPM_CPCCR_ECS;
	else
		cfcr &= ~CPM_CPCCR_ECS;

	pllout2 = (cfcr & CPM_CPCCR_PCS) ? new_freq : (new_freq / 2);

#ifdef CONFIG_JZ4750
	/* Init UHC clock */
	REG_CPM_UHCCDR = pllout2 / 48000000 - 1;
#endif
	//nf = new_freq * 2 / CFG_EXTAL;
	nf = new_freq / 1000000; //step length is 1M
	plcr1 = ((nf - 2) << CPM_CPPCR_PLLM_BIT) | /* FD */
		(22 << CPM_CPPCR_PLLN_BIT) |	/* RD=0, NR=2 */
		(0 << CPM_CPPCR_PLLOD_BIT) |    /* OD=0, NO=1 */
		(0x20 << CPM_CPPCR_PLLST_BIT) | /* PLL stable time */
		CPM_CPPCR_PLLEN;                /* enable PLL */          

	/* init PLL */
	REG_CPM_CPCCR = cfcr;
	REG_CPM_CPPCR = plcr1;
}

void calc_clocks_add_test(void)
{
	DECLARE_GLOBAL_DATA_PTR;

#ifndef CONFIG_FPGA
	unsigned int pllout;
	unsigned int div[10] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32};

	pllout = __cpm_get_pllout();

	gd->cpu_clk = pllout / div[__cpm_get_cdiv()];
	gd->sys_clk = pllout / div[__cpm_get_hdiv()];
	gd->per_clk = pllout / div[__cpm_get_pdiv()];
	gd->mem_clk = pllout / div[__cpm_get_mdiv()];
	gd->dev_clk = CFG_EXTAL;
#else
	gd->cpu_clk = gd->sys_clk = gd->per_clk = 
		gd->mem_clk = gd->dev_clk = CFG_EXTAL;
#endif
}

void sdram_add_test(int new_freq)
{
	register unsigned int dmcr, sdmode, tmp, cpu_clk, mem_clk, ns;

	unsigned int cas_latency_sdmr[2] = {
		EMC_SDMR_CAS_2,
		EMC_SDMR_CAS_3,
	};

	unsigned int cas_latency_dmcr[2] = {
		1 << EMC_DMCR_TCL_BIT,	/* CAS latency is 2 */
		2 << EMC_DMCR_TCL_BIT	/* CAS latency is 3 */
	};

	int div[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32};

	cpu_clk = new_freq;
	mem_clk = cpu_clk * div[__cpm_get_cdiv()] / div[__cpm_get_mdiv()];

	REG_EMC_RTCSR = EMC_RTCSR_CKS_DISABLE;
	REG_EMC_RTCOR = 0;
	REG_EMC_RTCNT = 0;

	/* Basic DMCR register value. */
	dmcr = ((SDRAM_ROW-11)<<EMC_DMCR_RA_BIT) |
		((SDRAM_COL-8)<<EMC_DMCR_CA_BIT) |
		(SDRAM_BANK4<<EMC_DMCR_BA_BIT) |
		(SDRAM_BW16<<EMC_DMCR_BW_BIT) |
		EMC_DMCR_EPIN |
		cas_latency_dmcr[((SDRAM_CASL == 3) ? 1 : 0)];

	/* SDRAM timimg parameters */
	ns = 1000000000 / mem_clk;

#if 0
	tmp = SDRAM_TRAS/ns;
	if (tmp < 4) tmp = 4;
	if (tmp > 11) tmp = 11;
	dmcr |= ((tmp-4) << EMC_DMCR_TRAS_BIT);

	tmp = SDRAM_RCD/ns;
	if (tmp > 3) tmp = 3;
	dmcr |= (tmp << EMC_DMCR_RCD_BIT);

	tmp = SDRAM_TPC/ns;
	if (tmp > 7) tmp = 7;
	dmcr |= (tmp << EMC_DMCR_TPC_BIT);

	tmp = SDRAM_TRWL/ns;
	if (tmp > 3) tmp = 3;
	dmcr |= (tmp << EMC_DMCR_TRWL_BIT);

	tmp = (SDRAM_TRAS + SDRAM_TPC)/ns;
	if (tmp > 14) tmp = 14;
	dmcr |= (((tmp + 1) >> 1) << EMC_DMCR_TRC_BIT);
#else
	dmcr |= 0xfffc;
#endif

	/* First, precharge phase */
	REG_EMC_DMCR = dmcr;

	/* Set refresh registers */
	tmp = SDRAM_TREF/ns;
	tmp = tmp/64 + 1;
	if (tmp > 0xff) tmp = 0xff;

	REG_EMC_RTCOR = tmp;
	REG_EMC_RTCSR = EMC_RTCSR_CKS_64;	/* Divisor is 64, CKO/64 */

	/* SDRAM mode values */
	sdmode = EMC_SDMR_BT_SEQ | 
		 EMC_SDMR_OM_NORMAL |
		 EMC_SDMR_BL_4 | 
		 cas_latency_sdmr[((SDRAM_CASL == 3) ? 1 : 0)];

	/* precharge all chip-selects */
	REG8(EMC_SDMR0|sdmode) = 0;

	/* wait for precharge, > 200us */
	tmp = (cpu_clk / 1000000) * 200;
	while (tmp--);

	/* enable refresh and set SDRAM mode */
	REG_EMC_DMCR = dmcr | EMC_DMCR_RFSH | EMC_DMCR_MRSET;

	/* write sdram mode register for each chip-select */
	REG8(EMC_SDMR0|sdmode) = 0;

	/* everything is ok now */
}


/* 
 * Add by Wolfgang, 2010-01-03
 * get_ram_size_per_bank() must be called after REG_EMC_DMCR initialized
 */
static inline unsigned int get_ram_size_per_bank(void)
{
	u32 dmcr;
	u32 rows, cols, dw, banks;
	ulong size;

	dmcr = REG_EMC_DMCR;
	rows = 11 + ((dmcr & EMC_DMCR_RA_MASK) >> EMC_DMCR_RA_BIT);
	cols = 8 + ((dmcr & EMC_DMCR_CA_MASK) >> EMC_DMCR_CA_BIT);
	dw = (dmcr & EMC_DMCR_BW) ? 2 : 4;
	banks = (dmcr & EMC_DMCR_BA) ? 4 : 2;

	size = (1 << (rows + cols)) * dw * banks;
//	size *= CONFIG_NR_DRAM_BANKS;

	return size;

}

void sdram_init(void)
{
	register unsigned int dmcr, sdmode, tmp, cpu_clk, mem_clk, ns;

#ifdef CONFIG_MOBILE_SDRAM
	register unsigned int sdemode; /*SDRAM Extended Mode*/
#endif
	unsigned int cas_latency_sdmr[2] = {
		EMC_SDMR_CAS_2,
		EMC_SDMR_CAS_3,
	};

	unsigned int cas_latency_dmcr[2] = {
		1 << EMC_DMCR_TCL_BIT,	/* CAS latency is 2 */
		2 << EMC_DMCR_TCL_BIT	/* CAS latency is 3 */
	};

	int div[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32};

	cpu_clk = CFG_CPU_SPEED;
#if defined(CONFIG_FPGA)
	mem_clk = CFG_EXTAL / CFG_DIV;
#else
	mem_clk = cpu_clk * div[__cpm_get_cdiv()] / div[__cpm_get_mdiv()];
#endif

	REG_EMC_BCR &= ~EMC_BCR_BRE;	/* Disable bus release */
	REG_EMC_RTCSR = 0;	/* Disable clock for counting */

	/* Basic DMCR value */
	dmcr = ((SDRAM_ROW-11)<<EMC_DMCR_RA_BIT) |
		((SDRAM_COL-8)<<EMC_DMCR_CA_BIT) |
		(SDRAM_BANK4<<EMC_DMCR_BA_BIT) |
		(SDRAM_BW16<<EMC_DMCR_BW_BIT) |
		EMC_DMCR_EPIN |
		cas_latency_dmcr[((SDRAM_CASL == 3) ? 1 : 0)];

	/* SDRAM timimg */
	ns = 1000000000 / mem_clk;
	tmp = SDRAM_TRAS/ns;
	if (tmp < 4) tmp = 4;
	if (tmp > 11) tmp = 11;
	dmcr |= ((tmp-4) << EMC_DMCR_TRAS_BIT);
	tmp = SDRAM_RCD/ns;
	if (tmp > 3) tmp = 3;
	dmcr |= (tmp << EMC_DMCR_RCD_BIT);
	tmp = SDRAM_TPC/ns;
	if (tmp > 7) tmp = 7;
	dmcr |= (tmp << EMC_DMCR_TPC_BIT);
	tmp = SDRAM_TRWL/ns;
	if (tmp > 3) tmp = 3;
	dmcr |= (tmp << EMC_DMCR_TRWL_BIT);
	tmp = (SDRAM_TRAS + SDRAM_TPC)/ns;
	if (tmp > 14) tmp = 14;
	dmcr |= (((tmp + 1) >> 1) << EMC_DMCR_TRC_BIT);

	/* SDRAM mode value */
	sdmode = EMC_SDMR_BT_SEQ | 
		 EMC_SDMR_OM_NORMAL |
		 EMC_SDMR_BL_4 | 
		 cas_latency_sdmr[((SDRAM_CASL == 3) ? 1 : 0)];

	/* Stage 1. Precharge all banks by writing SDMR with DMCR.MRSET=0 */
	REG_EMC_DMCR = dmcr;
	REG8(EMC_SDMR0|sdmode) = 0;

	/* Precharge Bank1 SDRAM */
#if CONFIG_NR_DRAM_BANKS == 2   
	REG_EMC_DMCR = dmcr | EMC_DMCR_MBSEL_B1;
	REG8(EMC_SDMR0|sdmode) = 0;
#endif

#ifdef CONFIG_MOBILE_SDRAM
	/* Mobile SDRAM Extended Mode Register */
	sdemode = EMC_SDMR_SET_BA1 | EMC_SDMR_DS_HALF | EMC_SDMR_PRSR_ALL;
#endif

	/* Wait for precharge, > 200us */
	tmp = (cpu_clk / 1000000) * 1000;
	while (tmp--);

	/* Stage 2. Enable auto-refresh */
	REG_EMC_DMCR = dmcr | EMC_DMCR_RFSH;

	tmp = SDRAM_TREF/ns;
	tmp = tmp/64 + 1;
	if (tmp > 0xff) tmp = 0xff;
	REG_EMC_RTCOR = tmp;
	REG_EMC_RTCNT = 0;
	REG_EMC_RTCSR = EMC_RTCSR_CKS_64;	/* Divisor is 64, CKO/64 */

	/* Wait for number of auto-refresh cycles */
	tmp = (cpu_clk / 1000000) * 1000;
	while (tmp--);

 	/* Stage 3. Mode Register Set */
	REG_EMC_DMCR = dmcr | EMC_DMCR_RFSH | EMC_DMCR_MRSET | EMC_DMCR_MBSEL_B0;
	REG8(EMC_SDMR0|sdmode) = 0;


#ifdef CONFIG_MOBILE_SDRAM
	REG8(EMC_SDMR0|sdemode) = 0;   	/* Set Mobile SDRAM Extended Mode Register */
#endif

#if CONFIG_NR_DRAM_BANKS == 2
	REG_EMC_DMCR = dmcr | EMC_DMCR_RFSH | EMC_DMCR_MRSET | EMC_DMCR_MBSEL_B1;
	REG8(EMC_SDMR0|sdmode) = 0;	/* Set Bank1 SDRAM Register */


#ifdef CONFIG_MOBILE_SDRAM
	REG8(EMC_SDMR0|sdemode) = 0;	/* Set Mobile SDRAM Extended Mode Register */
#endif

#endif   /*CONFIG_NR_DRAM_BANKS == 2*/

	/* Set back to basic DMCR value */
	REG_EMC_DMCR = dmcr | EMC_DMCR_RFSH | EMC_DMCR_MRSET;

	/* bank_size: 32M 64M 128M ... */
	unsigned int bank_size = get_ram_size_per_bank();
	unsigned int mem_base0, mem_base1, mem_mask;

	mem_base0 = EMC_MEM_PHY_BASE >> EMC_MEM_PHY_BASE_SHIFT;
	mem_base1 = ((EMC_MEM_PHY_BASE + bank_size) >> EMC_MEM_PHY_BASE_SHIFT);
	mem_mask = EMC_DMAR_MASK_MASK & 
		(~(((bank_size) >> EMC_MEM_PHY_BASE_SHIFT)-1)&EMC_DMAR_MASK_MASK);

	REG_EMC_DMAR0 = (mem_base0 << EMC_DMAR_BASE_BIT) | mem_mask;
	REG_EMC_DMAR1 = (mem_base1 << EMC_DMAR_BASE_BIT) | mem_mask;

	/* everything is ok now */
}


#if !defined(CONFIG_NAND_SPL) && !defined(CONFIG_SPI_SPL) && !defined(CONFIG_MSC_SPL)

static void calc_clocks(void)
{
	DECLARE_GLOBAL_DATA_PTR;

#ifndef CONFIG_FPGA
	unsigned int pllout;
	unsigned int div[10] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32};

	pllout = __cpm_get_pllout();

	gd->cpu_clk = pllout / div[__cpm_get_cdiv()];
	gd->sys_clk = pllout / div[__cpm_get_hdiv()];
	gd->per_clk = pllout / div[__cpm_get_pdiv()];
	gd->mem_clk = pllout / div[__cpm_get_mdiv()];
	gd->dev_clk = CFG_EXTAL;
#else
	gd->cpu_clk = CFG_CPU_SPEED;
	gd->sys_clk = gd->per_clk = gd->mem_clk = gd->dev_clk 
		= CFG_EXTAL / CFG_DIV;
#endif
}

static void rtc_init(void)
{
  return;

	while ( !__rtc_write_ready()) ;
	__rtc_enable_alarm();	/* enable alarm */

	while ( !__rtc_write_ready()) 
		;
	REG_RTC_RGR   = 0x00007fff; /* type value */

	while ( !__rtc_write_ready()) 
		;
	REG_RTC_HWFCR = 0x0000ffe0; /* Power on delay 2s */

	while ( !__rtc_write_ready()) 
		;
	REG_RTC_HRCR  = 0x00000fe0; /* reset delay 125ms */

}

static void led_flush_test(int level)
{
  return;
  #define LCD_BACKLIGHT_PIN 32*4+22

  __gpio_as_output(LCD_BACKLIGHT_PIN);
  __gpio_enable_pull(LCD_BACKLIGHT_PIN);
  int i = 0;
#define FLUSH_TIMES 5
  //for(i = 0; i < FLUSH_TIMES; i++)
  {
    //__gpio_clear_pin(LCD_BACKLIGHT_PIN);
    //delay_1S();
    if(1 == level)
      __gpio_set_pin(LCD_BACKLIGHT_PIN);
    if(0 == level)
      __gpio_clear_pin(LCD_BACKLIGHT_PIN);
    //delay_1S();
  }
}

//----------------------------------------------------------------------
// jz4750 board init routine

void udelay_jz(int uses)
{
  int i  = 0;
  i =uses * 50 ; 
  while(i--)
  {
  __asm__(
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      "nop\n\t"
      );
  }
}
void temp_delay(int i)
{
  return;
  unsigned int h = 0;
  while(i--)
  {
    h = 250;
    while(h--)
    {
      udelay_jz(100);
    }
  }
}


int jz_board_init(void)
{
	__cpm_start_all();
        temp_delay(1);
        led_flush_test(0);  
        temp_delay(1);
        board_early_init();  /* init gpio, pll etc. */
        led_flush_test(1);
        temp_delay(1);
	serial_init();
        //serial_puts("\n\njz_board_init 1\n");
        led_flush_test(0);  
        temp_delay(1);
        led_flush_test(1);
        temp_delay(1);
        temp_delay(1);
        temp_delay(1);
        temp_delay(1);
#ifndef CONFIG_NAND_U_BOOT
#ifndef CONFIG_FPGA
	pll_init();          /* init PLL */
#endif
        led_flush_test(0);  
        int i = 0;
        i = 10;
        while(i--)
          temp_delay(1);
        led_flush_test(1);
        i = 10;
        while(i--)
          temp_delay(1);
        //serial_puts("\n\njz_board_init 2\n");

	sdram_init();        /* init sdram memory */
        //serial_puts("\n\njz_board_init 3\n");

#endif
#if defined CONFIG_MSC_U_BOOT
	//pll_init();          /* init PLL */
#endif
        //serial_puts("\n\njz_board_init 4\n");

	calc_clocks();       /* calc the clocks */
#ifndef CONFIG_FPGA
	rtc_init();		/* init rtc on any reset: */
#endif
	return 0;
}

//----------------------------------------------------------------------
// U-Boot common routines

long int initdram(int board_type)
{
	ulong size;

	size = get_ram_size_per_bank();
	size *= CONFIG_NR_DRAM_BANKS;

	return size;
}

//----------------------------------------------------------------------
// Timer routines

#define TIMER_CHAN  0
#define TIMER_FDATA 0xffff  /* Timer full data value */
#define TIMER_HZ    CFG_HZ

#define READ_TIMER  REG_TCU_TCNT(TIMER_CHAN)  /* macro to read the 16 bit timer */

static ulong timestamp;
static ulong lastdec;

void	reset_timer_masked	(void);
ulong	get_timer_masked	(void);
void	udelay_masked		(unsigned long usec);

/*
 * timer without interrupts
 */

int timer_init(void)
{
	REG_TCU_TCSR(TIMER_CHAN) = TCU_TCSR_PRESCALE256 | TCU_TCSR_EXT_EN;
	REG_TCU_TCNT(TIMER_CHAN) = 0;
	REG_TCU_TDHR(TIMER_CHAN) = 0;
	REG_TCU_TDFR(TIMER_CHAN) = TIMER_FDATA;

	REG_TCU_TMSR = (1 << TIMER_CHAN) | (1 << (TIMER_CHAN + 16)); /* mask irqs */
	REG_TCU_TSCR = (1 << TIMER_CHAN); /* enable timer clock */
	REG_TCU_TESR = (1 << TIMER_CHAN); /* start counting up */

	lastdec = 0;
	timestamp = 0;

	return 0;
}

void reset_timer(void)
{
	reset_timer_masked ();
}

ulong get_timer(ulong base)
{
	return get_timer_masked () - base;
}

void set_timer(ulong t)
{
	timestamp = t;
}

void udelay (unsigned long usec)
{
	ulong tmo,tmp;

	/* normalize */
	if (usec >= 1000) {
		tmo = usec / 1000;
		tmo *= TIMER_HZ;
		tmo /= 1000;
	}
	else {
		if (usec >= 1) {
			tmo = usec * TIMER_HZ;
			tmo /= (1000*1000);
		}
		else
			tmo = 1;
	}

	/* check for rollover during this delay */
	tmp = get_timer (0);
	if ((tmp + tmo) < tmp )
		reset_timer_masked();  /* timer would roll over */
	else
		tmo += tmp;

	while (get_timer_masked () < tmo);
}

void reset_timer_masked (void)
{
	/* reset time */
	lastdec = READ_TIMER;
	timestamp = 0;
}

ulong get_timer_masked (void)
{
	ulong now = READ_TIMER;

	if (lastdec <= now) {
		/* normal mode */
		timestamp += (now - lastdec);
	} else {
		/* we have an overflow ... */
		timestamp += TIMER_FDATA + now - lastdec;
	}
	lastdec = now;

	return timestamp;
}

void udelay_masked (unsigned long usec)
{
	ulong tmo;
	ulong endtime;
	signed long diff;

	/* normalize */
	if (usec >= 1000) {
		tmo = usec / 1000;
		tmo *= TIMER_HZ;
		tmo /= 1000;
	} else {
		if (usec > 1) {
			tmo = usec * TIMER_HZ;
			tmo /= (1000*1000);
		} else {
			tmo = 1;
		}
	}

	endtime = get_timer_masked () + tmo;

	do {
		ulong now = get_timer_masked ();
		diff = endtime - now;
	} while (diff >= 0);
}

/*
 * This function is derived from PowerPC code (read timebase as long long).
 * On MIPS it just returns the timer value.
 */
unsigned long long get_ticks(void)
{
	return get_timer(0);
}

/*
 * This function is derived from PowerPC code (timebase clock frequency).
 * On MIPS it returns the number of timer ticks per second.
 */
ulong get_tbclk (void)
{
	return TIMER_HZ;
}

#endif /* !defined(CONFIG_NAND_SPL) && !defined(CONFIG_SPI_SPL) && !defined(CONFIG_MSC_SPL) */

//---------------------------------------------------------------------
// End of timer routine.
//---------------------------------------------------------------------

#endif /* CONFIG_JZ4750 */
