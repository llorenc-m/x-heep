/*
                              *******************
******************************* C SOURCE FILE *****************************
**                            *******************
**
** project  : X-HEEP
** filename : spi_sdk.c
** version  : 1
** date     : 18/04/24
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
* @file   spi_sdk.c
* @date   18/04/24
* @author Llorenç Muela
* @brief  The Serial Peripheral Interface (SPI) SDK to set up and use the
* SPI peripheral
*/

/****************************************************************************/
/**                                                                        **/
/**                            MODULES USED                                **/
/**                                                                        **/
/****************************************************************************/

#include "spi_sdk.h"
#include "spi_host.h"
#include "spi_host_regs.h"
#include "soc_ctrl_structs.h"
#include "bitfield.h"
#include "fast_intr_ctrl.h"
#include "hart.h"

/****************************************************************************/
/**                                                                        **/
/**                       DEFINITIONS AND MACROS                           **/
/**                                                                        **/
/****************************************************************************/

#define DATA_MODE_CPOL_OFFS 1
#define DATA_MODE_CPHA_OFFS 0

#define SYS_FREQ      (soc_ctrl_peri->SYSTEM_FREQUENCY_HZ)
#define SPI_MIN_FREQ  SYS_FREQ / (2 * 65535 + 2)            // 65535 = 2^16 - 1

// Maximum length of data (in bytes) for a command segment (currently 2^24)
#define MAX_COMMAND_LENGTH SPI_HOST_COMMAND_LEN_MASK
#define BYTES_PER_WORD     4
// Convert byte count to word count
#define LEN_WORDS(bytes)   ((bytes / BYTES_PER_WORD) + (bytes % BYTES_PER_WORD ? 1 : 0))

// The standard watermark for all transactions (seems reasonable)
#define TX_WATERMARK (SPI_HOST_PARAM_TX_DEPTH / 4)  // Arbirarily chosen
#define RX_WATERMARK (SPI_HOST_PARAM_RX_DEPTH - 12) // Arbirarily chosen

#define NULL_CALLBACKS (spi_callbacks_t) {NULL, NULL, NULL, NULL}

// SPI peripheral busy checks
#define SPI_BUSY(peri)     peri.state == SPI_STATE_BUSY
#define SPI_NOT_BUSY(peri) peri.state != SPI_STATE_BUSY

// Check command length validity
#define SPI_INVALID_LEN(len) (len == 0 || len > MAX_COMMAND_LENGTH)

/**
 * @brief Allows easy TX Transaction instantiation.
 */
#define SPI_TXN_TX(segment, txbuff, len) (spi_transaction_t) { \
    .segments = segment, \
    .seglen   = 1, \
    .txbuffer = txbuff, \
    .txlen    = len, \
    .rxbuffer = NULL, \
    .rxlen    = 0 \
}

/**
 * @brief Allows easy RX Transaction instantiation.
 * 
 */
#define SPI_TXN_RX(segment, rxbuff, len) (spi_transaction_t) { \
    .segments = segment, \
    .seglen   = 1, \
    .txbuffer = NULL, \
    .txlen    = 0, \
    .rxbuffer = rxbuff, \
    .rxlen    = len \
}

/**
 * @brief Allows easy BIDIR Transaction instantiation.
 * 
 */
#define SPI_TXN_BIDIR(segment, txbuff, rxbuff, len) (spi_transaction_t) { \
    .segments = segment, \
    .seglen   = 1, \
    .txbuffer = txbuff, \
    .txlen    = len, \
    .rxbuffer = rxbuff, \
    .rxlen    = len \
}

/**
 * @brief Allows easy generic Transaction instantiation.
 * 
 */
#define SPI_TXN(segment, seg_len, txbuff, rxbuff) (spi_transaction_t) { \
    .segments = segment, \
    .seglen   = seg_len, \
    .txbuffer = txbuff, \
    .txlen    = 0, \
    .rxbuffer = rxbuff, \
    .rxlen    = 0 \
}

/****************************************************************************/
/**                                                                        **/
/**                       TYPEDEFS AND STRUCTURES                          **/
/**                                                                        **/
/****************************************************************************/

/**
 * @brief Transaction Structure. Holds all information relevant to a transaction.
 * 
 */
