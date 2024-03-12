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

/****************************************************************************/
/**                                                                        **/
/*                           EXPORTED FUNCTIONS                             */
/**                                                                        **/
/****************************************************************************/

// SPI get functions

// return max(uint8_t) on NULL pointer
volatile uint8_t spi_get_tx_queue_depth(const spi_host_t *spi) {
    if (spi == NULL) return -1;
    volatile uint32_t status_reg = spi_get_status(spi);
    return bitfield_read(status_reg, SPI_HOST_STATUS_TXQD_MASK, SPI_HOST_STATUS_TXQD_OFFSET);
}

volatile spi_ch_status_t spi_get_tx_channel_status(const spi_host_t *spi) {
    // TODO: Find a good idea to return error flag if spi == NULL
    volatile uint32_t status_reg = spi_get_status(spi);
    spi_ch_status_t ch_status = {
        .empty = bitfield_read(status_reg, BIT_MASK_1, SPI_HOST_STATUS_TXEMPTY_BIT),
        .full = bitfield_read(status_reg, BIT_MASK_1, SPI_HOST_STATUS_TXFULL_BIT),
        .stall = bitfield_read(status_reg, BIT_MASK_1, SPI_HOST_STATUS_TXSTALL_BIT),
        .wm = bitfield_read(status_reg, BIT_MASK_1, SPI_HOST_STATUS_TXWM_BIT)
    };
    return ch_status;
}

// return max(uint8_t) on NULL pointer
volatile uint8_t spi_get_rx_queue_depth(const spi_host_t *spi) {
    if (spi == NULL) return -1;
    volatile uint32_t status_reg = spi_get_status(spi);
    return bitfield_read(status_reg, SPI_HOST_STATUS_RXQD_MASK, SPI_HOST_STATUS_RXQD_OFFSET);
}

volatile spi_ch_status_t spi_get_rx_channel_status(const spi_host_t *spi) {
    // TODO: Find a good idea to return error flag if spi == NULL
    volatile uint32_t status_reg = spi_get_status(spi);
    spi_ch_status_t ch_status = {
        .empty = bitfield_read(status_reg, BIT_MASK_1, SPI_HOST_STATUS_RXEMPTY_BIT),
        .full = bitfield_read(status_reg, BIT_MASK_1, SPI_HOST_STATUS_RXFULL_BIT),
        .stall = bitfield_read(status_reg, BIT_MASK_1, SPI_HOST_STATUS_RXSTALL_BIT),
        .wm = bitfield_read(status_reg, BIT_MASK_1, SPI_HOST_STATUS_RXWM_BIT)
    };
    return ch_status;
}

// return max(uint32_t) on NULL pointer
volatile uint32_t spi_get_csid(const spi_host_t *spi) {
    if (spi == NULL) return -1;
    return spi_cast_struct(spi)->CSID;
}

// SPI set functions

void spi_sw_reset(const spi_host_t *spi) {
    // TODO: return false if spi == NULL
    if (spi == NULL) return;
    volatile uint32_t ctrl_reg = spi_cast_struct(spi)->CONTROL;
    ctrl_reg = bitfield_write(ctrl_reg, BIT_MASK_1, SPI_HOST_CONTROL_SW_RST_BIT, 1);
    spi_cast_struct(spi)->CONTROL = ctrl_reg;
}

void spi_set_enable(const spi_host_t *spi, bool enable) {
    // TODO: return false if spi == NULL
    if (spi == NULL) return;
    volatile uint32_t ctrl_reg = spi_cast_struct(spi)->CONTROL;
    ctrl_reg = bitfield_write(ctrl_reg, BIT_MASK_1, SPI_HOST_CONTROL_SPIEN_BIT, enable);
    spi_cast_struct(spi)->CONTROL = ctrl_reg;
}

spi_watermark_flags_e spi_set_tx_watermark(const spi_host_t *spi, uint8_t watermark) {
    if (spi == NULL) return SPI_NULL_PTR;
    if (watermark > SPI_HOST_PARAM_TX_DEPTH) return SPI_WATERMARK_EXCEEDS;
    volatile uint32_t ctrl_reg = spi_cast_struct(spi)->CONTROL;
    ctrl_reg = bitfield_write(ctrl_reg, SPI_HOST_CONTROL_TX_WATERMARK_MASK, SPI_HOST_CONTROL_TX_WATERMARK_OFFSET, watermark);
    spi_cast_struct(spi)->CONTROL = ctrl_reg;
    return SPI_WATERMARK_OK;
}

spi_watermark_flags_e spi_set_rx_watermark(const spi_host_t *spi, uint8_t watermark) {
    if (spi == NULL) return SPI_NULL_PTR;
    if (watermark > SPI_HOST_PARAM_RX_DEPTH) return SPI_WATERMARK_EXCEEDS;
    volatile uint32_t ctrl_reg = spi_cast_struct(spi)->CONTROL;
    ctrl_reg = bitfield_write(ctrl_reg, SPI_HOST_CONTROL_RX_WATERMARK_MASK, SPI_HOST_CONTROL_RX_WATERMARK_OFFSET, watermark);
    spi_cast_struct(spi)->CONTROL = ctrl_reg;
    return SPI_WATERMARK_OK;
}

