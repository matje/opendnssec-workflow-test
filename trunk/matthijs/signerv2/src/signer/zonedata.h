/*
 * $Id$
 *
 * Copyright (c) 2009 NLNet Labs. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 * Zone data.
 *
 */

#ifndef SIGNER_ZONEDATA_H
#define SIGNER_ZONEDATA_H

#include "config.h"
#include "signer/domain.h"

#include <ldns/ldns.h>

/**
 * Zone data.
 *
 */
typedef struct zonedata_struct zonedata_type;
struct zonedata_struct {
    ldns_rbtree_t* domains;
};

/**
 * Create empty zone data..
 * \return zonedata_type* empty zone data tree
 *
 */
zonedata_type* zonedata_create(void);

/**
 * Add domain to zone data.
 * \param[in] zd zone data
 * \param[in] domain domain to add
 * \return domain_type* added domain
 *
 */
domain_type* zonedata_add_domain(zonedata_type* zd, domain_type* domain);

/**
 * Look up domain in zone data.
 * \param[in] zd zone data
 * \param[in] domain domain to look for
 * \return domain_type* domain, if found
 *
 */
domain_type* zonedata_lookup_domain(zonedata_type* zd, domain_type* domain);

/**
 * Clean up zone data.
 * \param[in] zonedata zone data to cleanup
 *
 */
void zonedata_cleanup(zonedata_type* zonedata);

/**
 * Print zone data.
 * \param[in] out file descriptor
 * \param[in] zone data zone data to print
 * \param[in] skip_soa if we already have printed the soa
 *
 */
void zonedata_print(FILE* fd, zonedata_type* zonedata, int skip_soa);

#endif /* SIGNER_ZONEDATA_H */
