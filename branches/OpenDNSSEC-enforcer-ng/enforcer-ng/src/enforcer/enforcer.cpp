/* 
 * 
 * @section DESCRIPTION
 * 
 * This module controls the order and time for keys to be introduced,
 * generated and discarded. It can be called for one zone at a time. It
 * will then manipulate its keys and bring them closer to their goal, 
 * within bounds of the zone's policy. New keys may be fetched from the
 * HSM and old keys discarded. When done, the update function returns 
 * a time at which it need to be called again for this zone. Unless 
 * there is an unscheduled change for this zone (some user input) it 
 * should not be called sooner. Calling sooner is not harmful in any
 * way but also not effective. Calling later does not do any harm as 
 * well, however rollovers may be delayed.
 * */

#include <ctime>
#include <iostream>

#include "enforcer/enforcer.h"
#include "enforcer/enforcerdata.h"
#include "policy/kasp.pb.h"

/* Interface of this cpp file is used by C code, we need to declare
 * extern "C" to prevent linking errors. */
extern "C" {
	#include "shared/duration.h"
	#include "shared/log.h"
}

using namespace std;
using ::ods::kasp::Policy;
using ::ods::kasp::Keys;

static const char *module_str = "enforcer";

/* be careful changing this, might mess up database*/
enum STATE {HID, RUM, OMN, UNR, NOCARE}; 
static const char* STATENAMES[] = {"HID", "RUM", "OMN", "UNR"};
enum RECORD {REC_MIN, DS = REC_MIN, DK, RD, RS, REC_MAX};
/* trick to loop over our RECORD enum */
RECORD& operator++(RECORD& r){return r = (r >= REC_MAX)?REC_MAX:RECORD(r+1);}
static const char* RECORDAMES[] = {"DS", "DNSKEY", "RRSIG DNSKEY", "RRSIG"};
/* \careful */

/* When no key available wait this many seconds before asking again. */
#define NOKEY_TIMEOUT 60
/* TODO: Temporary placeholder, must figure this out from policy. */
#define ALLOW_UNSIGNED false

/**
 * Stores the minimum of parm1 and parm2 in parm2.
 * 
 * Stores smallest of two times in min. Avoiding negative values,
 * which mean no update necessary. Any other time in the past: ASAP.
 * 
 * @param t, some time to test
 * \param min, smallest of t and min.
 * */
inline void 
minTime(const time_t t, time_t &min)
{
	if ( (t < min || min < 0) && t >= 0 ) min = t;
}

/**
 * Adds seconds to a time. 
 * 
 * Adds seconds to a time. Adding seconds directly is not portable.
 * 
 * @param t, base time
 * @param seconds, seconds to add to base
 * @return sum
 * */
time_t
addtime(const time_t &t, const int seconds)
{
	struct tm *tp = localtime(&t);
	tp->tm_sec += seconds;
	return mktime(tp);
}

/**
 * Retrieve a KeyState structure for one of the records of a key.
 * 
 * @param key, key to get the keystate from.
 * @param record, specifies which keystate.
 * @return keystate
 * */
KeyState&
getRecord(KeyData &key, const RECORD record)
{
	const char *scmd = "getRecord";
	switch(record) {
		case DS: return key.keyStateDS();
		case DK: return key.keyStateDNSKEY();
		case RD: return key.keyStateRRSIGDNSKEY();
		case RS: return key.keyStateRRSIG();
		default: 
			ods_fatal_exit("[%s] %s Unknown record type (%d), "
				"fault of programmer. Abort.", 
				module_str, scmd, (int)record);
	}
}

/**
 * Update the state of a record. Save the time of this change for
 * later use.
 * 
 * @param key
 * @param record
 * @param state
 * @param now, the current time.
 * */
void
setState(KeyData &key, const RECORD record, const STATE state, 
	const time_t now)
{
	KeyState &ks = getRecord(key, record);
	ks.setState(state);
	ks.setLastChange(now);

}

/**
 * Return state of a record.
 * 
 * @param key
 * @param record
 * @return state of record.
 * */
inline STATE
getState(KeyData &key, const RECORD record)
{
	return (STATE)getRecord(key, record).state();
}

