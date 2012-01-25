/*
 * $Id: axfr.c 4958 2011-04-18 07:11:09Z matthijs $
 *
 * Copyright (c) 2011 NLNet Labs. All rights reserved.
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
 * AXFR.
 *
 */

#include "config.h"
#include "adapter/addns.h"
#include "adapter/adutil.h"
#include "shared/file.h"
#include "shared/util.h"
#include "wire/axfr.h"
#include "wire/buffer.h"
#include "wire/query.h"
#include "wire/sock.h"

#define AXFR_TSIG_SIGN_EVERY_NTH 96 /* tsig sign every N packets. */

const char* axfr_str = "axfr";


/**
 * Do AXFR.
 *
 */
query_state
axfr(query_type* q, engine_type* engine)
{
    char* xfrfile = NULL;
    ldns_rr* rr = NULL;
    ldns_rdf* prev = NULL;
    ldns_rdf* orig = NULL;
    uint16_t total_added = 0;
    uint32_t ttl = 0;
    ldns_status status = LDNS_STATUS_OK;
    char line[SE_ADFILE_MAXLINE];
    unsigned l = 0;
    long fpos = 0;
    size_t bufpos = 0;
    ods_log_assert(q);
    ods_log_assert(q->buffer);
    ods_log_assert(engine);
    if (q->axfr_is_done) {
        return QUERY_PROCESSED;
    }
    if (q->maxlen > AXFR_MAX_MESSAGE_LEN) {
        q->maxlen = AXFR_MAX_MESSAGE_LEN;
    }

    /* prepare tsig */
    q->tsig_prepare_it = 0;
    q->tsig_update_it = 1;
    if (q->tsig_sign_it) {
        q->tsig_prepare_it = 1;
        q->tsig_sign_it = 0;
    }
    ods_log_assert(q->tsig_rr);
    ods_log_assert(q->zone);
    ods_log_assert(q->zone->name);
    if (q->axfr_fd == NULL) {
        /* start axfr */
        xfrfile = ods_build_path(q->zone->name, ".axfr", 0);
        q->axfr_fd = ods_fopen(xfrfile, NULL, "r");
        if (!q->axfr_fd) {
            ods_log_error("[%s] unable to open axfr file %s for zone %s",
                axfr_str, xfrfile, q->zone->name);
            free((void*)xfrfile);
            buffer_pkt_set_rcode(q->buffer, LDNS_RCODE_SERVFAIL);
            return QUERY_PROCESSED;
        }
        free((void*)xfrfile);
        if (q->tsig_rr->status == TSIG_OK) {
            q->tsig_sign_it = 1; /* sign first packet in stream */
        }
        /* compression? */

        /* add soa rr */
        fpos = ftell(q->axfr_fd);
        rr = addns_read_rr(q->axfr_fd, line, &orig, &prev, &ttl, &status,
            &l);
        if (!rr) {
            /* no SOA no transfer */
            ods_log_error("[%s] bad axfr zone %s, corrupted file",
                axfr_str, q->zone->name);
            buffer_pkt_set_rcode(q->buffer, LDNS_RCODE_SERVFAIL);
            ods_fclose(q->axfr_fd);
            q->axfr_fd = NULL;
            return QUERY_PROCESSED;
        }
        if (ldns_rr_get_type(rr) != LDNS_RR_TYPE_SOA) {
            ods_log_error("[%s] bad axfr zone %s, first rr is not soa",
                axfr_str, q->zone->name);
            ldns_rr_free(rr);
            buffer_pkt_set_rcode(q->buffer, LDNS_RCODE_SERVFAIL);
            ods_fclose(q->axfr_fd);
            q->axfr_fd = NULL;
            return QUERY_PROCESSED;
        }
        /* does it fit? */
        if (buffer_write_rr(q->buffer, rr)) {
            ods_log_debug("[%s] set soa in axfr zone %s", axfr_str,
                q->zone->name);
            buffer_pkt_set_ancount(q->buffer, buffer_pkt_ancount(q->buffer)+1);
            total_added++;
            ldns_rr_free(rr);
            rr = NULL;
            bufpos = buffer_position(q->buffer);
        } else {
            ods_log_error("[%s] soa does not fit in axfr zone %s",
                axfr_str, q->zone->name);
            ldns_rr_free(rr);
            rr = NULL;
            buffer_pkt_set_rcode(q->buffer, LDNS_RCODE_SERVFAIL);
            ods_fclose(q->axfr_fd);
            q->axfr_fd = NULL;
            return QUERY_PROCESSED;
        }
    } else if (q->tcp) {
        /* subsequent axfr packets */
        ods_log_debug("[%s] subsequent axfr packet zone %s", axfr_str,
            q->zone->name);
        buffer_set_limit(q->buffer, BUFFER_PKT_HEADER_SIZE);
        buffer_pkt_set_qdcount(q->buffer, 0);
        query_prepare(q);
    }
    /* add as many records as fit */
    fpos = ftell(q->axfr_fd);
    while ((rr = addns_read_rr(q->axfr_fd, line, &orig, &prev, &ttl,
        &status, &l)) != NULL) {
        ods_log_deeebug("[%s] read rr at line %d", axfr_str, l);
        if (status != LDNS_STATUS_OK) {
            ldns_rr_free(rr);
            rr = NULL;
            ods_log_error("[%s] error reading rr at line %i (%s): %s",
                axfr_str, l, ldns_get_errorstr_by_id(status), line);
            buffer_pkt_set_rcode(q->buffer, LDNS_RCODE_SERVFAIL);
            ods_fclose(q->axfr_fd);
            q->axfr_fd = NULL;
            return QUERY_PROCESSED;
        }
        /* does it fit? */
        if (buffer_write_rr(q->buffer, rr)) {
            ods_log_deeebug("[%s] add rr at line %d", axfr_str, l);
            fpos = ftell(q->axfr_fd);
            buffer_pkt_set_ancount(q->buffer, buffer_pkt_ancount(q->buffer)+1);
            total_added++;
            ldns_rr_free(rr);
            rr = NULL;
        } else {
            ods_log_deeebug("[%s] rr at line %d does not fit", axfr_str, l);
            ldns_rr_free(rr);
            rr = NULL;
            if (fseek(q->axfr_fd, fpos, SEEK_SET) != 0) {
                ods_log_error("[%s] unable to reset file position in axfr "
                    "file: fseek() failed (%s)", axfr_str, strerror(errno));
                buffer_pkt_set_rcode(q->buffer, LDNS_RCODE_SERVFAIL);
                ods_fclose(q->axfr_fd);
                q->axfr_fd = NULL;
                return QUERY_PROCESSED;
            } else if (q->tcp) {
                goto return_axfr;
            } else {
                goto udp_overflow;
            }
        }
    }
    ods_log_debug("[%s] axfr zone %s is done", axfr_str, q->zone->name);
    q->tsig_sign_it = 1; /* sign last packet */
    q->axfr_is_done = 1;
    ods_fclose(q->axfr_fd);
    q->axfr_fd = NULL;

return_axfr:
    if (q->tcp) {
        ods_log_debug("[%s] return part axfr zone %s", axfr_str,
            q->zone->name);
        buffer_pkt_set_ancount(q->buffer, total_added);
        buffer_pkt_set_nscount(q->buffer, 0);
        buffer_pkt_set_arcount(q->buffer, 0);
        /* check if it needs tsig signatures */
        if (q->tsig_rr->status == TSIG_OK) {
            if (q->tsig_rr->update_since_last_prepare >=
                AXFR_TSIG_SIGN_EVERY_NTH) {
                q->tsig_sign_it = 1;
            }
        }
        return QUERY_AXFR;
    }
udp_overflow:
    /* UDP Overflow */
    ods_log_info("[%s] axfr udp overflow zone %s", axfr_str, q->zone->name);
    buffer_set_position(q->buffer, bufpos);
    buffer_pkt_set_ancount(q->buffer, 1);
    buffer_pkt_set_nscount(q->buffer, 0);
    buffer_pkt_set_arcount(q->buffer, 0);
    /* check if it needs tsig signatures */
    if (q->tsig_rr->status == TSIG_OK) {
        q->tsig_sign_it = 1;
    }
    return QUERY_PROCESSED;

}


