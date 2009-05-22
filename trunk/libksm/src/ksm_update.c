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

/*+
 * ksm_update.c - Update Times
 *
 * Description:
 *      Given a set of zones, this module updates all the estimated times in
 *      the keys associated with the zone.
 *
 *      The estimated times are updated using the current state of the key and
 *      the actual time the key entered that state.  The key is updated using
 *      the values of the various parameters.
 *
 *      SO FAR, THIS ONLY APPLIES TO ZSKS
-*/

#include <stdio.h>

#include "ksm/database.h"
#include "ksm/db_fields.h"
#include "ksm/debug.h"
#include "ksm/ksm.h"
#include "ksm/kmedef.h"
#include "ksm/ksmdef.h"
#include "ksm/message.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))


/*+
 * KsmUpdate - Update Times for Keys
 *
 * Description:
 *      Obtains the times for the specified zone and process each key in it.
 *
 * Arguments:
 *      None.
 *
 * Returns:
 *      int
 *          Always 0.
-*/

int KsmUpdate(int policy_id, int zone_id)
{
    KSM_PARCOLL         collection;     /* Collection of parameters for zone */
    KSM_KEYDATA         data;           /* Data about the key */
    DB_RESULT           result;         /* For iterating through keys */
    int                 status;         /* Status return */
    DQS_QUERY_CONDITION condition[2];       /* Condition codes */

    /* Get the values of the parameters */

    status = KsmParameterCollection(&collection, policy_id);
    if (status == 0) {

        /*
         * Iterate round, updating each key.  As always, an error causes a
         * message to be output, so we don't need to handle error conditions.
         * Abandon updates if the update of a single key fails.
         */

        /* zone_id of -1 means all zones */
        if (zone_id == -1) {
            status = KsmKeyInit(&result, NULL);
        } 
        else {
            condition[0].code = DB_KEYDATA_ZONE_ID;
            condition[0].data.number = zone_id;
            condition[0].compare = DQS_COMPARE_EQ;

            condition[1].compare = DQS_END_OF_LIST;

            status = KsmKeyInit(&result, condition);
        }

        if (status == 0) {
            /* Transaction handling is one level up (in KsmRequestKeys) */
            status = KsmKey(result, &data);
            while (status == 0) {
                (void) KsmUpdateKey(&data, &collection);
                status = KsmKey(result, &data);
            }
            (void) KsmKeyEnd(result);

            /* Change end of list status to a success */

            if (status == -1) {
                status = 0;
            }
        }
    }
    /*
     * else {
     *      Unable to get parameter collection information.  If we can't do
     *      this, something must be seriously wrong.
     * }
     */

    return status;
}


/*+
 * KsmUpdateKey - Update Key Times
 *
 * Description:
 *      Updates the estimated times in a key based on the current state and the
 *      parameters.
 *
 * Arguments:
 *      KSM_KEYDATA* data
 *          Key to update.
 *
 *      KSM_PARCOLL* collection
 *          Parameter collection.
-*/

void KsmUpdateKey(KSM_KEYDATA* data, KSM_PARCOLL* collection)
{
    /* check the argument */
    if (data == NULL) {
        MsgLog(KSM_INVARG, "NULL data");
        return;
    }

    switch (data->state) {
    case KSM_STATE_GENERATE:
        KsmUpdateGenerateKeyTime(data);
        break;

    case KSM_STATE_PUBLISH:
        KsmUpdatePublishKeyTime(data, collection);
        break;

    case KSM_STATE_READY:
        KsmUpdateReadyKeyTime(data);
        break;

    case KSM_STATE_ACTIVE:
        KsmUpdateActiveKeyTime(data, collection);
        break;

    case KSM_STATE_RETIRE:
        KsmUpdateRetireKeyTime(data, collection);
        break;

    case KSM_STATE_DEAD:
        KsmUpdateDeadKeyTime(data);
        break;

    default:

        /* Should not have a key in an unknown state */

        MsgLog(KME_UNRKEYSTA, (int) data->keypair_id, data->state);
    }

    return;
}


/*+
 * KsmUpdateXxxxKeyTime - Update Key Time for Key In Xxxx State
 *
 * Description:
 *      Handles the update of a key in the specified state.
 *
 * Arguments:id
 *      KSM_KEYDATA* data
 *          Key to update.
 *
-*/