typedef struct {
    const spi_segment_t* segments;  // Pointer to array/buffer of command segments
    uint8_t              seglen;    // Size of the command segents array/buffer
    const uint32_t*      txbuffer;  // Pointer to array/buffer of TX data
    uint32_t             txlen;     // Size of TX array/buffer
    uint32_t*            rxbuffer;  // Pointer to array/buffer for RX data
    uint32_t             rxlen;     // Size of RX array/buffer
} spi_transaction_t;

/**
 * @brief Structure to hold all relative information about a particular peripheral.
 *  _peripherals variable in this file holds an instance of this structure for every
 *  SPI peripheral defined in the HAL. This is in order to store relevant information
 *  of the peripheral current status, transaction info, and peripheral instance.
 * 
 */
typedef struct {
    spi_host_t*       instance;  // Instance of peripheral defined in HAL
    spi_state_e       state;     // Current state of device
    spi_transaction_t txn;       // Current transaction being processed
    uint32_t          scnt;      // Counter to track segment to process
    uint32_t          wcnt;      // Counter to track TX word being processed
    uint32_t          rcnt;      // Counter to track RX word being processed
    spi_callbacks_t   callbacks; // Callback function to call when done
} spi_peripheral_t;

/****************************************************************************/
/**                                                                        **/
/*                      PROTOTYPES OF LOCAL FUNCTIONS                       */
/**                                                                        **/
/****************************************************************************/

/**
 * @brief Validates the idx and init variables of spi_t.
 * 
 * @param spi Pointer to spi_t structure obtained through spi_init call
 * @return spi_codes_e information about error. SPI_CODE_OK if all went well
 */
spi_codes_e spi_check_valid(spi_t* spi);

/**
 * @brief Validates slave csid.
 * 
 * @param slave to validate
 * @return spi_codes_e information about error. SPI_CODE_OK if all went well
 */
spi_codes_e spi_validate_slave(spi_slave_t slave);

/**
 * @brief Sets the slave configuration options and the slave CSID.
 * 
 * @param spi Pointer to spi_t structure obtained through spi_init call
 * @return spi_codes_e information about error. SPI_CODE_OK if all went well
 */
spi_codes_e spi_set_slave(spi_t* spi);

/**
 * @brief Validation and configuration of device prior to any transaction.
 * 
 * @param spi Pointer to spi_t structure obtained through spi_init call
 * @return spi_codes_e information about error. SPI_CODE_OK if all went well
 */
spi_codes_e spi_prepare_transfer(spi_t* spi);

/**
 * @brief Computes the true frequency that will be set based on the user desired 
 *  frequency.
 * 
 * @param freq Frequency defined by user
 * @return uint32_t True frequency after determining the SPI clk divisor
 */
uint32_t spi_true_slave_freq(uint32_t freq);

/**
 * @brief Validates all the provided segments and counts the number of words for
 *  TX and RX buffers.
 * 
 * @param segments An array of segments to validate
 * @param segments_len The length of the array of segments
 * @param tx_count Variable to store the counted TX words
 * @param rx_count Variable to store the counted RX words
 * @return true if all segments are valid
 * @return false otherwise
 */
bool spi_validate_segments(const spi_segment_t* segments, uint32_t segments_len, 
                           uint32_t* tx_count, uint32_t* rx_count);

/**
 * @brief Fills the TX FIFO until no more space or no more data.
 * 
 * @param peri Pointer to the spi_peripheral_t instance with the relevant data
 */
void spi_fill_tx(spi_peripheral_t* peri);

/**
 * @brief Empties the RX FIFO.
 * 
 * @param peri Pointer to the spi_peripheral_t instance with the relevant data
 */
void spi_empty_rx(spi_peripheral_t* peri);

/**
 * @brief Proceeds to initiate transaction once all tests passed.
 * 
 * @param peri Pointer to the relevant spi_peripheral_t instance
 * @param spi Pointer to the spi_t that requested transaction
 * @param txn Transaction data (segments, buffers, lengths)
 * @param done_cb The callback to be called once transaction is over
 * @param error_cb The callback to be called if a Hardware error occurs
 */