/**
 * Do IXFR (equal to AXFR for now).
 *
 */
query_state
ixfr(query_type* q, engine_type* engine)
{
    char* xfrfile = NULL;
    ldns_rr* rr = NULL;
    ldns_rdf* prev = NULL;
    ldns_rdf* orig = NULL;
    uint16_t total_added = 0;
    uint32_t ttl = 0;
    ldns_status status = LDNS_STATUS_OK;
    char line[SE_ADFILE_MAXLINE];
    unsigned l = 0;
    long fpos = 0;
    size_t bufpos = 0;
    uint32_t new_serial = 0;
    unsigned del_mode = 0;
    unsigned soa_found = 0;
    ods_log_assert(q);
    ods_log_assert(q->buffer);
    ods_log_assert(engine);
    if (q->axfr_is_done) {
        return QUERY_PROCESSED;
    }
    if (q->maxlen > AXFR_MAX_MESSAGE_LEN) {
        q->maxlen = AXFR_MAX_MESSAGE_LEN;
    }
    /* prepare tsig */
    q->tsig_prepare_it = 0;
    q->tsig_update_it = 1;
    if (q->tsig_sign_it) {
        q->tsig_prepare_it = 1;
        q->tsig_sign_it = 0;
    }
    ods_log_assert(q->tsig_rr);
    ods_log_assert(q->zone);
    ods_log_assert(q->zone->name);
    if (q->axfr_fd == NULL) {
        /* start ixfr */
        xfrfile = ods_build_path(q->zone->name, ".ixfr", 0);
        q->axfr_fd = ods_fopen(xfrfile, NULL, "r");
        if (!q->axfr_fd) {
            ods_log_error("[%s] unable to open ixfr file %s for zone %s",
                axfr_str, xfrfile, q->zone->name);
            ods_log_info("[%s] axfr fallback zone %s", axfr_str,
                q->zone->name);
            free((void*)xfrfile);
            return axfr(q, engine);
        }
        free((void*)xfrfile);
        if (q->tsig_rr->status == TSIG_OK) {
            q->tsig_sign_it = 1; /* sign first packet in stream */
        }
        /* compression? */

        /* add soa rr */
        fpos = ftell(q->axfr_fd);
        rr = addns_read_rr(q->axfr_fd, line, &orig, &prev, &ttl, &status,
            &l);
        if (!rr) {
            /* no SOA no transfer */
            ods_log_error("[%s] bad ixfr zone %s, corrupted file",
                axfr_str, q->zone->name);
            buffer_pkt_set_rcode(q->buffer, LDNS_RCODE_SERVFAIL);
            return QUERY_PROCESSED;
        }
        if (ldns_rr_get_type(rr) != LDNS_RR_TYPE_SOA) {
            ods_log_error("[%s] bad ixfr zone %s, first rr is not soa",
                axfr_str, q->zone->name);
            ldns_rr_free(rr);
            buffer_pkt_set_rcode(q->buffer, LDNS_RCODE_SERVFAIL);
            return QUERY_PROCESSED;
        }
        /* newest serial */
        new_serial = ldns_rdf2native_int32(
            ldns_rr_rdf(rr, SE_SOA_RDATA_SERIAL));
        /* does it fit? */
        buffer_set_position(q->buffer, q->startpos);
        if (buffer_write_rr(q->buffer, rr)) {
            ods_log_debug("[%s] set soa in ixfr zone %s", axfr_str,
                q->zone->name);
            buffer_pkt_set_ancount(q->buffer, buffer_pkt_ancount(q->buffer)+1);
            total_added++;
            ldns_rr_free(rr);
            rr = NULL;
            bufpos = buffer_position(q->buffer);
        } else {
            ods_log_error("[%s] soa does not fit in ixfr zone %s",
                axfr_str, q->zone->name);
            ldns_rr_free(rr);
            rr = NULL;
            buffer_pkt_set_rcode(q->buffer, LDNS_RCODE_SERVFAIL);
            return QUERY_PROCESSED;
        }
        if (util_serial_gt(q->serial, new_serial)) {
            goto axfr_fallback;
        }
    } else if (q->tcp) {
        /* subsequent ixfr packets */
        ods_log_debug("[%s] subsequent ixfr packet zone %s", axfr_str,
            q->zone->name);
        buffer_set_limit(q->buffer, BUFFER_PKT_HEADER_SIZE);
        buffer_pkt_set_qdcount(q->buffer, 0);
        query_prepare(q);
    }

    /* add as many records as fit */
    fpos = ftell(q->axfr_fd);
    while ((rr = addns_read_rr(q->axfr_fd, line, &orig, &prev, &ttl,
        &status, &l)) != NULL) {
        ods_log_deeebug("[%s] read rr at line %d", axfr_str, l);
        if (status != LDNS_STATUS_OK) {
            ldns_rr_free(rr);
            rr = NULL;
            ods_log_error("[%s] error reading rr at line %i (%s): %s",
                axfr_str, l, ldns_get_errorstr_by_id(status), line);
            goto axfr_fallback;
        }
        if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_SOA) {
            del_mode = !del_mode;
        }
        if (!soa_found) {
            if (del_mode && ldns_rr_get_type(rr) == LDNS_RR_TYPE_SOA &&
                q->serial == ldns_rdf2native_int32(
                ldns_rr_rdf(rr, SE_SOA_RDATA_SERIAL))) {
                soa_found = 1;
            } else {
                ods_log_deeebug("[%s] soa not found for rr at line %d",
                    axfr_str, l);
                continue;
            }
        }
        /* does it fit? */
        if (buffer_write_rr(q->buffer, rr)) {
            ods_log_deeebug("[%s] add rr at line %d", axfr_str, l);
            fpos = ftell(q->axfr_fd);
            buffer_pkt_set_ancount(q->buffer, buffer_pkt_ancount(q->buffer)+1);
            total_added++;
            ldns_rr_free(rr);
            rr = NULL;
        } else {
            ods_log_deeebug("[%s] rr at line %d does not fit", axfr_str, l);
            ldns_rr_free(rr);
            rr = NULL;
            if (fseek(q->axfr_fd, fpos, SEEK_SET) != 0) {
                ods_log_error("[%s] unable to reset file position in ixfr "
                    "file: fseek() failed (%s)", axfr_str, strerror(errno));
                buffer_pkt_set_rcode(q->buffer, LDNS_RCODE_SERVFAIL);
                return QUERY_PROCESSED;
            } else if (q->tcp) {
                goto return_ixfr;
            } else {
                goto axfr_fallback;
            }
        }
    }
    if (!soa_found) {
        goto axfr_fallback;
    }
    ods_log_debug("[%s] ixfr zone %s is done", axfr_str, q->zone->name);
    q->tsig_sign_it = 1; /* sign last packet */
    q->axfr_is_done = 1;
    ods_fclose(q->axfr_fd);