/**
 * Given goal and state, what will be the next state?
 * 
 * This is an implementation of our state diagram. State indicates
 * our current node and goal helps decide which edge to choose. 
 * Input state and return state me be the same: the record is said
 * to be stable.
 * 
 * @param introducing, movement direction of key.
 * @param state, current state of record.
 * @return next state
 * */
STATE
getDesiredState(const bool introducing, const STATE state)
{
	const char *scmd = "getDesiredState";
	if (state > NOCARE || state < HID) 
		ods_fatal_exit("[%s] %s Key in unknown state (%d), "
			"Corrupt database? Abort.",  module_str, scmd, (int)state);
	const STATE jmp[2][5] = {{HID, UNR, UNR, HID, NOCARE}, {RUM, OMN, OMN, RUM, NOCARE}};
	return jmp[introducing][(int)state];
}

/**
 * Make sure records are introduced in correct order.
 * 
 * Make sure records are introduced in correct order. Only look at the 
 * policy, timing and validity is done in another function.
 * 
 * @param key
 * @param record
 * @param next_state 
 * @return True IFF policy allows transition of record to state.
 * */
bool
policy_approval(KeyData &key, const RECORD record, const STATE next_state)
{
	const char *scmd = "getDesiredState";
	
	/** once the record is introduced the policy has no influence. */
	if (next_state != RUM) return true;
	
	switch(record) {
		case DS:
			/** If we want to minimize the DS transitions make sure
			 * the DNSKEY is fully propagated. */
			return !key.keyStateDS().minimize() || 
				getState(key, DK) == OMN;
		case DK:
			/** the DS *and* the RRSIGs must be fully propagated. */
			return !key.keyStateDNSKEY().minimize() || 
				(getState(key, DS) == OMN && getState(key, RS) == OMN);
		case RD:
			/** The only time not to introduce RRSIG DNSKEY is when the
			 * DNSKEY is still hidden. */
			return getState(key, DK) != HID;
		case RS:
			/** DNSKEY must be hidden. */
			return !key.keyStateRRSIG().minimize() || 
				getState(key, DK) == OMN;
		default: 
			ods_fatal_exit("[%s] %s Unknown record type (%d), "
				"fault of programmer. Abort.",
				module_str, scmd, (int)record);
	}
}

/**
 * Test if a key exist with certain states.
 * 
 * @param key_list, list to search in.
 * @param key, key to compare with
 * @param record, record of said key to compare with
 * @param next_state, desired state of said record. Required if 
 * 			pretend_update is set.
 * @param require_same_algorithm, search for keys with the same
 * 			algorithm as input key, else any algorithm.
 * @param pretend_update, pretend record of key is in state next_state.
 * @param mask, The states to look for in a key. respectively DS, 
 * 			DNSKEY, RRSIG DNSKEY and RRSIG state. NOCARE for a record
 * 			if any will do.
 * @return True IFF exist such key.
 * */
bool
exists(KeyDataList &key_list, KeyData &key, 
	const RECORD record,const STATE next_state, 
	const bool require_same_algorithm, const bool pretend_update, 
	const STATE mask[4])
{
		for (int i = 0; i < key_list.numKeys(); i++) {
			KeyData &k = key_list.key(i);
			if (require_same_algorithm && k.algorithm() != key.algorithm())
				continue;
			/** Do be need to substitute a state of this key with 
			 * next_state? */
			bool sub_key = pretend_update && !key.locator().compare(k.locator());
			bool match = true;
			for (RECORD r = REC_MIN; r < REC_MAX; ++r) {
				/** Do we need to substitute the state of THIS record? */
				bool sub_rec = sub_key && record == r;
				if (mask[r] == NOCARE) continue;
				/** Use actual state or next state */
				STATE state = sub_rec?next_state:getState(k, r);
				/** no match in this record, try next key */
				if (mask[r] != state) {
					match = false;
					break;
				}
			}
			if (match) return true;
		}
	return false;
}

