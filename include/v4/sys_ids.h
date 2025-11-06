#pragma once

/**
 * @file sys_ids.h
 * @brief SYS instruction ID definitions
 *
 * Defines the ID constants for SYS instruction opcodes.
 *
 * The SYS instruction now supports 32-bit system call IDs (0x00000000 - 0xFFFFFFFF).
 * Legacy 8-bit IDs (0x00-0xFF) are defined below for backward compatibility,
 * but new code should use 32-bit IDs (e.g., 0x0100-0x01FF for LED operations).
 *
 * See docs/sys-opcodes.md for detailed documentation.
 */

/* GPIO operations (0x00 - 0x0F) */
#define V4_SYS_GPIO_INIT 0x00  /**< Initialize GPIO pin */
#define V4_SYS_GPIO_WRITE 0x01 /**< Write to GPIO pin */
#define V4_SYS_GPIO_READ 0x02  /**< Read from GPIO pin */

/* UART operations (0x10 - 0x1F) */
#define V4_SYS_UART_INIT 0x10 /**< Initialize UART port */
#define V4_SYS_UART_PUTC 0x11 /**< Send one character */
#define V4_SYS_UART_GETC 0x12 /**< Receive one character */

/* Timer operations (0x20 - 0x2F) */
#define V4_SYS_MILLIS 0x20   /**< Get milliseconds since startup */
#define V4_SYS_MICROS 0x21   /**< Get microseconds since startup (64-bit) */
#define V4_SYS_DELAY_MS 0x22 /**< Blocking delay in milliseconds */
#define V4_SYS_DELAY_US 0x23 /**< Blocking delay in microseconds */

/* Console I/O operations (0x30 - 0x3F) */
#define V4_SYS_EMIT 0x30 /**< Output one character */
#define V4_SYS_KEY 0x31  /**< Input one character (blocking) */

/* System operations (0xF0 - 0xFF) */
#define V4_SYS_SYSTEM_RESET 0xFE /**< Perform system reset */
#define V4_SYS_SYSTEM_INFO 0xFF  /**< Get system info string */
