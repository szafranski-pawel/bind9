/*
 * Copyright (C) Internet Systems Consortium, Inc. ("ISC")
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * See the COPYRIGHT file distributed with this work for additional
 * information regarding copyright ownership.
 */

#pragma once

typedef struct dns_rdata_iot_string {
	uint8_t length;
	unsigned char *data;
} dns_rdata_iot_string_t;

typedef struct dns_rdata_iot {
	dns_rdatacommon_t common;
	isc_mem_t *mctx;
	unsigned char *txt;
	uint16_t txt_len;
	/* private */
	uint16_t offset;
} dns_rdata_iot_t;