return_ixfr:
    ods_log_debug("[%s] return part ixfr zone %s", axfr_str, q->zone->name);
    buffer_pkt_set_ancount(q->buffer, total_added);
    buffer_pkt_set_nscount(q->buffer, 0);
    buffer_pkt_set_arcount(q->buffer, 0);

    /* check if it needs tsig signatures */
    if (q->tsig_rr->status == TSIG_OK) {
        if (q->tsig_rr->update_since_last_prepare >= AXFR_TSIG_SIGN_EVERY_NTH) {
            q->tsig_sign_it = 1;
        }
    }
    return QUERY_IXFR;

axfr_fallback:
    buffer_set_position(q->buffer, q->startpos);
    if (q->tcp) {
        ods_log_info("[%s] axfr fallback zone %s", axfr_str, q->zone->name);
        if (q->axfr_fd) {
            ods_fclose(q->axfr_fd);
            q->axfr_fd = NULL;
        }
        return axfr(q, engine);
    }
    /* UDP Overflow */
    ods_log_info("[%s] ixfr udp overflow zone %s", axfr_str, q->zone->name);
    buffer_set_position(q->buffer, bufpos);
    buffer_pkt_set_ancount(q->buffer, 1);
    buffer_pkt_set_nscount(q->buffer, 0);
    buffer_pkt_set_arcount(q->buffer, 0);
    /* check if it needs tsig signatures */
    if (q->tsig_rr->status == TSIG_OK) {
        q->tsig_sign_it = 1;
    }
    return QUERY_PROCESSED;
}
