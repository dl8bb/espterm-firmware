#include <esp8266.h>
#include "uart_driver.h"
#include "uart_handler.h"
#include "ansi_parser.h"
#include "syscfg.h"

static char logbuf[DEBUG_LOGBUF_SIZE];
static u32 lb_nw = 1;
static u32 lb_ls = 0;
static ETSTimer flushLogTimer;

static void buf_putc(char c)
{
    if (sysconf->gpio2_conf == GPIOCONF_OFF) {
        if (lb_ls != lb_nw) {
            logbuf[lb_nw++] = c;
            if (lb_nw >= DEBUG_LOGBUF_SIZE) lb_nw = 0;
        }
    }
}

static void ICACHE_FLASH_ATTR
buf_pop(void *unused)
{
	u32 quantity = 16;
	u32 old_ls;
	while (quantity > 0) {
		// stop when done
		if ((lb_ls == lb_nw-1) || (lb_ls == DEBUG_LOGBUF_SIZE-1 && lb_nw == 0)) break;

		old_ls = lb_ls;
		lb_ls++;
		if (lb_ls >= DEBUG_LOGBUF_SIZE) lb_ls = 0;

		if (OK == UART_WriteCharCRLF(UART1, logbuf[lb_ls], 5)) {
			quantity--;
		} else {
			// try another time
			lb_ls = old_ls;
			break;
		}
	}
}

LOCAL void my_putc(char c)
{
	if (sysconf->gpio2_conf == GPIOCONF_OFF) {
		UART_WriteCharCRLF(UART1, (u8) c, 200);
	}
}

/**
 * Init the serial ports
 */
void ICACHE_FLASH_ATTR serialInitBase(void)
{
	UART_Init();
	// debug port
	UART_SetParity(UART1, PARITY_NONE);
	UART_SetStopBits(UART1, ONE_STOP_BIT);
	UART_SetBaudrate(UART1, BIT_RATE_115200);
	UART_SetPrintPort(UART1);
#if ASYNC_LOG
	os_install_putc1(buf_putc);
#else
	os_install_putc1(my_putc);
#endif
	UART_SetupAsyncReceiver();

	// 1 ms timer
	os_timer_disarm(&flushLogTimer);
	os_timer_setfn(&flushLogTimer, buf_pop, NULL);
	//os_timer_arm(&flushLogTimer, 1, 1);
	ets_timer_arm_new(&flushLogTimer, 1, 500, 0); // us timer
}

/**
 * Init the serial ports
 */
void ICACHE_FLASH_ATTR serialInit(void)
{
	UART_SetParity(UART0, (UartParityMode) sysconf->uart_parity);
	UART_SetStopBits(UART0, (UartStopBitsNum) sysconf->uart_stopbits);
	UART_SetBaudrate(UART0, sysconf->uart_baudrate);

	// GPIO2 may be used as a remotely controlled GPIO
	if (sysconf->gpio2_conf == GPIOCONF_OFF) {
		PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_U1TXD_BK);
	} else {
		PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
		// further config of the GPIO will be done in persist.c after calling this func
	}

	info("COM SERIAL: %d baud, %s parity, %s stopbit(s)",
		 sysconf->uart_baudrate,
		 (sysconf->uart_parity == PARITY_NONE ? "NONE" : (sysconf->uart_parity == PARITY_ODD ? "ODD" : "EVEN")),
		 (sysconf->uart_stopbits == ONE_STOP_BIT ? "1" : (sysconf->uart_stopbits == ONE_HALF_STOP_BIT ? "1.5" : "2"))
	);
}

/**
 * Handle a byte received from UART.
 * Might do some buffering here maybe
 *
 * @param c
 */
void ICACHE_FLASH_ATTR UART_HandleRxByte(char c)
{
	ansi_parser(c);
	system_soft_wdt_feed(); // so we survive long torrents
}