void spi_launch(spi_peripheral_t* peri, spi_t* spi, spi_transaction_t txn, 
                spi_callbacks_t callbacks);

/**
 * @brief Issue a command segment.
 * 
 * @param peri Pointer to the relevant spi_peripheral_t instance
 * @param seg The segment to be issued
 * @param csaat If the CS line should remain active once segment is done
 */
void spi_issue_cmd(const spi_peripheral_t* peri, spi_segment_t seg, bool csaat);

/**
 * @brief Resets the variables of the spi_peripheral_t to their initial values
 *  (except the callbacks which are always reset at the beginning of a transaction)
 * 
 * @param peri Pointer to the relevant spi_peripheral_t instance
 */
void spi_reset_peri(spi_peripheral_t* peri);

/**
 * @brief Function that gets called on each Event Interrupt. Handles all the logic
 *  of a transacton since all transactions use the interrupt.
 * 
 * @param peri Pointer to the relevant spi_peripheral_t instance
 * @param events Variable holding all the current events
 */
void spi_event_handler(spi_peripheral_t* peri, spi_event_e events);

/**
 * @brief Function that handles the harware errors. It is responsible of aborting
 *  any current transaction, resetting variables and calling the error callback if any.
 * 
 * @param peri Pointer to the relevant spi_peripheral_t instance
 * @param error Variable holding all the errors that occured
 */
void spi_error_handler(spi_peripheral_t* peri, spi_error_e error);

/****************************************************************************/
/**                                                                        **/
/**                          EXPORTED VARIABLES                            **/
/**                                                                        **/
/****************************************************************************/

/****************************************************************************/
/**                                                                        **/
/*                            GLOBAL VARIABLES                              */
/**                                                                        **/
/****************************************************************************/

static volatile spi_peripheral_t _peripherals[] = {
    (spi_peripheral_t) {
        .instance  = spi_flash,
        .state     = SPI_STATE_NONE,
        .txn       = {0},
        .scnt      = 0,
        .wcnt      = 0,
        .rcnt      = 0,
        .callbacks = NULL_CALLBACKS
    },
    (spi_peripheral_t) {
        .instance  = spi_host1,
        .state     = SPI_STATE_NONE,
        .txn       = {0},
        .scnt      = 0,
        .wcnt      = 0,
        .rcnt      = 0,
        .callbacks = NULL_CALLBACKS
    },
    (spi_peripheral_t) {
        .instance  = spi_host2,
        .state     = SPI_STATE_NONE,
        .txn       = {0},
        .scnt      = 0,
        .wcnt      = 0,
        .rcnt      = 0,
        .callbacks = NULL_CALLBACKS
    }
};

/****************************************************************************/
/**                                                                        **/
/**                          EXPORTED FUNCTIONS                            **/
/**                                                                        **/
/****************************************************************************/

spi_t spi_init(spi_idx_e idx, spi_slave_t slave) 
{
    spi_codes_e error = spi_validate_slave(slave);
    if (SPI_IDX_INVALID(idx)) error |= SPI_CODE_IDX_INVAL;
    if (error)
        return (spi_t) {
            .idx      = UINT32_MAX,
            .init     = false,
            .slave    = (spi_slave_t) {0}
        };
    spi_set_enable(_peripherals[idx].instance, true);
    spi_output_enable(_peripherals[idx].instance, true);
    spi_set_errors_enabled(_peripherals[idx].instance, SPI_ERROR_IRQALL, true);
    _peripherals[idx].state = SPI_STATE_INIT;
    slave.freq = spi_true_slave_freq(slave.freq);
    return (spi_t) {
        .idx      = idx,
        .init     = true,
        .slave    = slave
    };
}

void spi_deinit(spi_t* spi) 
{
    spi->idx      = UINT32_MAX;
    spi->init     = false;
    spi->slave    = (spi_slave_t) {0};
}

spi_codes_e spi_reset(spi_t* spi) 
{
    spi_codes_e error = spi_check_valid(spi);
    if (error) return error;

    spi_sw_reset(_peripherals[spi->idx].instance);

    return SPI_CODE_OK;
}

spi_state_e spi_get_state(spi_t* spi) 
{
    spi_codes_e error = spi_check_valid(spi);
    if (error) return SPI_STATE_ARG_INVAL;

    return _peripherals[spi->idx].state;
}

