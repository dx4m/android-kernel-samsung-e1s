/****************************************************************************
 *
 * Copyright (c) 2014 - 2021 Samsung Electronics Co., Ltd. All rights reserved
 *
 ****************************************************************************/

#ifndef WHDR_H
#define WHDR_H

#define FW_BUILD_ID_SZ 128
#define FW_TTID_SZ 32
#define MX_FW_RUNTIME_LENGTH (1024 * 1024)
/* uses */
#include "fwhdr_if.h"

struct fwhdr_if *whdr_create(void);
void whdr_destroy(struct fwhdr_if *interface);

/** Definition of tags */
enum whdr_tag
{
    /**
     * The total length of the entire WHDR this tag is contained in, including the size of all
     * tag, length and value fields.
     *
     * This special tag must be present as first item for validation/integrity check purposes,
     * and to be able to determine the overall length of the WHDR.
     *
     * Value length: 4 byte
     * Value encoding: uint32_t
     */
    WHDR_TAG_TOTAL_LENGTH = 0x52444857, /* "WHDR" */

    /**
     * Offset of location to load the WLAN firmware binary image. The offset is relative to
     * the beginning of the entire memory region that is shared between the AP and WLBT.
     *
     * Value length: 4 byte
     * Value encoding: uint32_t
     */
    WHDR_TAG_FW_OFFSET = 1,

    /**
     * Runtime size of firmware
     *
     * This accommodates not only the WLAN firmware binary image itself, but also any
     * statically allocated memory (such as zero initialised static variables) immediately
     * following it that is used by the firmware at runtime after it has booted.
     *
     * Value length: 4 byte
     * Value encoding: uint32_t
     */
    WHDR_TAG_FW_RUNTIME_SIZE = 2,

    /**
     * Firmware build identifier string
     *
     * Value length: N byte
     * Value encoding: uint8_t[N]
     */
    WHDR_TAG_FW_BUILD_ID = 3,

    /**
     * Total size of constant + LMA DRAM area
     *
     * The size of the part of the DRAM area that is read only,
     * e.g. constants + LMA area
     *
     * Value length: 4 byte
     * Value encoding: uint32_t
     */
    WHDR_TAG_FW_DRAM_CONSTANT_LMA_SIZE = 6,

    /**
     * Offset and length of a system_error_descriptor array.
     *
     * Value length: 8 byte
     * Value encoding: whdr_offset_length
     */
    WHDR_TAG_FW_SYSTEM_ERROR_DESCRIPTOR = 8,
};

/**
 * Structure describing the tag and length of each item in the WHDR.
 * The value of the item follows immediately after.
 */
struct whdr_tag_length
{
    uint32_t tag;    /* One of whdr_tag */
    uint32_t length; /* Length of value in bytes */
};


/**
 * Structure describing the offset and length of an array.
 */
struct whdr_offset_length
{
    uint32_t offset; /* Offset relative to the entire memory region */
    uint32_t length; /* The length of the array */
};


/**
 * Structure describing the system error record offset and description.
 *
 * An offset of 0 is a placeholder and means no system error is present.
 *
 * Any system error record pointed to by this must have the following structure:
 *     uint32_t version;
 *     uint32_t length;
 *     uint32_t info[4]
 *     ...
 *     uint32_t cksum (always as the last field, which can be found using the length)
 *
 * For description of the fields see the system error record header file.
 *
 * If another format is needed the WHDR_TAG_FW_SYSTEM_ERROR_DESCRIPTOR  tag should not
 * be used.
 */
struct system_error_descriptor
{
    uint32_t offset; /* Offset relative to the entire memory region */
    uint32_t name; /* Offset in the string section to the name of this record */
};

#endif /* WHDR_H */