spi_configopts_flags_e spi_set_configopts(const spi_host_t *spi, uint32_t csid, const uint32_t conf_reg) {
    // TODO: check if this could be generalized to more than 2 CSIDs... because right 
    // now not very consistent with spi_set_csid which uses SPI_HOST_PARAM_NUM_C_S
    if (spi == NULL) return SPI_NULL_PTR;
    switch (csid)
    {
    case 0:
        spi_cast_struct(spi)->CONFIGOPTS0 = conf_reg;
        break;
    case 1:
        spi_cast_struct(spi)->CONFIGOPTS1 = conf_reg;
        break;
    default:
        return SPI_CONFIGOPTS_CSID_INVALID;
        break;
    }
    return SPI_CONFIGOPTS_OK;
}

spi_csid_flags_e spi_set_csid(const spi_host_t* spi, uint32_t csid) {
    if (spi == NULL) return SPI_NULL_PTR;
    if (csid >= SPI_HOST_PARAM_NUM_C_S) return SPI_CSID_INVALID;
    spi_cast_struct(spi)->CSID = csid;
    return SPI_CSID_OK;
}

spi_command_flags_e spi_set_command(const spi_host_t *spi, const uint32_t cmd_reg) {
    if (spi == NULL) return SPI_NULL_PTR;
    if (bitfield_read(spi_get_status(spi), SPI_HOST_STATUS_CMDQD_MASK, SPI_HOST_STATUS_CMDQD_OFFSET) >= SPI_HOST_PARAM_CMD_DEPTH) return SPI_COMMAND_QUEUE_FULL;
    // TODO: add a definition for the number 3
    if (bitfield_read(cmd_reg, SPI_HOST_COMMAND_SPEED_MASK, SPI_HOST_COMMAND_SPEED_OFFSET) >= 3) return SPI_COMMAND_SPEED_INVALID;
    spi_cast_struct(spi)->COMMAND = cmd_reg;
    return SPI_COMMAND_OK;
}

spi_read_write_flags_e spi_write_word(const spi_host_t *spi, uint32_t wdata) {
    if (spi == NULL) return SPI_NULL_PTR;
    if (spi_get_tx_queue_depth(spi) >= SPI_HOST_PARAM_TX_DEPTH) return SPI_READ_WRITE_QUEUE_FULL;
    spi_cast_struct(spi)->TXDATA = wdata;
    return SPI_READ_WRITE_OK;
}

spi_read_write_flags_e spi_read_word(const spi_host_t *spi, uint32_t* dst) {
    if (spi == NULL) return SPI_NULL_PTR;
    if (spi_get_rx_queue_depth(spi) == 0) return SPI_READ_WRITE_QUEUE_EMPTY;
    *dst = spi_cast_struct(spi)->RXDATA;
    return SPI_READ_WRITE_OK;
}

void spi_enable_evt_intr(const spi_host_t *spi, bool enable) {
    // TODO: return false if spi == NULL
    if (spi == NULL) return;
    volatile uint32_t intr_enable_reg = spi_cast_struct(spi)->INTR_ENABLE;
    intr_enable_reg = bitfield_write(intr_enable_reg, BIT_MASK_1, SPI_HOST_INTR_ENABLE_SPI_EVENT_BIT, enable);
    spi_cast_struct(spi)->INTR_ENABLE = intr_enable_reg;
}

void spi_enable_error_intr(const spi_host_t *spi, bool enable) {
    // TODO: return false if spi == NULL
    if (spi == NULL) return;
    volatile uint32_t intr_enable_reg = spi_cast_struct(spi)->INTR_ENABLE;
    intr_enable_reg = bitfield_write(intr_enable_reg, BIT_MASK_1, SPI_HOST_INTR_STATE_ERROR_BIT, enable);
    spi_cast_struct(spi)->INTR_ENABLE = intr_enable_reg;
}

void spi_enable_rxwm_intr(const spi_host_t *spi, bool enable) {
    // TODO: return false if spi == NULL
    if (spi == NULL) return;
    volatile uint32_t intr_enable_reg = spi_cast_struct(spi)->EVENT_ENABLE;
    intr_enable_reg = bitfield_write(intr_enable_reg, BIT_MASK_1, SPI_HOST_EVENT_ENABLE_RXWM_BIT, enable);
    spi_cast_struct(spi)->EVENT_ENABLE = intr_enable_reg;
}

void spi_enable_txempty_intr(const spi_host_t *spi, bool enable) {
    // TODO: return false if spi == NULL
    if (spi == NULL) return;
    volatile uint32_t intr_enable_reg = spi_cast_struct(spi)->EVENT_ENABLE;
    intr_enable_reg = bitfield_write(intr_enable_reg, BIT_MASK_1, SPI_HOST_EVENT_ENABLE_TXEMPTY_BIT, enable);
    spi_cast_struct(spi)->EVENT_ENABLE = intr_enable_reg;
}

void spi_output_enable(const spi_host_t *spi, bool enable){
    // TODO: return false if spi == NULL
    if (spi == NULL) return;
    volatile uint32_t output_enable_reg = spi_cast_struct(spi)->CONTROL;
    output_enable_reg = bitfield_write(output_enable_reg, BIT_MASK_1, SPI_HOST_CONTROL_OUTPUT_EN_BIT, enable);
    spi_cast_struct(spi)->CONTROL = output_enable_reg;
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
