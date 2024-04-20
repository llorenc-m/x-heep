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
#include "soc_ctrl_structs.h"

/****************************************************************************/
/**                                                                        **/
/**                       DEFINITIONS AND MACROS                           **/
/**                                                                        **/
/****************************************************************************/

/****************************************************************************/
/**                                                                        **/
/**                       TYPEDEFS AND STRUCTURES                          **/
/**                                                                        **/
/****************************************************************************/

typedef struct {
    spi_host_t instance;
    bool busy;
    uint32_t* txbuffer;
    uint32_t* rxbuffer;
} spi_t;

/****************************************************************************/
/**                                                                        **/
/*                      PROTOTYPES OF LOCAL FUNCTIONS                       */
/**                                                                        **/
/****************************************************************************/

spi_codes_e spi_check_init(const spi_idx_e idx);
spi_codes_e spi_validate_slave(const spi_slave_t slave);

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

spi_t* _spi_array[] = {NULL, NULL, NULL};

/****************************************************************************/
/**                                                                        **/
/**                          EXPORTED FUNCTIONS                            **/
/**                                                                        **/
/****************************************************************************/

spi_codes_e spi_init(const spi_idx_e idx) {
    spi_codes_e error = spi_check_init(idx);
    if (error) return error;

    spi_t* spi    = malloc(sizeof(spi_t));
    spi->instance = spi_init_flash();
    spi->busy     = false;
    spi->txbuffer = NULL;
    spi->rxbuffer = NULL;

    _spi_array[idx] = spi;
    return SPI_CODE_OK;
}

spi_codes_e spi_deinit(const spi_idx_e idx) {
    if (SPI_IDX_INVALID(idx))    return SPI_CODE_IDX_INVAL;
    if (_spi_array[idx] == NULL) return SPI_CODE_NOT_INIT;

    free(_spi_array[idx]);

    _spi_array[idx] = NULL;
    return SPI_CODE_OK;
}

spi_codes_e spi_set_slave(const spi_idx_e idx, const spi_slave_t slave) {
    spi_codes_e error = spi_check_init(idx);
    if (error) return error;
    error = spi_validate_slave(slave);
    if (error) return error;

    uint16_t clk_div = soc_ctrl_peri->SYSTEM_FREQUENCY_HZ / (2 * slave.freq) - 1;
    if (soc_ctrl_peri->SYSTEM_FREQUENCY_HZ / (2 * (clk_div + 1)) > slave.freq)
        clk_div++;
    spi_configopts_t config = {
        .clkdiv = clk_div,
        .cpha = slave.phase,
        .cpol = slave.polarity,
        .csnidle = slave.csn_idle,
        .csnlead = slave.csn_lead,
        .csntrail = slave.csn_trail,
        .fullcyc = slave.full_cycle
    };
    spi_return_flags_e config_error = spi_set_configopts(_spi_array[idx]->instance, 
                                                         slave.csid,
                                                         spi_create_configopts(config)
                                                        );
    if (config_error) return SPI_CODE_BASE_ERROR;
    return SPI_CODE_OK;
}

spi_codes_e spi_get_slave(const spi_idx_e idx, const uint8_t csid, spi_slave_t* slave) {
    spi_codes_e error = spi_check_init(idx);
    if (error) return error;
    if (SPI_CSID_INVALID(csid)) return SPI_CODE_SLAVE_CSID_INVAL;

    
}

/****************************************************************************/
/**                                                                        **/
/*                            LOCAL FUNCTIONS                               */
/**                                                                        **/
/****************************************************************************/

spi_codes_e spi_check_init(const spi_idx_e idx) {
    if (SPI_IDX_INVALID(idx))    return SPI_CODE_IDX_INVAL;
    if (_spi_array[idx] != NULL) return SPI_CODE_INIT;
    return SPI_CODE_OK;
}

spi_codes_e spi_validate_slave(const spi_slave_t slave) {
    spi_codes_e error = SPI_CODE_OK;
    if (SPI_CSID_INVALID(slave.csid)) error += SPI_CODE_SLAVE_CSID_INVAL;
    if (slave.freq > soc_ctrl_peri->SYSTEM_FREQUENCY_HZ / 2) error += SPI_CODE_SLAVE_FREQ_INVAL;
    return error;
}

/****************************************************************************/
/**                                                                        **/
/**                                EOF                                     **/
/**                                                                        **/
/****************************************************************************/
