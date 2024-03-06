/*
                              *******************
******************************* C SOURCE FILE *****************************
**                            *******************
**
** project  : X-HEEP
** filename : spi_host.c
** version  : 1
** date     : 06/03/24
**
***************************************************************************
**
** Copyright (c) EPFL contributors.
** All rights reserved.
**
***************************************************************************
*/

/***************************************************************************/
/***************************************************************************/
/**
* @file   spi_host.c
* @date   06/03/24
* @brief  The Serial Peripheral Interface (SPI) driver to set up and use the
* SPI peripheral
*/

/****************************************************************************/
/**                                                                        **/
/*                             MODULES USED                                 */
/**                                                                        **/
/****************************************************************************/

#include "spi_host.h"

#include "mmio.h"
#include "bitfield.h"

/****************************************************************************/
/**                                                                        **/
/*                        DEFINITIONS AND MACROS                            */
/**                                                                        **/
/****************************************************************************/

/****************************************************************************/
/**                                                                        **/
/*                        TYPEDEFS AND STRUCTURES                           */
/**                                                                        **/
/****************************************************************************/

/****************************************************************************/
/**                                                                        **/
/*                      PROTOTYPES OF LOCAL FUNCTIONS                       */
/**                                                                        **/
/****************************************************************************/

/****************************************************************************/
/**                                                                        **/
/*                           EXPORTED VARIABLES                             */
/**                                                                        **/
/****************************************************************************/

/****************************************************************************/
/**                                                                        **/
/*                            GLOBAL VARIABLES                              */
/**                                                                        **/
/****************************************************************************/

/**
 * Control Block (CB) of the SPI peripheral.
 * Has variables and constant necessary/useful for its control.
 */
static struct
{
    /**
    * Flag to lower as soon as a transaction is launched, and raised by the
    * interrupt handler once it has finished. Only used when the end event is
    * set to INTR_WAIT.
    */
    uint8_t intrFlag;

    /**
     * memory mapped structure of an SPI.
     */
    spi_host* peri;

} spi_cb;

/****************************************************************************/
/**                                                                        **/
/*                           EXPORTED FUNCTIONS                             */
/**                                                                        **/
/****************************************************************************/

// SPI get functions

volatile uint8_t spi_get_tx_queue_depth(const spi_host_t *spi) {
    volatile uint32_t status_reg = spi_get_status(spi);
    return bitfield_field32_read(status_reg, SPI_HOST_STATUS_TXQD_FIELD);
}

volatile spi_ch_status_t spi_get_tx_channel_status(const spi_host_t *spi) {
    volatile uint32_t status_reg = spi_get_status(spi);
    spi_ch_status_t ch_status = {
        .empty = bitfield_bit32_read(status_reg, SPI_HOST_STATUS_TXEMPTY_BIT),
        .full = bitfield_bit32_read(status_reg, SPI_HOST_STATUS_TXFULL_BIT),
        .stall = bitfield_bit32_read(status_reg, SPI_HOST_STATUS_TXSTALL_BIT),
        .wm = bitfield_bit32_read(status_reg, SPI_HOST_STATUS_TXWM_BIT)
    };
    return ch_status;
}

volatile uint8_t spi_get_rx_queue_depth(const spi_host_t *spi) {
    volatile uint32_t status_reg = spi_get_status(spi);
    return bitfield_field32_read(status_reg, SPI_HOST_STATUS_RXQD_FIELD);
}

volatile spi_ch_status_t spi_get_rx_channel_status(const spi_host_t *spi) {
    volatile uint32_t status_reg = spi_get_status(spi);
    spi_ch_status_t ch_status = {
        .empty = bitfield_bit32_read(status_reg, SPI_HOST_STATUS_RXEMPTY_BIT),
        .full = bitfield_bit32_read(status_reg, SPI_HOST_STATUS_RXFULL_BIT),
        .stall = bitfield_bit32_read(status_reg, SPI_HOST_STATUS_RXSTALL_BIT),
        .wm = bitfield_bit32_read(status_reg, SPI_HOST_STATUS_RXWM_BIT)
    };
    return ch_status;
}

