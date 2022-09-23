// Generated register defines for dma

// Copyright information found in source file:
// Copyright EPFL contributors.

// Licensing information found in source file:
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#ifndef _DMA_REG_DEFS_
#define _DMA_REG_DEFS_

#ifdef __cplusplus
extern "C" {
#endif
// Register width
#define DMA_PARAM_REG_WIDTH 32

// Input data pointer (word aligned)
#define DMA_PTR_IN_REG_OFFSET 0x0

// Output data pointer (word aligned)
#define DMA_PTR_OUT_REG_OFFSET 0x4

// Number of words (4 bytes) to copy - Once a value is written, the copy
// starts
#define DMA_DMA_START_REG_OFFSET 0x8

// Register set to 1 when copy is done
#define DMA_DONE_REG_OFFSET 0xc

// Increment number of source pointer every time a word is copied from source
#define DMA_SRC_PTR_INC_REG_OFFSET 0x10

// Increment number of source pointer every time a word is copied to
// destination
#define DMA_DST_PTR_INC_REG_OFFSET 0x14

// Register set to 1 when copy from SPI, it waits for the spi watermark to
// start the transaction
#define DMA_SPI_MODE_REG_OFFSET 0x18
#define DMA_SPI_MODE_SPI_MODE_BIT 0

#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // _DMA_REG_DEFS_
// End generated register defines for dma