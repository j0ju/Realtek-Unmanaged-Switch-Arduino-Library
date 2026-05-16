/*
 * smi.h — public interface for the SMI layer.
 *
 * The original Arduino library did not expose smi_init/smi_close because
 * initialisation happened implicitly via Arduino's Wire library.  The
 * Linux userland replacement (smi_linux.c) needs explicit init/close, so
 * add those declarations here.
 *
 * All other declarations (smi_read, smi_write) are kept as-is from the
 * upstream file; only this preamble block is added.
 */

#ifndef __SMI_H__
#define __SMI_H__

#include "rtk_types.h"
#include "rtk_error.h"

#define MDC_MDIO_CTRL0_REG 31
#define MDC_MDIO_START_REG 29
#define MDC_MDIO_CTRL1_REG 21
#define MDC_MDIO_ADDRESS_REG 23
#define MDC_MDIO_DATA_WRITE_REG 24
#define MDC_MDIO_DATA_READ_REG 25
#define MDC_MDIO_PREAMBLE_LEN 32

#define MDC_MDIO_START_OP 0xFFFF
#define MDC_MDIO_ADDR_OP 0x000E
#define MDC_MDIO_READ_OP 0x0001
#define MDC_MDIO_WRITE_OP 0x0003

#define SPI_READ_OP 0x3
#define SPI_WRITE_OP 0x2
#define SPI_READ_OP_LEN 0x8
#define SPI_WRITE_OP_LEN 0x8
#define SPI_REG_LEN 16
#define SPI_DATA_LEN 16

#define GPIO_DIR_IN 1
#define GPIO_DIR_OUT 0

#define ack_timer 10

rtk_int32 smi_read(rtk_uint32 mAddrs, rtk_uint32 *rData);
rtk_int32 smi_write(rtk_uint32 mAddrs, rtk_uint32 rData);
extern rtk_int32 smi_init(const char *dev, unsigned short addr);
extern void      smi_close(void);


#endif /* __SMI_H__ */