volatile uint32_t spi_get_csid(const spi_host_t *spi) {
    // return mmio_region_read32(spi->base_addr, SPI_HOST_CSID_REG_OFFSET);
    return spi_host_peri->CSID;
}

// SPI set functions

void spi_sw_reset(const spi_host_t *spi) {
    // volatile uint32_t ctrl_reg = mmio_region_read32(spi->base_addr, SPI_HOST_CONTROL_REG_OFFSET);
    volatile uint32_t ctrl_reg = spi_host_peri->CONTROL;
    ctrl_reg = bitfield_bit32_write(ctrl_reg, SPI_HOST_CONTROL_SW_RST_BIT, 1);
    // mmio_region_write32(spi->base_addr, SPI_HOST_CONTROL_REG_OFFSET, ctrl_reg);
    spi_host_peri->CONTROL = ctrl_reg;
}

void spi_set_enable(const spi_host_t *spi, bool enable) {
    // volatile uint32_t ctrl_reg = mmio_region_read32(spi->base_addr, SPI_HOST_CONTROL_REG_OFFSET);
    volatile uint32_t ctrl_reg = spi_host_peri->CONTROL;
    ctrl_reg = bitfield_bit32_write(ctrl_reg, SPI_HOST_CONTROL_SPIEN_BIT, enable);
    // mmio_region_write32(spi->base_addr, SPI_HOST_CONTROL_REG_OFFSET, ctrl_reg);
    spi_host_peri->CONTROL = ctrl_reg;
}

void spi_set_tx_watermark(const spi_host_t *spi, uint8_t watermark) {
    // volatile uint32_t ctrl_reg = mmio_region_read32(spi->base_addr, SPI_HOST_CONTROL_REG_OFFSET);
    volatile uint32_t ctrl_reg = spi_host_peri->CONTROL;
    ctrl_reg = bitfield_field32_write(ctrl_reg, SPI_HOST_CONTROL_TX_WATERMARK_FIELD, watermark);
    // mmio_region_write32(spi->base_addr, SPI_HOST_CONTROL_REG_OFFSET, ctrl_reg);
    spi_host_peri->CONTROL = ctrl_reg;
}

void spi_set_rx_watermark(const spi_host_t *spi, uint8_t watermark) {
    // volatile uint32_t ctrl_reg = mmio_region_read32(spi->base_addr, SPI_HOST_CONTROL_REG_OFFSET);
    volatile uint32_t ctrl_reg = spi_host_peri->CONTROL;
    ctrl_reg = bitfield_field32_write(ctrl_reg, SPI_HOST_CONTROL_RX_WATERMARK_FIELD, watermark);
    // mmio_region_write32(spi->base_addr, SPI_HOST_CONTROL_REG_OFFSET, ctrl_reg);
    spi_host_peri->CONTROL = ctrl_reg;
}

void spi_set_configopts(const spi_host_t *spi, uint32_t csid, const uint32_t conf_reg) {
    // mmio_region_write32(spi->base_addr, sizeof(uint32_t) * csid + SPI_HOST_CONFIGOPTS_0_REG_OFFSET, conf_reg);
    switch (csid)
    {
    case 0:
        spi_host_peri->CONFIGOPTS0 = conf_reg;
        break;
    case 1:
        spi_host_peri->CONFIGOPTS1 = conf_reg;
        break;
    default:
        break;
    }
}

void spi_set_csid(const spi_host_t* spi, uint32_t csid) {
    // mmio_region_write32(spi->base_addr, SPI_HOST_CSID_REG_OFFSET, csid);
    spi_host_peri->CSID = csid;
}

void spi_set_command(const spi_host_t *spi, const uint32_t cmd_reg) {
    // mmio_region_write32(spi->base_addr, SPI_HOST_COMMAND_REG_OFFSET, cmd_reg);
    spi_host_peri->COMMAND = cmd_reg;
}

void spi_write_word(const spi_host_t *spi, uint32_t wdata) {
    // mmio_region_write32(spi->base_addr, SPI_HOST_TXDATA_REG_OFFSET, wdata);
    spi_host_peri->TXDATA = wdata;
}