spi_codes_e spi_transmit(spi_t* spi, const uint32_t* src_buffer, uint32_t len) 
{
    spi_codes_e error = spi_prepare_transfer(spi);
    if (SPI_INVALID_LEN(len)) error |= SPI_CODE_TXN_LEN_INVAL;
    if (error) return error;

    spi_segment_t     seg = SPI_SEG_TX(len);
    spi_transaction_t txn = SPI_TXN_TX(&seg, src_buffer, LEN_WORDS(len));

    spi_launch(&_peripherals[spi->idx], spi, txn, NULL_CALLBACKS);

    while (SPI_BUSY(_peripherals[spi->idx])) wait_for_interrupt();

    return SPI_CODE_OK;
}

spi_codes_e spi_receive(spi_t* spi, uint32_t* dest_buffer, uint32_t len) 
{
    spi_codes_e error = spi_prepare_transfer(spi);
    if (SPI_INVALID_LEN(len)) error |= SPI_CODE_TXN_LEN_INVAL;
    if (error) return error;

    spi_segment_t     seg = SPI_SEG_RX(len);
    spi_transaction_t txn = SPI_TXN_RX(&seg, dest_buffer, LEN_WORDS(len));

    spi_launch(&_peripherals[spi->idx], spi, txn, NULL_CALLBACKS);

    while (SPI_BUSY(_peripherals[spi->idx])) wait_for_interrupt();

    return SPI_CODE_OK;
}

spi_codes_e spi_transceive(spi_t* spi, const uint32_t* src_buffer, 
                           uint32_t* dest_buffer, uint32_t len) 
{
    spi_codes_e error = spi_prepare_transfer(spi);
    if (SPI_INVALID_LEN(len)) error |= SPI_CODE_TXN_LEN_INVAL;
    if (error) return error;

    spi_segment_t     seg = SPI_SEG_BIDIR(len);
    spi_transaction_t txn = SPI_TXN_BIDIR(&seg, src_buffer, dest_buffer, LEN_WORDS(len));

    spi_launch(&_peripherals[spi->idx], spi, txn, NULL_CALLBACKS);

    while (SPI_BUSY(_peripherals[spi->idx])) wait_for_interrupt();

    return SPI_CODE_OK;
}

spi_codes_e spi_execute(spi_t* spi, const spi_segment_t* segments, 
                        uint32_t segments_len, const uint32_t* src_buffer, 
                        uint32_t* dest_buffer) 
{
    spi_codes_e error = spi_prepare_transfer(spi);
    if (error) return error;

    spi_transaction_t txn = SPI_TXN(segments, segments_len, src_buffer, dest_buffer);

    if (!spi_validate_segments(txn.segments, txn.seglen, &txn.txlen, &txn.rxlen)) 
        return SPI_CODE_SEGMENT_INVAL;

    spi_launch(&_peripherals[spi->idx], spi, txn, NULL_CALLBACKS);

    while (SPI_BUSY(_peripherals[spi->idx])) wait_for_interrupt();

    return SPI_CODE_OK;
}

spi_codes_e spi_transmit_nb(spi_t* spi, const uint32_t* src_buffer, uint32_t len, 
                            spi_callbacks_t callbacks) 
{
    spi_codes_e error = spi_prepare_transfer(spi);
    if (SPI_INVALID_LEN(len)) error |= SPI_CODE_TXN_LEN_INVAL;
    if (error) return error;

    spi_segment_t     seg = SPI_SEG_TX(len);
    spi_transaction_t txn = SPI_TXN_TX(&seg, src_buffer, LEN_WORDS(len));

    spi_launch(&_peripherals[spi->idx], spi, txn, callbacks);

    return SPI_CODE_OK;
}

spi_codes_e spi_receive_nb(spi_t* spi, uint32_t* dest_buffer, uint32_t len, 
                           spi_callbacks_t callbacks) 
{
    spi_codes_e error = spi_prepare_transfer(spi);
    if (SPI_INVALID_LEN(len)) error |= SPI_CODE_TXN_LEN_INVAL;
    if (error) return error;

    spi_segment_t     seg = SPI_SEG_RX(len);
    spi_transaction_t txn = SPI_TXN_RX(&seg, dest_buffer, LEN_WORDS(len));

    spi_launch(&_peripherals[spi->idx], spi, txn, callbacks);

    return SPI_CODE_OK;
}

