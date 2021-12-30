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

#ifndef RDATA_GENERIC_IOT_4096_C
#define RDATA_GENERIC_IOT_4096_C

#define RRTYPE_IOT_ATTRIBUTES (0)

static inline isc_result_t
fromtext_iot(ARGS_FROMTEXT) {
	REQUIRE(type == dns_rdatatype_iot);

	return (generic_fromtext_txt(CALL_FROMTEXT));
}

static inline isc_result_t
totext_iot(ARGS_TOTEXT) {
	REQUIRE(rdata != NULL);
	REQUIRE(rdata->type == dns_rdatatype_iot);

	return (generic_totext_txt(CALL_TOTEXT));
}

static inline isc_result_t
fromwire_iot(ARGS_FROMWIRE) {
	REQUIRE(type == dns_rdatatype_iot);

	return (generic_fromwire_txt(CALL_FROMWIRE));
}

static inline isc_result_t
towire_iot(ARGS_TOWIRE) {
	REQUIRE(rdata->type == dns_rdatatype_iot);

	UNUSED(cctx);

	return (mem_tobuffer(target, rdata->data, rdata->length));
}

static inline int
compare_iot(ARGS_COMPARE) {
	isc_region_t r1;
	isc_region_t r2;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == dns_rdatatype_iot);

	dns_rdata_toregion(rdata1, &r1);
	dns_rdata_toregion(rdata2, &r2);
	return (isc_region_compare(&r1, &r2));
}

static inline isc_result_t
fromstruct_iot(ARGS_FROMSTRUCT) {
	REQUIRE(type == dns_rdatatype_iot);

	return (generic_fromstruct_txt(CALL_FROMSTRUCT));
}

static inline isc_result_t
tostruct_iot(ARGS_TOSTRUCT) {
	dns_rdata_iot_t *iot = target;

	REQUIRE(iot != NULL);
	REQUIRE(rdata != NULL);
	REQUIRE(rdata->type == dns_rdatatype_iot);

	iot->common.rdclass = rdata->rdclass;
	iot->common.rdtype = rdata->type;
	ISC_LINK_INIT(&iot->common, link);

	return (generic_tostruct_txt(CALL_TOSTRUCT));
}

static inline void
freestruct_iot(ARGS_FREESTRUCT) {
	dns_rdata_iot_t *iot = source;

	REQUIRE(iot != NULL);
	REQUIRE(iot->common.rdtype == dns_rdatatype_iot);

	generic_freestruct_txt(source);
}

static inline isc_result_t
additionaldata_iot(ARGS_ADDLDATA) {
	REQUIRE(rdata->type == dns_rdatatype_iot);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(add);
	UNUSED(arg);

	return (ISC_R_SUCCESS);
}

static inline isc_result_t
digest_iot(ARGS_DIGEST) {
	isc_region_t r;

	REQUIRE(rdata->type == dns_rdatatype_iot);

	dns_rdata_toregion(rdata, &r);

	return ((digest)(arg, &r));
}

static inline bool
checkowner_iot(ARGS_CHECKOWNER) {
	REQUIRE(type == dns_rdatatype_iot);

	UNUSED(name);
	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(wildcard);

	return (true);
}

static inline bool
checknames_iot(ARGS_CHECKNAMES) {
	REQUIRE(rdata->type == dns_rdatatype_iot);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(bad);

	return (true);
}

static inline int
casecompare_iot(ARGS_COMPARE) {
	return (compare_iot(rdata1, rdata2));
}

#endif /* RDATA_GENERIC_IOT_4096_C */