void KsmUpdateGenerateKeyTime(KSM_KEYDATA* data)
{
    /*
     * Keys in the generated state don't automatically change their state -
     * they wait until a request is made to publish them.
     */

    /* check the argument */
    if (data == NULL) {
        MsgLog(KSM_INVARG, "NULL data");
        return;
    }
    DbgOutput(DBG_M_UPDATE, "Key ID %d in state 'generate' - not updated\n",
        (int) data->keypair_id);

    return;
}

void KsmUpdatePublishKeyTime(KSM_KEYDATA* data, KSM_PARCOLL* collection)
{
    int deltat;  /* Time interval */
    int Ipc;     /* Child zone publication interval */
    int Ipp;     /* Parent zone publication interval */

    /* check the argument */
    if (data == NULL || collection == NULL) {
        MsgLog(KSM_INVARG, "NULL argument");
        return;
    }
    DbgOutput(DBG_M_UPDATE, "Key ID %d in state 'publish' - updating\n",
        (int) data->keypair_id);

    Ipc = MAX(collection->zskttl, MIN(collection->soattl, collection->soamin)) +
            collection->propdelay + collection->pub_safety;
    if (data->keytype == KSM_TYPE_ZSK) {
    /*
     * A key in the "publish" state moves into the "ready" state when it has
     * been published for at least:
     *
     *      Ipc = max(TTLkeyc, Cc) + Dpc +Sp
     *
     * ... where:
     *
     *      TTLkeyc  = TTL of the ZSK DNSKEY record
     *      Cc       = SOA negative cache time, 
     *      Dpc      = Propagation delay
     *      Sp       = Publish Safety Margin
     *
     * The negative cache time is given as:
     *
     *      MIN(TTLsoa, SOAmin)
     *
     * ... where TTLsoa is the TTL of the SOA record and SOAmin is the value of
     * the "Minimum" field of the SOA record.
     */

        deltat = Ipc;
    }
    else if (data->keytype == KSM_TYPE_KSK) {
    /*
     * A key in the "publish" state moves into the "ready" state when it has
     * been published for at least:
     *
     *      max(Ipc, Ipp)
     *  where
     *      Ipp = max(TTLdsp, Cp) + Dpp + Dr +Sp
     *
     * ... where:
     *
     *      TTLdsp  = TTL of the DS record in the parent
     *      Cp      = SOA negative cache time (of parent), 
     *      Dpp     = Propagation delay
     *      Dr      = Registration delay
     *      Sp      = Publish Safety Margin
     *
     * The negative cache time is given as:
     *
     *      MIN(TTLsoa, SOAmin)
     *
     * ... where TTLsoa is the TTL of the SOA record and SOAmin is the value of
     * the "Minimum" field of the SOA record.
     */
    Ipp = MAX(collection->kskttl, MIN(collection->soattl, collection->soamin)) +
            collection->kskpropdelay + collection->regdelay + collection->pub_safety;

        deltat = MAX(Ipc, Ipp);

    }
    else {
        return;
    }

    (void) KsmUpdateKeyTime(data, "PUBLISH", "READY", deltat);

    return;
}

void KsmUpdateReadyKeyTime(KSM_KEYDATA* data)
{
    /*
     * Keys in the ready state don't automatically move into the active state.
     * They need to be explicitly activated.
     */

    /* check the argument */
    if (data == NULL) {
        MsgLog(KSM_INVARG, "NULL data");
        return;
    }
    DbgOutput(DBG_M_UPDATE, "Key ID %d in state 'ready' - not updated\n",
        (int) data->keypair_id);

    return;
}