/**
 * Test if all keys are in a good unsigned state.
 * 
 * @param key_list, list to search in.
 * @param key, key to compare with
 * @param record, record of said key to compare with
 * @param next_state, desired state of said record. Required if 
 * 			pretend_update is set.
 * @param pretend_update, pretend record of key is in state next_state.
 * @param mask, The states to look for in a key. respectively DS, 
 * 			DNSKEY, RRSIG DNSKEY and RRSIG state. NOCARE for a record
 * 			if any will do.
 * @param mustHID, the record which must be HIDDEN for each key, 
 * 			otherwise mask must apply.
 * @return True IFF all keys are securely insecure.
 * */
bool
unsigned_ok(KeyDataList &key_list, KeyData &key, const RECORD record, 
	const STATE next_state, const bool pretend_update, 
	const STATE mask[4], const RECORD mustHID)
{
	for (int i = 0; i < key_list.numKeys(); i++) {
		KeyData &k = key_list.key(i);
		if (k.algorithm() != key.algorithm()) continue;
		bool substitute = pretend_update && 
			!key.locator().compare(k.locator());
		
		STATE cmp_msk[4];
		for (RECORD r = REC_MIN; r < REC_MAX; ++r) {
			/** all records should satisfy mask */
			if (r != mustHID) 
				cmp_msk[r] = mask[r];
			/** except mustHid, which should be hidden.
			 * MustHid=record=r: pretend update */
			else if (substitute && record==r)
				cmp_msk[r] = next_state;
			else
				cmp_msk[r] = getState(k, r);
		}
		/** If state is hidden this key is okay. */
		if (cmp_msk[mustHID] == HID || cmp_msk[mustHID] == NOCARE) 
			continue;
		/** Otherwise, we must test mask */
		if (!exists(key_list, key, record, next_state, true, 
			pretend_update, cmp_msk))
			return false;
	}
	return true;
}

/** 
 * Checks for existence of DS.
 * 
 * @param key_list, list to search in.
 * @param key, key to compare with
 * @param record, record of said key to compare with
 * @param next_state, desired state of said record. Required if 
 * 			pretend_update is set.
 * @param pretend_update, pretend record of key is in state next_state.
 * @return True IFF a introducing DS exists.
 * */
bool
rule1(KeyDataList &key_list, KeyData &key, const RECORD record, 
	const STATE next_state, const bool pretend_update)
{
	const STATE mask_triv[] =  {OMN, NOCARE, NOCARE, NOCARE};
	const STATE mask_dsin[] =  {RUM, NOCARE, NOCARE, NOCARE};
	
	return  
		exists(key_list, key, record, next_state, false, pretend_update, mask_triv) ||
		exists(key_list, key, record, next_state, false, pretend_update, mask_dsin);
}

/** 
 * Checks for a valid DNSKEY situation.
 * 
 * @param key_list, list to search in.
 * @param key, key to compare with
 * @param record, record of said key to compare with
 * @param next_state, desired state of said record. Required if 
 * 			pretend_update is set.
 * @param pretend_update, pretend record of key is in state next_state.
 * @return True IFF one of requirements is met.
 * */
bool
rule2(KeyDataList &key_list, KeyData &key, const RECORD record, 
	const STATE next_state, const bool pretend_update)
{
	const STATE mask_unsg[] =  {HID, OMN, OMN, NOCARE};
	const STATE mask_triv[] =  {OMN, OMN, OMN, NOCARE};
	const STATE mask_ds_i[] =  {RUM, OMN, OMN, NOCARE};
	const STATE mask_ds_o[] =  {UNR, OMN, OMN, NOCARE};
	const STATE mask_k_i1[] =  {OMN, RUM, RUM, NOCARE};
	const STATE mask_k_i2[] =  {OMN, OMN, RUM, NOCARE};
	const STATE mask_k_o1[] =  {OMN, UNR, UNR, NOCARE};
	const STATE mask_k_o2[] =  {OMN, UNR, OMN, NOCARE};

	/** for performance the lighter, more-likely-to-be-true test are
	 * performed first. */
	
	return
		exists(key_list, key, record, next_state, true, pretend_update, mask_triv) ||
		
		exists(key_list, key, record, next_state, true, pretend_update, mask_ds_i) &&
		exists(key_list, key, record, next_state, true, pretend_update, mask_ds_o) ||
		
		(exists(key_list, key, record, next_state, true, pretend_update, mask_k_i1) ||
		 exists(key_list, key, record, next_state, true, pretend_update, mask_k_i2) )&&
		(exists(key_list, key, record, next_state, true, pretend_update, mask_k_o1) ||
		 exists(key_list, key, record, next_state, true, pretend_update, mask_k_o2) ) ||

		unsigned_ok(key_list, key, record, next_state, pretend_update, mask_unsg, DS);
}

