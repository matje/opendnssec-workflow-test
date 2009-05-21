/*
 * $Id$
 *
 * Copyright (c) 2008-2009 Nominet UK. All rights reserved.
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

/* 
 * communicator.c code implements the server_main
 * function needed by daemon.c
 *
 * The bit that makes the daemon do something useful
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>
#include "daemon.h"
#include "daemon_util.h"
#include "communicator.h"
#include "kaspaccess.h"
#include "ksm/memory.h"
#include "ksm/string_util2.h"
#include "ksm/datetime.h"
#include "config.h"

    int
server_init(DAEMONCONFIG *config)
{
    if (config == NULL) {
        log_msg(NULL, LOG_ERR, "Error, no config provided");
        exit(1);
    }

    /* set the default pidfile if nothing was provided on the command line*/
    if (config->pidfile == NULL) {
        config->pidfile = COM_PID;
    }

    return 0;
}

/*
 * Main loop of keygend server
 */
    void
server_main(DAEMONCONFIG *config)
{
    DB_RESULT handle;
    DB_RESULT handle2;
    DB_HANDLE	dbhandle;
    int status = 0;
    int status2 = 0;

    struct timeval tv;

    KSM_ZONE *zone;
    KSM_POLICY *policy;

    if (config == NULL) {
        log_msg(NULL, LOG_ERR, "Error, no config provided");
        exit(1);
    }

    policy = (KSM_POLICY *)malloc(sizeof(KSM_POLICY));
    policy->signer = (KSM_SIGNER_POLICY *)malloc(sizeof(KSM_SIGNER_POLICY));
    policy->signature = (KSM_SIGNATURE_POLICY *)malloc(sizeof(KSM_SIGNATURE_POLICY));
    policy->ksk = (KSM_KEY_POLICY *)malloc(sizeof(KSM_KEY_POLICY));
    policy->zsk = (KSM_KEY_POLICY *)malloc(sizeof(KSM_KEY_POLICY));
    policy->denial = (KSM_DENIAL_POLICY *)malloc(sizeof(KSM_DENIAL_POLICY));
    policy->enforcer = (KSM_ENFORCER_POLICY *)malloc(sizeof(KSM_ENFORCER_POLICY));
    policy->name = (char *)calloc(KSM_NAME_LENGTH, sizeof(char));
    /* Let's check all of those mallocs, or should we use MemMalloc ? */
    if (policy->signer == NULL || policy->signature == NULL ||
            policy->ksk == NULL || policy->zsk == NULL || 
            policy->denial == NULL || policy->enforcer == NULL) {
        log_msg(config, LOG_ERR, "Malloc for policy struct failed\n");
        exit(1);
    }
    kaspSetPolicyDefaults(policy, NULL);

    zone = (KSM_ZONE *)malloc(sizeof(KSM_ZONE));
    zone->name = (char *)calloc(KSM_ZONE_NAME_LENGTH, sizeof(char));
    /* Let's check those mallocs, or should we use MemMalloc ? */
    if (zone->name == NULL) {
        log_msg(config, LOG_ERR, "Malloc for zone struct failed\n");
        exit(1);
    }

    /* Read the config file */
    status = ReadConfig(config);
    if (status != 0) {
        log_msg(config, LOG_ERR, "Error reading config");
        exit(1);
    }

    kaspConnect(config, &dbhandle);

    while (1) {

        /* Read all policies */
        status = KsmPolicyInit(&handle, NULL);
        if (status == 0) {
            /* get the first policy */
            status = KsmPolicy(handle, policy);
            while (status == 0) {
                KsmPolicyRead(policy);
                log_msg(config, LOG_INFO, "Policy %s found.", policy->name);

                /* Update the salt if it is not up to date */
                status2 = KsmPolicyUpdateSalt(policy);
                if (status2 != 0) {
                    log_msg(config, LOG_ERR, "Error updating salt");
                    exit(1);
                }

                /* Got one; loop round zones on this policy */
                status2 = KsmZoneInit(&handle2, policy->id);
                if (status2 == 0) {
                    /* get the first zone */
                    status2 = KsmZone(handle2, zone);
                    while (status2 == 0) {
                        log_msg(config, LOG_INFO, "zone %s found.", zone->name);

                        /* turn this zone and policy into a file */
                        status2 = commGenSignConf(zone, policy);
                        if (status2 != 0) {
                            log_msg(config, LOG_ERR, "Error writing signconf");
                            exit(1);
                        }

                        /* get next zone */
                        status2 = KsmZone(handle2, zone);
                    }
                    /* get next policy */
                    status = KsmPolicy(handle, policy);
                }
                else
                {
                    log_msg(config, LOG_ERR, "Error querying KASP DB for zones");
                    exit(1);
                }
            }
        } else {
            log_msg(config, LOG_ERR, "Error querying KASP DB for policies");
            exit(1);
        }
        DbFreeResult(handle);

        /* sleep for the key gen interval unless we are in debug mode */
        if (config->debug)
        {
            break;
        }
        /* sleep for a bit */
        tv.tv_sec = config->interval;
        tv.tv_usec = 0;
        log_msg(config, LOG_INFO, "Sleeping for %i seconds.",config->interval);
        select(0, NULL, NULL, NULL, &tv);

        /* re-read the config file in case it has changed */
        status = ReadConfig(config);
        if (status != 0) {
            log_msg(config, LOG_ERR, "Error reading config");
            exit(1);
        }

    }
    kaspDisconnect(&dbhandle);
    free(policy->name);
    free(policy->enforcer);
    free(policy->denial);
    free(policy->zsk);
    free(policy->ksk);
    free(policy->signature);
    free(policy->signer);
    free(policy);

    free(zone->name);
    free(zone);
}