spi_codes_e spi_transceive_nb(spi_t* spi, const uint32_t* src_buffer, uint32_t* dest_buffer, 
                              uint32_t len, spi_callbacks_t callbacks) 
{
    spi_codes_e error = spi_prepare_transfer(spi);
    if (SPI_INVALID_LEN(len)) error |= SPI_CODE_TXN_LEN_INVAL;
    if (error) return error;

    spi_segment_t     seg = SPI_SEG_BIDIR(len);
    spi_transaction_t txn = SPI_TXN_BIDIR(&seg, src_buffer, dest_buffer, LEN_WORDS(len));

    spi_launch(&_peripherals[spi->idx], spi, txn, callbacks);

    return SPI_CODE_OK;
}

spi_codes_e spi_execute_nb(spi_t* spi, const spi_segment_t* segments, 
                           uint32_t segments_len, const uint32_t* src_buffer, 
                           uint32_t* dest_buffer, spi_callbacks_t callbacks) 
{
    spi_codes_e error = spi_prepare_transfer(spi);
    if (error) return error;

    spi_transaction_t txn = SPI_TXN(segments, segments_len, src_buffer, dest_buffer);

    if (!spi_validate_segments(txn.segments, txn.seglen, &txn.txlen, &txn.rxlen)) 
        return SPI_CODE_SEGMENT_INVAL;

    spi_launch(&_peripherals[spi->idx], spi, txn, callbacks);

    return SPI_CODE_OK;
}

/****************************************************************************/
/**                                                                        **/
/*                            LOCAL FUNCTIONS                               */
/**                                                                        **/
/****************************************************************************/

spi_codes_e spi_check_valid(spi_t* spi) 
{
    if (SPI_IDX_INVALID(spi->idx)) return SPI_CODE_IDX_INVAL;
    if (!spi->init)                return SPI_CODE_NOT_INIT;
    return SPI_CODE_OK;
}

spi_codes_e spi_validate_slave(spi_slave_t slave) 
{
    spi_codes_e error = SPI_CODE_OK;
    if (SPI_CSID_INVALID(slave.csid)) error |= SPI_CODE_SLAVE_CSID_INVAL;
    if (slave.freq < SPI_MIN_FREQ)    error |= SPI_CODE_SLAVE_FREQ_INVAL;
    return error;
}

spi_codes_e spi_set_slave(spi_t* spi) 
{
    if (spi_get_active(_peripherals[spi->idx].instance) == SPI_TRISTATE_TRUE) 
      return SPI_CODE_NOT_IDLE;

    uint16_t clk_div = 0;
    if (spi->slave.freq < SYS_FREQ / 2) {
        clk_div = (SYS_FREQ / spi->slave.freq - 2) / 2;
        if (SYS_FREQ / (2 * clk_div + 2) > spi->slave.freq) clk_div++;
    }
    spi_configopts_t config = {
        .clkdiv   = clk_div,
        .cpha     = bitfield_read(spi->slave.data_mode, BIT_MASK_1, DATA_MODE_CPHA_OFFS),
        .cpol     = bitfield_read(spi->slave.data_mode, BIT_MASK_1, DATA_MODE_CPOL_OFFS),
        .csnidle  = spi->slave.csn_idle,
        .csnlead  = spi->slave.csn_lead,
        .csntrail = spi->slave.csn_trail,
        .fullcyc  = spi->slave.full_cycle
    };
    spi_return_flags_e config_error = spi_set_configopts(_peripherals[spi->idx].instance, 
                                                         spi->slave.csid,
                                                         spi_create_configopts(config));
    if (config_error) return SPI_CODE_SLAVE_INVAL;
    spi_set_csid(_peripherals[spi->idx].instance, spi->slave.csid);
    return SPI_CODE_OK;
}

spi_codes_e spi_prepare_transfer(spi_t* spi) 
{
    spi_codes_e error = spi_check_valid(spi);
    if (error) return error;

    if (SPI_BUSY(_peripherals[spi->idx])) return SPI_CODE_IS_BUSY;

    error = spi_set_slave(spi);
    if (error) return error;

    return SPI_CODE_OK;
}