/** 
 * Checks for a valid signature situation.
 * 
 * @param key_list, list to search in.
 * @param key, key to compare with
 * @param record, record of said key to compare with
 * @param next_state, desired state of said record. Required if 
 * 			pretend_update is set.
 * @param pretend_update, pretend record of key is in state next_state.
 * @return True IFF one of requirements is met.
 * */
bool
rule3(KeyDataList &key_list, KeyData &key, const RECORD record, 
	const STATE next_state, const bool pretend_update)
{
	const STATE mask_triv[] =  {NOCARE, OMN, NOCARE, OMN};
	const STATE mask_keyi[] =  {NOCARE, RUM, NOCARE, OMN};
	const STATE mask_keyo[] =  {NOCARE, UNR, NOCARE, OMN};
	const STATE mask_sigi[] =  {NOCARE, OMN, NOCARE, RUM};
	const STATE mask_sigo[] =  {NOCARE, OMN, NOCARE, UNR};
	const STATE mask_unsg[] =  {NOCARE, HID, NOCARE, OMN};

	/** for performance the lighter, more-likely-to-be-true test are
	 * performed first. */

	return
		exists(key_list, key, record, next_state, true, pretend_update, mask_triv) ||
		
		exists(key_list, key, record, next_state, true, pretend_update, mask_keyi) &&
		exists(key_list, key, record, next_state, true, pretend_update, mask_keyo) ||
		
		exists(key_list, key, record, next_state, true, pretend_update, mask_sigi) &&
		exists(key_list, key, record, next_state, true, pretend_update, mask_sigo) ||

		unsigned_ok(key_list, key, record, next_state, pretend_update, mask_unsg, DK);
}

/**
 * Checks of transition to next_state maintains validity of zone.
 * 
 * Check all 3 rules. Any of the rules that are true in the current 
 * situation (ideally all) must be true in the desired situation.
 * No decay is allowed.
 * 
 * @param key_list, list to search in.
 * @param key, key to compare with
 * @param record, record of said key to compare with
 * @param next_state, desired state of said record.
 * @return True if transition is okay DNSSEC-wise.
 * */
bool
dnssec_approval(KeyDataList &key_list, KeyData &key, const RECORD record, 
	const STATE next_state)
{
	return 
		(!rule1(key_list, key, record, next_state, false) ||
		  rule1(key_list, key, record, next_state, true ) ) &&
		(!rule2(key_list, key, record, next_state, false) ||
		  rule2(key_list, key, record, next_state, true ) ) &&
		(!rule3(key_list, key, record, next_state, false) ||
		  rule3(key_list, key, record, next_state, true ) );
}

/**
 * At what time may this transition take place?
 * 
 * Given a record, its next state, and its last change time when may 
 * apply the transition? This is largely policy related.
 * 
 * @param policy of this zone
 * @param record we are testing
 * @param next_state of record 
 * @param lastchange of record
 * @return absolute time
 * */
time_t
min_transition_time(const Policy *policy, const RECORD record, 
	const STATE next_state, const time_t lastchange)
{
	const char *scmd = "min_transition_time";

	/** We may freely move a record to a uncertain state. */
	if (next_state == RUM || next_state == UNR) return lastchange;
	
	switch(record) {
		case DS:
			return addtime(lastchange,
				  policy->parent().ttlds()
				+ policy->parent().registrationdelay()
				+ policy->parent().propagationdelay());
		/* TODO: 5011 will create special case here */
		case DK: /** intentional fall-through */
		case RD:
			return addtime(lastchange,
				  policy->keys().ttl()
				+ policy->zone().propagationdelay()
				+ (next_state == OMN)
					? policy->keys().publishsafety()
					: policy->keys().retiresafety());
		case RS:
			return addtime(lastchange,
				  policy->signatures().ttl()
				+ policy->zone().propagationdelay());
		default: 
			ods_fatal_exit("[%s] %s Unknown record type (%d), "
				"fault of programmer. Abort.",
				module_str, scmd, (int)record);
	}
}