int commGenSignConf(KSM_ZONE *zone, KSM_POLICY *policy)
{
    int status = 0;
    FILE *file;
    char *filename;         /* This is the final filename */
    char *temp_filename;    /* In case this fails we write to a temp file and only overwrite
                               the current file when we are finished */
    char *old_filename;     /* Keep a copy of the previous version, just in case! (Also gets
                               round potentially different behaviour of rename over existing
                               file.) */
    char*   datetime = DtParseDateTimeString("now");

    if (zone == NULL || policy == NULL)
    {
        /* error */
        log_msg(NULL, LOG_ERR, "NULL policy or zone provided\n");
        MemFree(datetime);
        return -1;
    }

    filename = NULL;
    StrAppend(&filename, SIGNER_CONF_DIR); /* set at config time */
    StrAppend(&filename, zone->name);
    StrAppend(&filename, ".xml");

    old_filename = NULL;
    StrAppend(&old_filename, filename);
    StrAppend(&old_filename, ".OLD");

    temp_filename = NULL;
    StrAppend(&temp_filename, filename);
    StrAppend(&temp_filename, ".tmp");

    file = fopen(temp_filename, "w");

    if (file == NULL)
    {
        /* error */
        log_msg(NULL, LOG_ERR, "Could not open: %s\n", temp_filename);
        MemFree(datetime);
        return -1;
    }

    fprintf(file, "<SignerConfiguration>\n");
    fprintf(file, "\t<Zone name=\"%s\">\n", zone->name);

    fprintf(file, "\t\t<Signatures>\n");
    fprintf(file, "\t\t\t<Resign>PT%dS</Resign>\n", policy->signature->resign);
    fprintf(file, "\t\t\t<Refresh>PT%dS</Refresh>\n", policy->signer->refresh);
    fprintf(file, "\t\t\t<Validity>\n");
    fprintf(file, "\t\t\t\t<Default>PT%dS</Default>\n", policy->signature->valdefault);
    fprintf(file, "\t\t\t\t<Denial>PT%dS</Denial>\n", policy->signature->valdenial);
    fprintf(file, "\t\t\t</Validity>\n");
    fprintf(file, "\t\t\t<Jitter>PT%dS</Jitter>\n", policy->signer->jitter);
    fprintf(file, "\t\t\t<InceptionOffset>PT%dS</InceptionOffset>\n", policy->signature->clockskew);
    fprintf(file, "\t\t</Signatures>\n");

    fprintf(file, "\n");

    fprintf(file, "\t\t<Denial>\n");
    fprintf(file, "\t\t\t<NSEC%d>\n", policy->denial->version);
    if (policy->denial->version == 3)
    {
        fprintf(file, "\t\t\t\t<OptOut />\n");
        fprintf(file, "\t\t\t\t<Hash>\n");
        fprintf(file, "\t\t\t\t\t<Algorithm>%d</Algorithm>\n", policy->denial->algorithm);
        fprintf(file, "\t\t\t\t\t<Iterations>%d</Iterations>\n", policy->denial->iteration);
        fprintf(file, "\t\t\t\t</Hash>\n");
    }
    fprintf(file, "\t\t\t</NSEC%d>\n", policy->denial->version);
    fprintf(file, "\t\t</Denial>\n");

    fprintf(file, "\n");

    /* start of keys section */ 
    fprintf(file, "\t\t<Keys>\n");
    fprintf(file, "\t\t\t<TTL>PT%dS</TTL>\n", policy->ksk->ttl);

    KsmRequestKeys(0, 0, datetime, commKeyConfig, file, policy->id, zone->id);

    fprintf(file, "\t\t</Keys>\n");

    fprintf(file, "\n");

    fprintf(file, "\t\t<SOA>\n");
    fprintf(file, "\t\t\t<TTL>PT%dS</TTL>\n", policy->signer->soattl);
    fprintf(file, "\t\t\t<Minimum>PT%dS</Minimum>\n", policy->signer->soamin);
    fprintf(file, "\t\t\t<Serial>%s</Serial>\n", KsmKeywordSerialValueToName( policy->signer->serial) );
    fprintf(file, "\t\t</SOA>\n");

    fprintf(file, "\t</Zone>\n");
    fprintf(file, "</SignerConfiguration>\n");

    status = fclose(file);

    if (status == EOF) /* close failed... do something? */
    {
        MemFree(datetime);
        return -1;
    }

    /* we now have a complete xml file. First move the old one out of the way */
    status = rename(filename, old_filename);
    if (status != 0 && status != -1)
    {
        /* cope with initial conditioin of files not existing */
        MemFree(datetime);
        return -1;
    }

    /* Then copy our temp into place */
    if (rename(temp_filename, filename) != 0)
    {
        MemFree(datetime);
        return -1;
    }

    MemFree(datetime);
    return 0;
}

/*
 * CallBack to print key info in signerConfiguration
 */

int commKeyConfig(void* context, KSM_KEYDATA* key_data)
{
    FILE *file = (FILE *)context;

    fprintf(file, "\t\t\t<Key>\n");
    fprintf(file, "\t\t\t\t<Flags>%d</Flags>\n", key_data->keytype);
    fprintf(file, "\t\t\t\t<Algorithm>%d</Algorithm>\n", key_data->algorithm);
    fprintf(file, "\t\t\t\t<Locator>%s</Locator>\n", key_data->location);
    if (key_data->keytype == KSM_TYPE_KSK)
    {
        fprintf(file, "\t\t\t\t<KSK />\n");
    }
    else
    {
        fprintf(file, "\t\t\t\t<ZSK />\n");
    }
    fprintf(file, "\t\t\t\t<%s />\n", KsmKeywordStateValueToName(key_data->state));
    fprintf(file, "\t\t\t</Key>\n");
    fprintf(file, "\n");

    return 0;
}