uint32_t spi_true_slave_freq(uint32_t freq) 
{
    uint16_t clk_div = 0;
    if (freq < SYS_FREQ / 2) {
        clk_div = (SYS_FREQ / freq - 2) / 2;
        if (SYS_FREQ / (2 * clk_div + 2) > freq) clk_div++;
    }
    return SYS_FREQ / (2 * clk_div + 2);
}

bool spi_validate_segments(const spi_segment_t* segments, uint32_t segments_len, 
                           uint32_t* tx_count, uint32_t* rx_count) 
{
    *tx_count = 0;
    *rx_count = 0;

    for (int i = 0; i < segments_len; i++)
    {
        uint8_t direction = bitfield_read(segments[i].mode, 0b11, 0);
        uint8_t speed     = bitfield_read(segments[i].mode, 0b11, 2);
        if (!spi_validate_cmd(direction, speed)) return false;
        uint32_t word_len = LEN_WORDS(segments[i].len);
        if (direction == SPI_DIR_TX_ONLY || direction == SPI_DIR_BIDIR) 
            *tx_count += word_len;
        if (direction == SPI_DIR_RX_ONLY || direction == SPI_DIR_BIDIR) 
            *rx_count += word_len;
    }

    return true;
}

void spi_fill_tx(spi_peripheral_t* peri) 
{
    if (peri->txn.txbuffer != NULL && peri->wcnt < peri->txn.txlen) {
        while (
            peri->wcnt < peri->txn.txlen 
            && !spi_write_word(peri->instance, peri->txn.txbuffer[peri->wcnt])
        ) peri->wcnt++;
    }
}

void spi_empty_rx(spi_peripheral_t* peri) 
{
    if (peri->txn.rxbuffer != NULL && peri->rcnt < peri->txn.rxlen) {
        while (
            peri->rcnt < peri->txn.rxlen 
            && !spi_read_word(peri->instance, &peri->txn.rxbuffer[peri->rcnt])
        ) peri->rcnt++;
    }
}

void spi_launch(spi_peripheral_t* peri, spi_t* spi, spi_transaction_t txn, 
                spi_callbacks_t callbacks) 
{
    peri->state     = SPI_STATE_BUSY;
    peri->txn       = txn;
    peri->callbacks = callbacks;

    spi_set_tx_watermark(peri->instance, TX_WATERMARK);
    spi_set_rx_watermark(peri->instance, RX_WATERMARK);

    spi_fill_tx(peri);

    spi_set_events_enabled(peri->instance, 
                           SPI_EVENT_IDLE|SPI_EVENT_READY|SPI_EVENT_TXWM|SPI_EVENT_RXWM, 
                           true);
    spi_enable_evt_intr   (peri->instance, true);

    spi_wait_for_ready(peri->instance);
    peri->scnt++;
    spi_issue_cmd(peri, txn.segments[0], 0 < txn.seglen - 1 ? true : false);
}

void spi_issue_cmd(const spi_peripheral_t* peri, spi_segment_t seg, bool csaat) 
{
    uint32_t cmd_reg = spi_create_command((spi_command_t) {
        .direction = bitfield_read(seg.mode, 0b11, 0),
        .speed     = bitfield_read(seg.mode, 0b11, 2),
        .csaat     = csaat,
        .len       = seg.len - 1
    });
    spi_set_command(peri->instance, cmd_reg);
}

void spi_reset_peri(spi_peripheral_t* peri) 
{
    peri->scnt     = 0;
    peri->wcnt     = 0;
    peri->rcnt     = 0;
    peri->txn      = (spi_transaction_t) {0};
    peri->callbacks = NULL_CALLBACKS;
}

