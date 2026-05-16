/*
 * smi_linux.c — Linux userland replacement for smi.c
 *
 * Implements the two public functions declared in smi.h:
 *
 *   rtk_int32 smi_read (rtk_uint32 mAddrs, rtk_uint32 *rData)
 *   rtk_int32 smi_write(rtk_uint32 mAddrs, rtk_uint32  rData)
 *
 * ...using the Linux /dev/i2c-N interface with I2C_RDWR ioctl.
 *
 * The Realtek SMI "read" transfer requires the adapter to support
 * I2C_FUNC_NOSTART so that the 2-byte register address can be inserted
 * after the START+address byte but before the bus turnaround, without
 * the device seeing a repeated START.  This matches the wire sequence
 * produced by the Allwinner TWI_EFR (Enhance Feature Register) used in
 * the Turing Pi BMC kernel patches.
 *
 * Compile-time defaults (override via -D on the command line or Makefile):
 *   RTK_I2C_ADDR   7-bit I2C device address  (default 0x58)
 *   RTK_I2C_DEV    /dev/i2c-N path            (default "/dev/i2c-0")
 *
 * Usage:
 *   Call smi_init() once before any smi_read/smi_write calls.
 *   Call smi_close() when done.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include "rtk_types.h"
#include "rtk_error.h"
#include "smi.h"

/* ---- Compile-time defaults ------------------------------------------------ */

#ifndef RTK_I2C_ADDR
#define RTK_I2C_ADDR  0x58
#endif

#ifndef RTK_I2C_DEV
#define RTK_I2C_DEV   "/dev/i2c-0"
#endif

/* ---- Module state --------------------------------------------------------- */

static int   g_fd   = -1;
static __u16 g_addr = RTK_I2C_ADDR;

/* ---- Public init/close ---------------------------------------------------- */

/**
 * smi_init - Open the I2C bus and verify NOSTART capability.
 *
 * @dev:   Path to the I2C adapter (e.g. "/dev/i2c-1").
 *         Pass NULL to use the compile-time default RTK_I2C_DEV.
 * @addr:  7-bit I2C address of the Realtek switch.
 *         Pass 0 to use the compile-time default RTK_I2C_ADDR.
 *
 * Returns RT_ERR_OK on success, RT_ERR_FAILED on any error.
 * Prints a descriptive message to stderr on failure.
 */
rtk_int32 smi_init(const char *dev, __u16 addr)
{
    unsigned long funcs;

    if (g_fd >= 0) {
        /* Already open — close and reopen if caller changed device */
        close(g_fd);
        g_fd = -1;
    }

    g_addr = addr ? addr : (__u16)RTK_I2C_ADDR;

    g_fd = open(dev ? dev : RTK_I2C_DEV, O_RDWR);
    if (g_fd < 0) {
        fprintf(stderr, "smi_init: cannot open %s: %s\n",
                dev ? dev : RTK_I2C_DEV, strerror(errno));
        return RT_ERR_FAILED;
    }

    /* Query adapter capabilities */
    if (ioctl(g_fd, I2C_FUNCS, &funcs) < 0) {
        fprintf(stderr, "smi_init: I2C_FUNCS ioctl failed: %s\n",
                strerror(errno));
        close(g_fd);
        g_fd = -1;
        return RT_ERR_FAILED;
    }

    if (!(funcs & I2C_FUNC_NOSTART)) {
        fprintf(stderr,
                "smi_init: adapter does not support I2C_FUNC_NOSTART "
                "(required for Realtek SMI reads).\n"
                "Use an adapter with EFR support, or i2c-gpio bit-bang.\n");
        close(g_fd);
        g_fd = -1;
        return RT_ERR_FAILED;
    }

    return RT_ERR_OK;
}

/**
 * smi_close - Release the I2C file descriptor.
 */
void smi_close(void)
{
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
}

/* ---- Internal helper ------------------------------------------------------ */

static inline int _check_open(void)
{
    if (g_fd < 0) {
        fprintf(stderr, "smi: not initialised — call smi_init() first\n");
        return 0;
    }
    return 1;
}

/* ---- smi_read ------------------------------------------------------------- */

/*
 * Wire sequence for a Realtek SMI read:
 *
 *   START [addr|R] [reg_lo] [reg_hi] ↩ [data_lo] [data_hi] STOP
 *            ↑                 ↑
 *   msg[0]: zero-length read   msg[1]: NOSTART write of register address
 *                              msg[2]: NOSTART read of 2 data bytes
 *
 * The adapter's EFR (or equivalent) inserts msg[1] between the address
 * byte and the bus turnaround, so the switch sees a single coherent
 * transaction rather than a repeated-START sequence.
 */
