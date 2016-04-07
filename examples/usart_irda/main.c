/* ----------------------------------------------------------------------------
 *         SAM Software Package License
 * ----------------------------------------------------------------------------
 * Copyright (c) 2016, Atmel Corporation
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaimer below.
 *
 * Atmel's name may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ----------------------------------------------------------------------------
 */

/**
 *  \page irda IrDA Example
 *
 *  \section Purpose
 *  This application gives an example of how to use USART in IrDA mode.
 *
 *  \section Requirements
 *  To run this example 2 kits are needed, one acts as a transmitter and the
 *  other acts as a receiver. And the infrared modules should keep face to face.
 *  This example can be used on SAMA5D2x Xplained board with a Fieldbus shield.
 *  Setting of jumpers on Fieldbus shield board:
 *  - Keep JP24 and JP25 open
 *  - Short 1&2, 3&4, 5&6 of J11
 *  \section Description
 *
 *  \section Usage
 *  -# Build the program and download it inside the evaluation board. Please
 *     refer to the <a href="http://www.atmel.com/dyn/resources/prod_documents/6421B.pdf">SAM-BA User Guide</a>,
 *     the <a href="http://www.atmel.com/dyn/resources/prod_documents/doc6310.pdf">GNU-Based Software Development</a>
 *     application note or to the <a href="http://www.iar.com/website1/1.0.1.0/78/1/">IAR EWARM User and reference guides</a>,
 *     depending on your chosen solution.
 *  -# On the computer, open and configure a terminal application (e.g.
 *     HyperTerminal on Microsoft Windows) with these settings:
 *        - 115200 bauds
 *        - 8 data bits
 *        - No parity
 *        - 1 stop bit
 *        - No flow control
 *  -# Start the application. The following traces shall appear on the terminal:
 *     \code
 *      -- IrDA Example xxx --
 *      -- SAMxxxxx-xx
 *      -- Compiled: xxx xx xxxx xx:xx:xx --
 *      Menu:
 *        t - transmit data throught IrDA
 *        r - receive data from IrDA
 *     \endcode
 *
 *   \section References
 *  - irda/main.c
 *  - pio.h
 *  - usart.h
 *
 */

/** \file
 *
 *  This file contains all the specific code for the IrDA example.
 *
 */


/*------------------------------------------------------------------------------
 *          Headers
 *------------------------------------------------------------------------------*/

#include "board.h"
#include "chip.h"
#include "trace.h"
#include "compiler.h"
#include "timer.h"

#include "peripherals/aic.h"
#include "peripherals/wdt.h"
#include "peripherals/pio.h"

#include "cortex-a/mmu.h"
#include "cortex-a/cp15.h"

#include "peripherals/usartd.h"
#include "peripherals/usart.h"

#include "misc/console.h"

#include "power/act8945a.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/*------------------------------------------------------------------------------
 *         Internal definitions
 *------------------------------------------------------------------------------*/

/** define the peripherals and pins used for IrDA */
#if defined(CONFIG_BOARD_SAMA5D2_XPLAINED)
#define IRDA_USART USART3
#define IRDA_PINS  PINS_FLEXCOM3_USART_IOS3

#elif defined(CONFIG_BOARD_SAMA5D4_XPLAINED)
#define IRDA_USART USART4
#define IRDA_PINS  PINS_USART4

#elif defined(CONFIG_BOARD_SAMA5D4_EK)
#define IRDA_USART USART4
#define IRDA_PINS  PINS_USART4

#else
#error Unsupported SoC!
#endif

/*------------------------------------------------------------------------------
 *         Internal variables
 *------------------------------------------------------------------------------*/

#ifdef CONFIG_HAVE_PMIC_ACT8945A
	struct _pin act8945a_pins[] = ACT8945A_PINS;

	struct _twi_desc act8945a_twid = {
		.addr = ACT8945A_ADDR,
		.freq = ACT8945A_FREQ,
		.transfert_mode = TWID_MODE_POLLING
	};

	struct _act8945a act8945a = {
		.desc = {
			.pin_chglev = ACT8945A_PIN_CHGLEV,
			.pin_irq = ACT8945A_PIN_IRQ,
			.pin_lbo = ACT8945A_PIN_LBO
		}
	};
#endif

/** define pins for IrDA TX & RX */
static const struct _pin pins_irda[] = IRDA_PINS;

/** define descriptor for IrDA */
static struct _usart_desc irda_desc = {
	.addr           = IRDA_USART,
	.baudrate       = 57600,
	.mode           = US_MR_USART_MODE_IRDA | US_MR_CHMODE_NORMAL |
	                  US_MR_PAR_NO | US_MR_CHRL_8_BIT |
	                  US_MR_NBSTOP_1_BIT,
	.transfert_mode = USARTD_MODE_POLLING,
};