void spi_read_word(const spi_host_t *spi, uint32_t* dst) {
    // *dst = mmio_region_read32(spi->base_addr, SPI_HOST_RXDATA_REG_OFFSET);
    *dst = spi_host_peri->RXDATA;
}

void spi_enable_evt_intr(const spi_host_t *spi, bool enable) {
    // volatile uint32_t intr_enable_reg = mmio_region_read32(spi->base_addr, SPI_HOST_INTR_ENABLE_REG_OFFSET);
    volatile uint32_t intr_enable_reg = spi_host_peri->INTR_ENABLE;
    intr_enable_reg = bitfield_bit32_write(intr_enable_reg, SPI_HOST_INTR_ENABLE_SPI_EVENT_BIT, enable);
    // mmio_region_write32(spi->base_addr, SPI_HOST_INTR_ENABLE_REG_OFFSET, intr_enable_reg);
    spi_host_peri->INTR_ENABLE = intr_enable_reg;
}

void spi_enable_error_intr(const spi_host_t *spi, bool enable) {
    // volatile uint32_t intr_enable_reg = mmio_region_read32(spi->base_addr, SPI_HOST_INTR_ENABLE_REG_OFFSET);
    volatile uint32_t intr_enable_reg = spi_host_peri->INTR_ENABLE;
    intr_enable_reg = bitfield_bit32_write(intr_enable_reg, SPI_HOST_INTR_STATE_ERROR_BIT, enable);
    // mmio_region_write32(spi->base_addr, SPI_HOST_INTR_ENABLE_REG_OFFSET, intr_enable_reg);
    spi_host_peri->INTR_ENABLE = intr_enable_reg;
}

void spi_enable_rxwm_intr(const spi_host_t *spi, bool enable) {
    // volatile uint32_t intr_enable_reg = mmio_region_read32(spi->base_addr, SPI_HOST_EVENT_ENABLE_REG_OFFSET);
    volatile uint32_t intr_enable_reg = spi_host_peri->EVENT_ENABLE;
    intr_enable_reg = bitfield_bit32_write(intr_enable_reg, SPI_HOST_EVENT_ENABLE_RXWM_BIT, enable);
    // mmio_region_write32(spi->base_addr, SPI_HOST_EVENT_ENABLE_REG_OFFSET, intr_enable_reg);
    spi_host_peri->EVENT_ENABLE = intr_enable_reg;
}

void spi_enable_txempty_intr(const spi_host_t *spi, bool enable) {
    // volatile uint32_t intr_enable_reg = mmio_region_read32(spi->base_addr, SPI_HOST_EVENT_ENABLE_REG_OFFSET);
    volatile uint32_t intr_enable_reg = spi_host_peri->EVENT_ENABLE;
    intr_enable_reg = bitfield_bit32_write(intr_enable_reg, SPI_HOST_EVENT_ENABLE_TXEMPTY_BIT, enable);
    // mmio_region_write32(spi->base_addr, SPI_HOST_EVENT_ENABLE_REG_OFFSET, intr_enable_reg);
    spi_host_peri->EVENT_ENABLE = intr_enable_reg;
}

void spi_output_enable(const spi_host_t *spi, bool enable){
    // volatile uint32_t output_enable_reg = mmio_region_read32(spi->base_addr, SPI_HOST_CONTROL_REG_OFFSET);
    volatile uint32_t output_enable_reg = spi_host_peri->CONTROL;
    output_enable_reg = bitfield_bit32_write(output_enable_reg, SPI_HOST_CONTROL_OUTPUT_EN_BIT, enable);
    // mmio_region_write32(spi->base_addr, SPI_HOST_CONTROL_REG_OFFSET, output_enable_reg);
    spi_host_peri->CONTROL = output_enable_reg;
}

__attribute__((weak, optimize("O0"))) void handler_irq_spi(uint32_t id)
{
 // Replace this function with a non-weak implementation
}

/****************************************************************************/
/**                                                                        **/
/*                            LOCAL FUNCTIONS                               */
/**                                                                        **/
/****************************************************************************/

/****************************************************************************/
/**                                                                        **/
/*                                 EOF                                      */
/**                                                                        **/
/****************************************************************************/