/**
 * Try to push each key for this zone to a next state. If one changes
 * visit the rest again. Loop stops when no changes can be made without
 * advance of time. Return time of first possible event.
 * 
 * @param zone, zone we are processing
 * @param now, current time
 * @return first absolute time some record *could* be advanced.
 * */
time_t
updateZone(EnforcerZone &zone, const time_t now)
{
	time_t returntime_zone = -1;
	time_t returntime_key;
	bool change;
	KeyDataList &key_list = zone.keyDataList();
	const Policy *policy = zone.policy();
	const char *scmd = "updateZone";
	ods_log_verbose("[%s] %s", module_str, scmd);

	/** Keep looping till there are no state changes.
	 * Find the earliest update time */
	do {
		change = false;
		for (int i = 0; i < key_list.numKeys(); i++) {
			KeyData &key = key_list.key(i);
			ods_log_verbose("[%s] %s processing key %s", module_str, 
				scmd, key.locator().c_str());

			for (RECORD record = REC_MIN; record < REC_MAX; ++record) {
				STATE state = getState(key, record);
				STATE next_state = getDesiredState(key.introducing(), state);
				
				/** record is stable */
				if (state == next_state) continue;
				ods_log_verbose("[%s] %s May %s transition to %s?", 
					module_str, scmd, RECORDAMES[(int)record], 
					STATENAMES[(int)next_state]);
				
				/** Policy prevents transition */
				if (!policy_approval(key, record, next_state)) continue;
				ods_log_verbose("[%s] %s Policy says we can (1/3)", 
					module_str, scmd);
				
				/** Would be invalid DNSSEC state */
				if (!dnssec_approval(key_list, key, record, next_state))
					continue;
				ods_log_verbose("[%s] %s DNSSEC says we can (2/3)", 
					module_str, scmd);
					
				time_t returntime_key = min_transition_time(policy, record, 
					next_state, getRecord(key, record).lastChange());

				/** It is to soon to make this change. Schedule it. */
				if (returntime_key > now) {
					minTime(returntime_key, returntime_zone);
					continue;
				}
				ods_log_verbose("[%s] %s Timing says we can (3/3) now: %d key: %d", 
					module_str, scmd, now, returntime_key);
				
				/** We've passed all tests! Make the transition */
				/* TODO: save next change instead of now */
				setState(key, record, next_state, now);
				change = true;
			}
		}
	} while (change);
	return returntime_zone;
}

/**
 * Search for youngest key in use by any zone with this policy
 * with at least the roles requested. See if it isn't expired.
 * also, check if it isn't in zone already. Also length, algorithm
 * must match and it must be a first generation key.
 * */
bool 
getLastReusableKey(EnforcerZone &zone,
	const Policy *policy, const KeyRole role,
	int bits, const string &repository, int algorithm, 
	const time_t now, HsmKey **ppKey,
	HsmKeyFactory &keyfactory, int lifetime)
{
	const char *scmd = "getLastReusableKey";
	
	if (!keyfactory.UseSharedKey(bits, repository, policy->name(), 
		algorithm, role, zone.name(), ppKey))
		return false;
	
	/** UseSharedKey() promised us a match, we'd better crash. */
	if (*ppKey == NULL)
		ods_fatal_exit("[%s] %s Keyfactory promised key but did not give it",
			module_str, scmd);
	
	/** Key must (still) be in use */
	if (now < (*ppKey)->inception() + lifetime) return true;
	
	/** Clean up, was set by default by UseSharedKey(), unset */
	(*ppKey)->setUsedByZone(zone.name(), false);
	return false;
}

/**
 * Abstraction to generalize different kind of keys. 
 * return number of keys _in_a_policy_ 
 * */
int 
numberOfKeys(const Keys *policyKeys, const KeyRole role)
{
	const char *scmd = "numberOfKeys";
	switch (role) {
		case KSK: return policyKeys->ksk_size();
		case ZSK: return policyKeys->zsk_size();
		case CSK: return policyKeys->csk_size();
		default:
			ods_fatal_exit("[%s] %s Unknow Role: (%d)", 
					module_str, scmd, role); /* report a bug! */
	}
}