void KsmUpdateActiveKeyTime(KSM_KEYDATA* data, KSM_PARCOLL* collection)
{
    int deltat;     /* Time interval */

    /* check the argument */
    if (data == NULL || collection == NULL) {
        MsgLog(KSM_INVARG, "NULL argument");
        return;
    }
    DbgOutput(DBG_M_UPDATE, "Key ID %d in state 'active' - updating\n",
        (int) data->keypair_id);

    /*
     * A key in the "active" state moves into the "retire" state when it has
     * been active for at least:
     *
     *          Lz
     *
     * ... where
     *
     *      Lz = Life time of a ZSK (i.e. how long it is used for)
     */

    if (data->keytype == KSM_TYPE_ZSK) {
        deltat = collection->zsklife;
    }
    else if (data->keytype == KSM_TYPE_KSK) {
        deltat = collection->ksklife;
    }
    else {
        return;
    }

    (void) KsmUpdateKeyTime(data, "ACTIVE", "RETIRE", deltat);

    return;
}

void KsmUpdateRetireKeyTime(KSM_KEYDATA* data, KSM_PARCOLL* collection)
{
    int deltat;     /* Time interval */

    /* check the argument */
    if (data == NULL || collection == NULL) {
        MsgLog(KSM_INVARG, "NULL argument");
        return;
    }
    DbgOutput(DBG_M_UPDATE, "Key ID %d in state 'retire' - updating\n",
        (int) data->keypair_id);

    /*
     * A key in the "retire" state moves into the "dead" state after a period
     * of:
     *
     *          TTLsig + Dp + St
     *
     * ... where
     *
     *      TTLsig  = Signature lifetime (how long a signature is valid for)
     *      Dp      = Propagation delay
     *      St      = Retire safety margin
     */

    if (data->keytype == KSM_TYPE_ZSK) {
        deltat = data->siglifetime + collection->propdelay + collection->ret_safety;
    }
    else if (data->keytype == KSM_TYPE_KSK) {
        /* for a KSK this can be 0; are we happy with that? Might revisit this in the future */
        deltat = 0;
    }
    else {
        return;
    }

    (void) KsmUpdateKeyTime(data, "RETIRE", "DEAD", deltat);

    return;
}

void KsmUpdateDeadKeyTime(KSM_KEYDATA* data)
{
    /*
     * Keys in the dead state don't automatically change their state - they
     * are retained in the database for historical reasons or until they are
     * explicitly deleted.
     */

    /* check the argument */
    if (data == NULL) {
        MsgLog(KSM_INVARG, "NULL data");
        return;
    }
    DbgOutput(DBG_M_UPDATE, "Key ID %d in state 'dead' - not updated\n",
        (int) data->keypair_id);

    return;
}


/*+
 * KsmUpdateKeyTime - Update Key Time
 *
 * Description:
 *      Actually performs the update of the database.  The update is
 *      
 *          destination_time = source_time + interval
 *
 * Arguments:
 *      const KSM_KEYDATA* data
 *          Data about the key to be updated.  Note that this is NOT updated
 *          by the update.
 *
 *      const char* source
 *          Source field.
 *
 *      const char* destination
 *          Source field.
 *
 *      int interval
 *          Interval (seconds) to update the source field with.
 *
 * Returns:
 *      int
 *          0       Update successful
 *          Other   Error.  A message will have beeen output.
-*/

int KsmUpdateKeyTime(const KSM_KEYDATA* data, const char* source,
    const char* destination, int interval)
{
    char            buffer[KSM_SQL_SIZE];    /* Long enough for any statement */
    unsigned int    nchar;          /* Number of characters converted */
    int             status;         /* Status return */

    /* check the argument */
    if (data == NULL || source == NULL || destination == NULL) {
        return MsgLog(KSM_INVARG, "NULL argument");
    }
#ifdef USE_MYSQL
    nchar = snprintf(buffer, sizeof(buffer),
        "UPDATE keypairs SET %s = %s + INTERVAL %d SECOND WHERE ID = %lu",
        destination, source, interval, (unsigned long) data->keypair_id);
#else
    nchar = snprintf(buffer, sizeof(buffer),
        "UPDATE keypairs SET %s = DATETIME(%s, '+%d SECONDS') WHERE ID = %lu",
        destination, source, interval, (unsigned long) data->keypair_id);
#endif /* USE_MYSQL */
    if (nchar < sizeof(buffer)) {

        /* All OK, execute the statement */

        status = DbExecuteSqlNoResult(DbHandle(), buffer);
    }
    else {

        /* Unable to create update statement */

        status = MsgLog(KME_BUFFEROVF, "KsmUpdateKeyTime");
    }

    return status;
}
