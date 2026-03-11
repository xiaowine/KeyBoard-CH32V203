#ifndef UTILS_H
#define UTILS_H

#include "stdio.h"
#include "ch32v20x.h"

/* UART Printf Definition */
#define DEBUG_UART1 1
#define DEBUG_UART2 2
#define DEBUG_UART3 3

/* DEBUG UATR Definition */
#ifndef DEBUG
#define DEBUG DEBUG_UART1
#endif

/* SDI Printf Definition */
#define SDI_PR_CLOSE 0
#define SDI_PR_OPEN 1

#ifndef SDI_PRINT
#define SDI_PRINT SDI_PR_CLOSE
#endif

void USART_Printf_Init(uint32_t baudrate);
void SDI_Printf_Enable(void);

#if (DEBUG)
#define PRINT(format, ...) printf(format, ##__VA_ARGS__)
#else
#define PRINT(X...)
#endif

#define INTF __attribute__((interrupt("WCH-Interrupt-fast")))
#define RAM __attribute__((section(".ramfunc")))
#define AL4 __attribute__((aligned(4)))
#define CEIL_DIV(x, y) (((x) + (y) - 1) / (y))

uint32_t get_bit_index(uint32_t v);

#endif