/**
 * Abstraction to generalize different kind of keys. 
 * Note: a better solution would be inheritance. 
 * */
void 
keyProperties(const Keys *policyKeys, const KeyRole role, const int index, 
	int *bits, int *algorithm, int *lifetime, string &repository)
{
	const char *scmd = "keyProperties";
	
	/** Programming error, report a bug! */
	if (index >= numberOfKeys(policyKeys, role)) 
		ods_fatal_exit("[%s] %s Index out of bounds", module_str, scmd); 
		
	switch (role) {
		case KSK:
			*bits	   = policyKeys->ksk(index).bits();
			*algorithm = policyKeys->ksk(index).algorithm();
			*lifetime  = policyKeys->ksk(index).lifetime();
            repository.assign(policyKeys->ksk(index).repository());
			break;
		case ZSK:
			*bits	   = policyKeys->zsk(index).bits();
			*algorithm = policyKeys->zsk(index).algorithm();
			*lifetime  = policyKeys->zsk(index).lifetime();
            repository.assign(policyKeys->zsk(index).repository());
			break;
		case CSK:
			*bits	   = policyKeys->csk(index).bits();
			*algorithm = policyKeys->csk(index).algorithm();
			*lifetime  = policyKeys->csk(index).lifetime();
            repository.assign(policyKeys->csk(index).repository());
			break;
		default:
			/** Programming error, report a bug! */
			ods_fatal_exit("[%s] %s Unknow Role: (%d)",
				module_str, scmd, role);
	}
}

/**
 * Finds the last inserted key in the list. It's role must be a
 * subset or equal to role.
 * 
 * @param keys list of keys to search in
 * @param role minimum role target must have
 * @return time_t inception time of youngest matching key. 
 * 		-1 iff none found
 * */
time_t 
most_recent_inception(KeyDataList &keys, KeyRole role)
{
	/** default answer when no keys available */
	time_t most_recent = -1; 

	for (int i=0; i<keys.numKeys(); ++i) {
		KeyData &k = keys.key(i);
		if (!k.revoke() && (k.role()&role) == role && k.inception()) {
			if (k.inception() > most_recent)
				most_recent = k.inception();
		}
	}
	return most_recent;
}

/**
 * See what needs to be done for the policy 
 * */
