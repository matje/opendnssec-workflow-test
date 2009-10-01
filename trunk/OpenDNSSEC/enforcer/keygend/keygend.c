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
 * keygend.c code implements the server_main
 * function needed by daemon.c
 *
 * The bit that makes the daemon do something useful
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>

#include <libxml/parser.h>

#include "daemon.h"
#include "daemon_util.h"
#include "kaspaccess.h"

#include "ksm/datetime.h"
#include "ksm/string_util.h"
#include "ksm/string_util2.h"

#include "libhsm.h"
#include "keygend.h" 

int
server_init(DAEMONCONFIG *config)
{
    if (config == NULL) {
        log_msg(NULL, LOG_ERR, "Error in server_init, no config provided");
        exit(1);
    }

    /* set the default pidfile if nothing was provided on the command line*/
    if (config->pidfile == NULL) {
        config->pidfile = KEYGEN_PIDFILE;
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
    DB_HANDLE dbhandle;
    int status = 0;
    int i = 0; 
    char *id;
    char *rightnow; 
    DB_ID ignore = 0;
    struct timeval tv;
    KSM_POLICY *policy;
    int result;
    hsm_ctx_t *ctx = NULL;
    hsm_key_t *key = NULL;
    char *hsm_error_message = NULL;
    
    FILE *lock_fd = NULL;  /* for sqlite file locking */
    char *lock_filename = NULL;

    int ksks_needed = 0;    /* Total No of ksks needed before next generation run */
    int zsks_needed = 0;    /* Total No of zsks needed before next generation run */
    int keys_in_queue = 0;  /* number of unused keys */
    int new_keys = 0;       /* number of keys required */

    int same_keys = 0;      /* Do ksks and zsks look the same ? */
   
    if (config == NULL) {
        log_msg(NULL, LOG_ERR, "Error in server_main, no config provided");
        exit(1);
    }

    policy = (KSM_POLICY *)malloc(sizeof(KSM_POLICY));
    policy->signer = (KSM_SIGNER_POLICY *)malloc(sizeof(KSM_SIGNER_POLICY));
    policy->signature = (KSM_SIGNATURE_POLICY *)malloc(sizeof(KSM_SIGNATURE_POLICY));
    policy->zone = (KSM_ZONE_POLICY *)malloc(sizeof(KSM_ZONE_POLICY));
    policy->parent = (KSM_PARENT_POLICY *)malloc(sizeof(KSM_PARENT_POLICY));
    policy->keys = (KSM_COMMON_KEY_POLICY *)malloc(sizeof(KSM_COMMON_KEY_POLICY));
    policy->ksk = (KSM_KEY_POLICY *)malloc(sizeof(KSM_KEY_POLICY));
    policy->zsk = (KSM_KEY_POLICY *)malloc(sizeof(KSM_KEY_POLICY));
    policy->denial = (KSM_DENIAL_POLICY *)malloc(sizeof(KSM_DENIAL_POLICY));
    policy->enforcer = (KSM_ENFORCER_POLICY *)malloc(sizeof(KSM_ENFORCER_POLICY));
/*    policy->audit = (KSM_AUDIT_POLICY *)malloc(sizeof(KSM_AUDIT_POLICY)); */
    policy->audit = (char *)calloc(KSM_POLICY_AUDIT_LENGTH, sizeof(char));
    policy->name = (char *)calloc(KSM_NAME_LENGTH, sizeof(char));
    policy->description = (char *)calloc(KSM_POLICY_DESC_LENGTH, sizeof(char));
    /* Let's check all of those mallocs, or should we use MemMalloc ? */
    if (policy->signer == NULL || policy->signature == NULL ||
            policy->zone == NULL || policy->parent == NULL ||
            policy->keys == NULL ||
            policy->ksk == NULL || policy->zsk == NULL || 
            policy->denial == NULL || policy->enforcer == NULL ||
            policy->audit == NULL) {
        log_msg(config, LOG_ERR, "Malloc for policy struct failed");
        unlink(config->pidfile);
        exit(1);
    }
    kaspSetPolicyDefaults(policy, NULL);
   
    /* We keep the HSM connection open for the lifetime of the daemon */ 
    result = hsm_open(CONFIG_FILE, hsm_prompt_pin, NULL);
    if (result) {
        hsm_error_message = hsm_get_error(ctx);
        if (hsm_error_message) {
            log_msg(config, LOG_ERR, hsm_error_message);
            free(hsm_error_message);
        } else {
            /* decode the error code ourselves 
               TODO find if there is a better way to do this (and can all of these be returned? are there others?) */
            switch (result) {
                case HSM_ERROR:
                    log_msg(config, LOG_ERR, "hsm_open() result: HSM error");
                    break;
                case HSM_PIN_INCORRECT:
                    log_msg(config, LOG_ERR, "hsm_open() result: incorrect PIN");
                    break;
                case HSM_CONFIG_FILE_ERROR:
                    log_msg(config, LOG_ERR, "hsm_open() result: config file error");
                    break;
                case HSM_REPOSITORY_NOT_FOUND:
                    log_msg(config, LOG_ERR, "hsm_open() result: repository not found");
                    break;
                case HSM_NO_REPOSITORIES:
                    log_msg(config, LOG_ERR, "hsm_open() result: no repositories");
                    break;
                default:
                    log_msg(config, LOG_ERR, "hsm_open() result: %d", result);
            }
        }
        exit(1);
    }
    log_msg(config, LOG_INFO, "HSM opened successfully.");
    ctx = hsm_create_context();

    while (1) {

        /* Read the config file */
        status = ReadConfig(config, 1);
        if (status != 0) {
            log_msg(config, LOG_ERR, "Error reading config");
            unlink(config->pidfile);
            exit(1);
        }

        /* If we are in sqlite mode then take a lock out on a file to
           prevent multiple access (not sure that we can be sure that sqlite is
           safe for multiple processes to access). */
        if (DbFlavour() == SQLITE_DB) {

            /* set up lock filename (it may have changed?) */
            lock_filename = NULL;
            StrAppend(&lock_filename, (char *)config->schema);
            StrAppend(&lock_filename, ".our_lock");

            lock_fd = fopen(lock_filename, "w");
            status = get_lite_lock(lock_filename, lock_fd);
            StrFree(lock_filename);
            if (status != 0) {
                log_msg(config, LOG_ERR, "Error getting db lock");
                unlink(config->pidfile);
                exit(1);
            }
        }

        log_msg(config, LOG_INFO, "Connecting to Database...");
        kaspConnect(config, &dbhandle);

        /* Read all policies */
        status = KsmPolicyInit(&handle, NULL);
        if (status == 0) {
            /* get the first policy */
            status = KsmPolicy(handle, policy);
            while (status == 0) {
                log_msg(config, LOG_INFO, "Policy %s found.", policy->name);
                /* Clear the policy struct */
                kaspSetPolicyDefaults(policy, NULL);

                /* Read the parameters for that policy */
                status = kaspReadPolicy(policy);

                if  (policy->shared_keys == 1 ) {
                    log_msg(config, LOG_INFO, "Key sharing is On");
                } else {
                    log_msg(config, LOG_INFO, "Key sharing is Off.");
                }

                rightnow = DtParseDateTimeString("now");

                /* Check datetime in case it came back NULL */
                if (rightnow == NULL) {
                    log_msg(config, LOG_DEBUG, "Couldn't turn \"now\" into a date, quitting...");
                    exit(1);
                }

                if (policy->ksk->sm == policy->zsk->sm && policy->ksk->bits == policy->zsk->bits && policy->ksk->algorithm == policy->zsk->algorithm) {
                    same_keys = 1;
                } else {
                    same_keys = 0;
                }
                /* Find out how many ksk keys are needed for the POLICY */
                status = KsmKeyPredict(policy->id, KSM_TYPE_KSK, policy->shared_keys, config->keygeninterval, &ksks_needed);
                if (status != 0) {
                    log_msg(NULL, LOG_ERR, "Could not predict ksk requirement for next interval for %s", policy->name);
                    /* TODO exit? continue with next policy? */
                }
                /* Find out how many suitable keys we have */
                status = KsmKeyCountStillGood(policy->id, policy->ksk->sm, policy->ksk->bits, policy->ksk->algorithm, config->keygeninterval, rightnow, &keys_in_queue);
                if (status != 0) {
                    log_msg(NULL, LOG_ERR, "Could not count current ksk numbers for policy %s", policy->name);
                    /* TODO exit? continue with next policy? */
                }

                new_keys = ksks_needed - keys_in_queue;
    /* fprintf(stderr, "keygen(ksk): new_keys(%d) = keys_needed(%d) - keys_in_queue(%d)\n", new_keys, ksks_needed, keys_in_queue); */

                /* TODO: check capacity of HSM will not be exceeded */
                /* Create the required keys */
                for (i=new_keys ; i > 0 ; i--){
                        if (policy->ksk->algorithm == 5 || policy->ksk->algorithm == 7 ) {
                            key = hsm_generate_rsa_key(ctx, policy->ksk->sm_name, policy->ksk->bits);
                            if (key) {
                                /* log_msg(config, LOG_INFO,"Created key in HSM"); */
                            } else {
                                log_msg(config, LOG_ERR,"Error creating key in repository %s", policy->ksk->sm_name);
                                hsm_error_message = hsm_get_error(ctx);
                                if (hsm_error_message) {
                                    log_msg(config, LOG_ERR, hsm_error_message);
                                    free(hsm_error_message);
                                }
                                unlink(config->pidfile);
                                exit(1);
                            }
                            id = hsm_get_key_id(ctx, key);
                            status = KsmKeyPairCreate(policy->id, id, policy->ksk->sm, policy->ksk->bits, policy->ksk->algorithm, rightnow, &ignore);
                            if (status != 0) {
                                log_msg(config, LOG_ERR,"Error creating key in Database");
                                hsm_error_message = hsm_get_error(ctx);
                                if (hsm_error_message) {
                                    log_msg(config, LOG_ERR, hsm_error_message);
                                    free(hsm_error_message);
                                }
                                unlink(config->pidfile);
                                exit(1);
                            }
                            log_msg(config, LOG_INFO, "Created KSK size: %i, alg: %i with id: %s in HSM: %s and database.", policy->ksk->bits,
                                    policy->ksk->algorithm, id, policy->ksk->sm_name);
                            free(id);
                        } else {
                            log_msg(config, LOG_ERR, "Key algorithm %d unsupported by libhsm.", policy->ksk->algorithm);
                            unlink(config->pidfile);
                            exit(1);
                        }
                 }

                /* Find out how many zsk keys are needed */
                keys_in_queue = 0;
                new_keys = 0;

                /* Find out how many zsk keys are needed for the POLICY */
                status = KsmKeyPredict(policy->id, KSM_TYPE_ZSK, policy->shared_keys, config->keygeninterval, &zsks_needed);
                if (status != 0) {
                    log_msg(NULL, LOG_ERR, "Could not predict zsk requirement for next intervalfor %s", policy->name);
                    /* TODO exit? continue with next policy? */
                }
                /* Find out how many suitable keys we have */
                status = KsmKeyCountStillGood(policy->id, policy->zsk->sm, policy->zsk->bits, policy->zsk->algorithm, config->keygeninterval, rightnow, &keys_in_queue);
                if (status != 0) {
                    log_msg(NULL, LOG_ERR, "Could not count current zsk numbers for policy %s", policy->name);
                    /* TODO exit? continue with next policy? */
                }
                /* Might have to account for ksks */
                if (same_keys) {
                    keys_in_queue -= ksks_needed;
                }

                new_keys = zsks_needed - keys_in_queue;
    /* fprintf(stderr, "keygen(zsk): new_keys(%d) = keys_needed(%d) - keys_in_queue(%d)\n", new_keys, zsks_needed, keys_in_queue); */
                
                /* TODO: check capacity of HSM will not be exceeded */
                for (i = new_keys ; i > 0 ; i--) {
                   if (policy->zsk->algorithm == 5 || policy->zsk->algorithm == 7) {
                       key = hsm_generate_rsa_key(ctx, policy->zsk->sm_name, policy->zsk->bits);
                       if (key) {
                           /* log_msg(config, LOG_INFO,"Created key in HSM"); */
                       } else {
                           log_msg(config, LOG_ERR,"Error creating key in repository %s", policy->zsk->sm_name);
                           hsm_error_message = hsm_get_error(ctx);
                           if (hsm_error_message) {
                               log_msg(config, LOG_ERR, hsm_error_message);
                               free(hsm_error_message);
                           }
                           unlink(config->pidfile);
                           exit(1);
                       }
                       id = hsm_get_key_id(ctx, key);
                       status = KsmKeyPairCreate(policy->id, id, policy->zsk->sm, policy->zsk->bits, policy->zsk->algorithm, rightnow, &ignore);
                       if (status != 0) {
                           log_msg(config, LOG_ERR,"Error creating key in Database");
                           hsm_error_message = hsm_get_error(ctx);
                           if (hsm_error_message) {
                               log_msg(config, LOG_ERR, hsm_error_message);
                               free(hsm_error_message);
                           }
                           unlink(config->pidfile);
                           exit(1);
                       }
                       log_msg(config, LOG_INFO, "Created ZSK size: %i, alg: %i with id: %s in HSM: %s and database.", policy->zsk->bits,
                               policy->zsk->algorithm, id, policy->zsk->sm_name);
                       free(id);
                    } else {
                        log_msg(config, LOG_ERR, "Key algorithm %d unsupported by libhsm.", policy->zsk->algorithm);
                        unlink(config->pidfile);
                       exit(1);
                    }
                }
                StrFree(rightnow);

                /* get next policy */
                status = KsmPolicy(handle, policy);
            }
        } else {
            log_msg(config, LOG_ERR, "Error querying KASP DB for policies.");
            unlink(config->pidfile);
            exit(1);
        }
        DbFreeResult(handle);

        /* Disconnect from DB in case we are asleep for a long time */
        log_msg(config, LOG_INFO, "Disconnecting from Database...");
        kaspDisconnect(&dbhandle);

         /* Release sqlite lock file (if we have it) */
        if (DbFlavour() == SQLITE_DB) {
            status = release_lite_lock(lock_fd);
            if (status != 0) {
                log_msg(config, LOG_ERR, "Error releasing db lock");
                unlink(config->pidfile);
                exit(1);
            }
            fclose(lock_fd);
        }

        if (config->once == true ){
            log_msg(config, LOG_INFO, "Running once only, exiting...");
            break;
        }

        /* If we have been sent a SIGTERM then it is time to exit */
        if (config->term == 1 ){
            log_msg(config, LOG_INFO, "Received SIGTERM, exiting...");
            break;
        }
        /* Or SIGINT */
        if (config->term == 2 ){
            log_msg(config, LOG_INFO, "Received SIGINT, exiting...");
            break;
        }

        /* sleep for the key gen interval */
        tv.tv_sec = config->keygeninterval;
        tv.tv_usec = 0;
        log_msg(config, LOG_INFO, "Sleeping for %i seconds.",config->keygeninterval);
        select(0, NULL, NULL, NULL, &tv);

        /* If we have been sent a SIGTERM then it is time to exit */
        if (config->term == 1 ){
            log_msg(config, LOG_INFO, "Received SIGTERM, exiting...");
            break;
        }
        /* Or SIGINT */
        if (config->term == 2 ){
            log_msg(config, LOG_INFO, "Received SIGINT, exiting...");
            break;
        }
    }

    /*
     * Destroy HSM context
     */
    if (ctx) {
      hsm_destroy_context(ctx);
    }

    result = hsm_close();
    log_msg(config, LOG_INFO, "all done! hsm_close result: %d", result);

    KsmPolicyFree(policy);

    unlink(config->pidfile);

    xmlCleanupParser();

}