/** define receive/transmit status for IrDA */
static bool receiving = false;

/** Transmit buffer. */
static char buffer_tx[] =
"\n\r\
**************************************************************************\n\r\
* This application gives an example of how to use USART in IrDA mode.\n\r\
* The USART features an IrDA mode supplying half-duplex point-to-point \n\r\
* wireless communication. It embeds the modulator and demodulator which \n\r\
* allows a glueless connection to the infrared transceivers. The modulator\n\r\
* and demodulator are compliant with the IrDA specification version 1.1 and\n\r\
* support data transfer speeds ranging from 2.4 kbit/s to 115.2 kbit/s. \n\r\
* \n\r\
* Note that the modulator and the demodulator are activated.\n\r\
**************************************************************************\n\r";

/*------------------------------------------------------------------------------
 *         Internal functions
 *------------------------------------------------------------------------------*/

/**
 *  \brief Update receive/transmit status for IrDA.
 */
static void _update_irda_status(bool is_receiving)
{
	if (is_receiving) {
		printf("\n\rData received from IrDA will be printed bellow...\n\r");
		usart_set_receiver_enabled(IRDA_USART, 1u);
		usart_set_transmitter_enabled(IRDA_USART, 0u);
		pio_clear(&pins_irda[0]);
	} else {
		printf("\n\rTransmitting data throuth IrDA...\n\r");
		usart_set_receiver_enabled(IRDA_USART, 0u);
		usart_set_transmitter_enabled(IRDA_USART, 1u);
	}
}

/**
 *  \brief Handler for DBGU input.
 */
static void console_handler(uint8_t key)
{
	if (key == 't' || key == 'T') {
		_update_irda_status(0);
		receiving = false;
	} else if (key == 'r' || key == 'R') {
		_update_irda_status(1);
		receiving = true;
	}
}

/**
 *  \brief Handler for IrDA
 */
static void irda_irq_handler(void)
{
	if (usart_is_rx_ready(IRDA_USART))
		printf("%c", usart_get_char(IRDA_USART));
}

/**
 *  \brief initialize IrDA interface
 */
static void irda_interface_init(void)
{
	uint32_t id = get_usart_id_from_addr(IRDA_USART);
	pio_configure(&pins_irda[0], ARRAY_SIZE(pins_irda));
	usartd_configure(&irda_desc);
	aic_set_source_vector(id, irda_irq_handler);
	usart_enable_it(IRDA_USART, US_IER_RXRDY);
	usart_set_irda_filter(IRDA_USART, 100);
	aic_enable(id);
}

/*------------------------------------------------------------------------------
 *         Exported functions
 *------------------------------------------------------------------------------*/

/**
 *  \brief IrDA Application entry point.
 *
 *  \return Unused (ANSI-C compatibility).
 */
extern int main( void )
{
	/* Disable watchdog */
	wdt_disable();

	/* Disable all PIO interrupts */
	pio_reset_all_it();

	/* Configure console */
	board_cfg_console();
	console_clear_screen();
	console_reset_cursor();

#ifndef VARIANT_DDRAM
	mmu_initialize();
	cp15_enable_mmu();
	cp15_enable_dcache();
	cp15_enable_icache();
#endif

	printf( "-- IrDA Example %s --\n\r", SOFTPACK_VERSION ) ;
	printf( "-- %s\n\r", BOARD_NAME ) ;
	printf( "-- Compiled: %s %s --\n\r", __DATE__, __TIME__ ) ;

#ifdef CONFIG_HAVE_PMIC_ACT8945A
	pio_configure(act8945a_pins, ARRAY_SIZE(act8945a_pins));
	if (act8945a_configure(&act8945a, &act8945a_twid)) {
		act8945a_set_regulator_voltage(&act8945a, 6, 2500);
		act8945a_enable_regulator(&act8945a, 6, true);
	} else {
		printf("--E-- Error initializing ACT8945A PMIC\n\r");
	}
#endif

	console_set_rx_handler(console_handler);
	console_enable_rx_interrupt();

	printf("Initializing IrDA interface\n\r");
	irda_interface_init();
	printf("IrDA interface initialized.\n\r");

	printf("\n\rMenu:\n\r");
	printf("  t - transmit data throught IrDA\n\r");
	printf("  r - receive data from IrDA\n\r");

	_update_irda_status(receiving);

	while(1){
		if (!receiving) {
			struct _buffer tx = {
				.data = (unsigned char*)buffer_tx,
				.size = sizeof(buffer_tx)
			};
			usartd_transfert(&irda_desc, 0, &tx,
					 usartd_finish_transfert_callback, 0);
			timer_wait(200);
		}
	}
}