time_t updatePolicy(EnforcerZone &zone, const time_t now, HsmKeyFactory &keyfactory,
		KeyDataList &key_list)
{
	int bits, algorithm, lifetime;
	time_t last_insert, next_insert;

	time_t return_at = -1;
	const Policy *policy = zone.policy();
	Keys policyKeys = policy->keys();
	const string policyName = policy->name();
	const char *scmd = "updatePolicy";

	ods_log_verbose("[%s] %s policyName: %s", module_str, scmd, 
		policyName.c_str());
	
	/** Visit every type of key-configuration, not pretty but we can't
	 * loop over enums. Include MAX in enum? */
	for ( int role = 1; role < 4; role++ ) {
		last_insert = most_recent_inception(zone.keyDataList(),
			(KeyRole)role);
		
		/** NOTE: we are not looping over keys, but configurations */
		for ( int i = 0; i < numberOfKeys( &policyKeys, (KeyRole)role ); i++ ) {
			string repository;

			/** select key properties of key i in KeyRole role */
			keyProperties(&policyKeys, (KeyRole)role, i, &bits, 
				&algorithm, &lifetime, repository);
			next_insert = last_insert + lifetime;
			if ( now < next_insert && last_insert != -1 ) {
				/** No need to change key, come back at next_insert*/
				minTime( next_insert, return_at );
				continue;
			}

			/** time for a new key */
			ods_log_verbose("[%s] %s New key needed for role %d", 
				module_str, scmd, role);
			string locator;
			HsmKey *hsm_key;
			bool got_key;

			if ( policyKeys.zones_share_keys() )
				got_key = getLastReusableKey(
					zone, policy, (KeyRole)role, bits, repository, algorithm, now,
					&hsm_key, keyfactory, lifetime)
				?
					true
				:
					keyfactory.CreateSharedKey(bits, repository, policyName,
					algorithm, (KeyRole)role, zone.name(),&hsm_key );
			else
				got_key = keyfactory.CreateNewKey(bits,repository,
					policyName, algorithm, (KeyRole)role, &hsm_key );
			
			if ( !got_key ) {
				/** The factory was not ready, return later */
				minTime( now + NOKEY_TIMEOUT, return_at);
				ods_log_warning("[%s] %s No keys available on hsm, retry in %d seconds", 
					module_str, scmd, NOKEY_TIMEOUT);
				continue;
			}
			ods_log_verbose("[%s] %s got new key from HSM", module_str, 
				scmd);
			
			/** Make new key from HSM_key and set defaults */
			KeyData &new_key = zone.keyDataList().addNewKey( algorithm, 
				now, (KeyRole)role, false, false, false);
			new_key.setLocator( hsm_key->locator() );
			new_key.setDSSeen( false );
			new_key.setSubmitToParent( false );
			new_key.keyStateDS().setState((role&KSK)?HID:NOCARE);
			new_key.keyStateDNSKEY().setState(HID);
			new_key.keyStateRRSIGDNSKEY().setState((role&KSK)?HID:NOCARE);
			new_key.keyStateRRSIG().setState((role&ZSK)?HID:NOCARE);
			new_key.keyStateDS().setLastChange(now);
			new_key.keyStateDNSKEY().setLastChange(now);
			new_key.keyStateRRSIGDNSKEY().setLastChange(now);
			new_key.keyStateRRSIG().setLastChange(now);
			new_key.setIntroducing(true);

			/** New key inserted, come back after its lifetime */
			minTime( now + lifetime, return_at );

			/** Tell similar keys to outroduce, skip new key */
			/* TODO: Do we want this? is there a usecase where we
			 * would want for example 2 zsks? */
			for (int j = 0; j < key_list.numKeys(); j++) {
				KeyData &key = key_list.key(j);
				if (!key.introducing() || !(key.role() & role)||
					key.locator().compare(new_key.locator()) == 0)
					continue;
				key.setIntroducing(false);
				ods_log_verbose("[%s] %s decommissioning old key: %s", 
					module_str, scmd, key.locator().c_str());
			}
		}
	} /** loop over KeyRole */
	return return_at;
}

/**
 * Removes all keys from list that are no longer used.
 * 
 * @param key_list list to filter.
 * */
void
removeDeadKeys(KeyDataList &key_list)
{
	const char *scmd = "removeDeadKeys";

	for (int i = key_list.numKeys()-1; i >= 0; i--) {
		KeyData &key = key_list.key(i);
		if (	(getState(key, DS) == HID || getState(key, DS) == NOCARE) &&
				(getState(key, DK) == HID || getState(key, DK) == NOCARE) &&
				(getState(key, RD) == HID || getState(key, RD) == NOCARE) &&
				(getState(key, RS) == HID || getState(key, RS) == NOCARE) &&
				!key.introducing()) {
			ods_log_info("[%s] %s delete key: %s", module_str, scmd, key.locator().c_str());
			key_list.delKey(i);
		}
	}
}

/* see header file */
time_t 
update(EnforcerZone &zone, const time_t now, HsmKeyFactory &keyfactory)
{
	time_t policy_return_time, zone_return_time;
	KeyDataList &key_list = zone.keyDataList();
	const char *scmd = "update";

	ods_log_info("[%s] %s Zone: %s", module_str, scmd, zone.name().c_str());

	policy_return_time = updatePolicy(zone, now, keyfactory, key_list);
	zone_return_time = updateZone(zone, now);
	removeDeadKeys(key_list);

	/** Always set these flags. Normally this needs to be done _only_
	 * when signerConfNeedsWriting() is set. However a previous
	 * signerconf might not be available, we have no way of telling. :(
	 * */
	for (int i = 0; i < key_list.numKeys(); i++) {
		KeyData &k = key_list.key(i);
		k.setPublish(getState(k, DK) == OMN || getState(k, DK) == RUM);
		k.setActive(getState(k, RS) == OMN || getState(k, RS) == RUM);
	}

	minTime(policy_return_time, zone_return_time);
	return zone_return_time;
}