void spi_event_handler(spi_peripheral_t* peri, spi_event_e events) 
{
    if (events & SPI_EVENT_READY) 
    {
        // If SPI is ready and there are still commands to add, add them to queue
        if (peri->txn.segments != NULL && peri->scnt < peri->txn.seglen) 
        {
            spi_issue_cmd(peri, peri->txn.segments[peri->scnt], 
                          peri->scnt < peri->txn.seglen-1 ? true : false);
            peri->scnt++;
        }
        // If no more commands and SPI is idle, then the transaction is over
        else if (events & SPI_EVENT_IDLE) 
        {
            spi_set_events_enabled(peri->instance, SPI_EVENT_ALL, false);
            spi_enable_evt_intr   (peri->instance, false);
            spi_empty_rx(peri);
            peri->state = SPI_STATE_DONE;
            if (peri->callbacks.done_cb != NULL) 
            {
                peri->callbacks.done_cb(peri->txn.txbuffer, peri->txn.txlen, 
                                        peri->txn.rxbuffer, peri->txn.rxlen);
            }
            spi_reset_peri(peri);
            return;
        }
    }
    if (events & SPI_EVENT_TXWM)
    {
        spi_fill_tx(peri);
        if (peri->callbacks.txwm_cb != NULL)
        {
            peri->callbacks.txwm_cb(peri->txn.txbuffer, peri->txn.txlen, 
                                    peri->txn.rxbuffer, peri->txn.rxlen);
        }
    }
    if (events & SPI_EVENT_RXWM)
    {
        spi_empty_rx(peri);
        if (peri->callbacks.rxwm_cb != NULL)
        {
            peri->callbacks.rxwm_cb(peri->txn.txbuffer, peri->txn.txlen, 
                                    peri->txn.rxbuffer, peri->txn.rxlen);
        }
    }
}

void spi_error_handler(spi_peripheral_t* peri, spi_error_e error) 
{
    spi_set_events_enabled(peri->instance, SPI_EVENT_ALL, false);
    spi_enable_evt_intr   (peri->instance, false);
    peri->state = SPI_STATE_ERROR;
    if (peri->callbacks.error_cb != NULL) 
    {
        peri->callbacks.error_cb(peri->txn.txbuffer, peri->txn.txlen, 
                                 peri->txn.rxbuffer, peri->txn.rxlen);
    }
    spi_reset_peri(peri);
}

/****************************************************************************/
/**                                                                        **/
/*                               INTERRUPTS                                 */
/**                                                                        **/
/****************************************************************************/

/**
 * @brief Implementation of the weak function of the HAL
 * 
 */
void spi_intr_handler_event_flash(spi_event_e events) 
{
    if (SPI_NOT_BUSY(_peripherals[SPI_IDX_FLASH])) return;
    spi_event_handler(&_peripherals[SPI_IDX_FLASH], events);
}

/**
 * @brief Implementation of the weak function of the HAL
 * 
 */
void spi_intr_handler_error_flash(spi_error_e errors) 
{
    if (SPI_NOT_BUSY(_peripherals[SPI_IDX_FLASH])) return;
    spi_error_handler(&_peripherals[SPI_IDX_FLASH], errors);
}

/**
 * @brief Implementation of the weak function of the HAL
 * 
 */
void spi_intr_handler_event_host(spi_event_e events) 
{
    if (SPI_NOT_BUSY(_peripherals[SPI_IDX_HOST])) return;
    spi_event_handler(&_peripherals[SPI_IDX_HOST], events);
}

/**
 * @brief Implementation of the weak function of the HAL
 * 
 */
void spi_intr_handler_error_host(spi_error_e errors) 
{
    if (SPI_NOT_BUSY(_peripherals[SPI_IDX_HOST])) return;
    spi_error_handler(&_peripherals[SPI_IDX_HOST], errors);
}

/**
 * @brief Implementation of the weak function of the HAL
 * 
 */
void spi_intr_handler_event_host2(spi_event_e events) 
{
    if (SPI_NOT_BUSY(_peripherals[SPI_IDX_HOST_2])) return;
    spi_event_handler(&_peripherals[SPI_IDX_HOST_2], events);
}

/**
 * @brief Implementation of the weak function of the HAL
 * 
 */
void spi_intr_handler_error_host2(spi_error_e errors) 
{
    if (SPI_NOT_BUSY(_peripherals[SPI_IDX_HOST_2])) return;
    spi_error_handler(&_peripherals[SPI_IDX_HOST_2], errors);
}

/****************************************************************************/
/**                                                                        **/
/**                                EOF                                     **/
/**                                                                        **/
/****************************************************************************/