rtk_int32 smi_read(rtk_uint32 mAddrs, rtk_uint32 *rData)
{
    uint8_t reg[2];
    uint8_t val[2] = {0, 0};

    struct i2c_msg msgs[3];
    struct i2c_rdwr_ioctl_data xfer;
    int rc;

    if (mAddrs > 0xFFFF)    return RT_ERR_INPUT;
    if (!rData)             return RT_ERR_NULL_POINTER;
    if (!_check_open())     return RT_ERR_FAILED;

    /* Register address, little-endian (matches Realtek SMI byte order) */
    reg[0] = (uint8_t)(mAddrs & 0xff);
    reg[1] = (uint8_t)((mAddrs >> 8) & 0xff);

    /*
     * Message 0: START + address byte only.
     * A zero-length I2C_M_RD puts the START and address on the wire
     * without sending any payload or turning the bus around yet.
     */
    msgs[0].addr  = g_addr;
    msgs[0].flags = I2C_M_RD;
    msgs[0].len   = 0;
    msgs[0].buf   = NULL;

    /*
     * Message 1: the 2-byte register address, no repeated START.
     * The adapter inserts these bytes immediately after msg[0]'s address
     * byte using the EFR / NOSTART capability.
     */
    msgs[1].addr  = g_addr;
    msgs[1].flags = I2C_M_NOSTART;
    msgs[1].len   = sizeof(reg);
    msgs[1].buf   = reg;

    /*
     * Message 2: read 2 data bytes, still no START.
     * This is the actual bus turnaround; the switch drives the data lines.
     */
    msgs[2].addr  = g_addr;
    msgs[2].flags = I2C_M_RD | I2C_M_NOSTART;
    msgs[2].len   = sizeof(val);
    msgs[2].buf   = val;

    xfer.msgs  = msgs;
    xfer.nmsgs = 3;

    rc = ioctl(g_fd, I2C_RDWR, &xfer);
    if (rc != 3) {
        if (rc < 0)
            fprintf(stderr, "smi_read(0x%04x): I2C_RDWR failed: %s\n",
                    mAddrs, strerror(errno));
        else
            fprintf(stderr, "smi_read(0x%04x): short transfer (%d/3)\n",
                    mAddrs, rc);
        return RT_ERR_FAILED;
    }

    /* Reassemble 16-bit value from two little-endian bytes */
    *rData = (rtk_uint32)val[0] | ((rtk_uint32)val[1] << 8);
    return RT_ERR_OK;
}

/* ---- smi_write ------------------------------------------------------------ */

/*
 * Wire sequence for a Realtek SMI write:
 *
 *   START [addr|W] [reg_lo] [reg_hi] [data_lo] [data_hi] STOP
 *
 * This is a plain I2C write — no NOSTART needed.
 * The I2C controller automatically prepends the address byte.
 */
rtk_int32 smi_write(rtk_uint32 mAddrs, rtk_uint32 rData)
{
    uint8_t buf[4];

    struct i2c_msg msg;
    struct i2c_rdwr_ioctl_data xfer;
    int rc;

    if (mAddrs > 0xFFFF)    return RT_ERR_INPUT;
    if (rData  > 0xFFFF)    return RT_ERR_INPUT;
    if (!_check_open())     return RT_ERR_FAILED;

    /* [reg_lo][reg_hi][data_lo][data_hi] — all little-endian */
    buf[0] = (uint8_t)(mAddrs & 0xff);
    buf[1] = (uint8_t)((mAddrs >> 8) & 0xff);
    buf[2] = (uint8_t)(rData  & 0xff);
    buf[3] = (uint8_t)((rData  >> 8) & 0xff);

    msg.addr  = g_addr;
    msg.flags = 0;              /* plain write */
    msg.len   = sizeof(buf);
    msg.buf   = buf;

    xfer.msgs  = &msg;
    xfer.nmsgs = 1;

    rc = ioctl(g_fd, I2C_RDWR, &xfer);
    if (rc != 1) {
        if (rc < 0)
            fprintf(stderr, "smi_write(0x%04x, 0x%04x): I2C_RDWR failed: %s\n",
                    mAddrs, rData, strerror(errno));
        else
            fprintf(stderr, "smi_write(0x%04x, 0x%04x): short transfer (%d/1)\n",
                    mAddrs, rData, rc);
        return RT_ERR_FAILED;
    }

    return RT_ERR_OK;
}

