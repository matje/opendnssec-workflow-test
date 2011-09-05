#ifndef _ENFORCER_ENFORCERZONE_H_
#define _ENFORCER_ENFORCERZONE_H_

/*
 * PB implemenation of enforcer data to be used as test fixture 
 * and to establish whether the interface is actually complete.
 */

#include <set>
#include "policy/kasp.pb.h"
#include "enforcer/enforcerdata.h"
#include "keystate/keystate.pb.h"

class KeyStatePB : public KeyState {
private:
    ::ods::keystate::KeyState *_keystate;
public:
    KeyStatePB(::ods::keystate::KeyState *keystate);
    
    virtual int state();
    virtual void setState(int value);
    
    virtual int lastChange();
    virtual void setLastChange(int value);

    virtual int ttl();
    virtual void setTtl(int value);

    virtual bool minimize();
    void setMinimize(bool value);
};

class KeyDataPB : public KeyData {
private:
    ::ods::keystate::KeyData *_keydata;
    
    KeyStatePB _keyStateDS;
    KeyStatePB _keyStateRRSIG;
    KeyStatePB _keyStateDNSKEY;
    KeyStatePB _keyStateRRSIGDNSKEY;
public:
    KeyDataPB( ::ods::keystate::KeyData *keydata );
    bool matches( const ::ods::keystate::KeyData *keydata );

    virtual const std::string &locator();
    virtual void setLocator(const std::string &value);
    
    virtual int algorithm();
    void setAlgorithm(int value);
    
    virtual time_t inception();
    void setInception(time_t value);
    
    virtual KeyState &keyStateDS();
    virtual KeyState &keyStateRRSIG();
    virtual KeyState &keyStateDNSKEY();
    virtual KeyState &keyStateRRSIGDNSKEY();
    
    virtual KeyRole role();
    void setRole(KeyRole value);
    
    virtual bool isDSSeen();
    virtual void setDSSeen(bool value);
    
    virtual bool submitToParent();
    virtual void setSubmitToParent(bool value);

    virtual bool introducing(); /* goal */
    virtual void setIntroducing(bool value);
    
    /* alternative path */
    virtual bool revoke();
    
    /* selective brakes */
    virtual bool standby();

    virtual void setPublish(bool value);
    virtual void setActiveZSK(bool value);
    virtual void setActiveKSK(bool value);
};

class KeyDataListPB : public KeyDataList {
private:
    std::vector<KeyDataPB> _keys;
    ::ods::keystate::EnforcerZone *_zone;
public:
    KeyDataListPB( ::ods::keystate::EnforcerZone *zone);

    virtual KeyData &addNewKey(int algorithm, time_t inception, KeyRole role,
                               bool minimizeDS, bool minimizeRRSIG, 
                               bool minimizeDNSKEY);
    virtual int numKeys();
    virtual KeyData &key(int index);
    virtual void delKey(int index);
};

class EnforcerZonePB : public EnforcerZone {
private:
    ::ods::keystate::EnforcerZone *_zone;
    const ::ods::kasp::Policy *_policy;

    KeyDataListPB _keyDataList;
public:
    EnforcerZonePB(::ods::keystate::EnforcerZone *zone, const ::ods::kasp::Policy *policy);

    /* Get access to the policy for associated with this zone */
    virtual const std::string &name();
    
    /* Get access to the policy for associated with this zone */
    virtual const ::ods::kasp::Policy *policy();
    
    /* Get access to the list of KeyData entries for this zone. */
    virtual KeyDataList &keyDataList();
    
    /* returns true when the signer configuration for the signer should be updated */
    virtual bool signerConfNeedsWriting();
    
    /* set to true when the signer configuration for the signer needs to  be updated. */
    virtual void setSignerConfNeedsWriting(bool value);
    
    /* When the key states in this zone are expected to change state. */
    void setNextChange(time_t value);

    /* Moment at which current TTL becomes effective */
    virtual time_t ttlEnddateDs();
    virtual void setTtlEnddateDs(time_t value);
    virtual time_t ttlEnddateDk();
    virtual void setTtlEnddateDk(time_t value);
    virtual time_t ttlEnddateRs();
    virtual void setTtlEnddateRs(time_t value);
};

#endif
