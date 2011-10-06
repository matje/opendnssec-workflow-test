#include <fcntl.h>

#include "signconf/signconf_task.h"
#include "shared/file.h"
#include "shared/duration.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include "xmlext-pb/xmlext-rd.h"
#include "xmlext-pb/xmlext-wr.h"
#include "signconf/signconf.pb.h"
#include "policy/kasp.pb.h"
#include "keystate/keystate.pb.h"



static const char *module_str = "signconf_task";

void WriteSignConf(const std::string &path,
                   ::ods::signconf::SignerConfigurationDocument *doc)
{
    write_pb_message_to_xml_file(doc,path.c_str());
}

/*
 * ForEvery zone Z in zonelist do
 *   if flag signerConfNeedsWriting is set then
 *      Assign the data from the zone and associated policy to the signer
 *          configuration object
 *      Write signer configuration XML file at the correct location taken
 *          from zonedata signerconfiguration field in the zone 
 */
void 
perform_signconf(int sockfd, engineconfig_type *config, int bforce)
{
    char buf[ODS_SE_MAXLINE];
	const char *policyfile = config->policy_filename;
    const char *datastore = config->datastore;
    int fd;
    
	GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    // Read the zonelist and policies in from the same directory as 
    // the database, use serialized protocolbuffer for now, but switch 
    // to using database table ASAP.
    
    bool bFailedToLoad = false;
    
    ::ods::kasp::KaspDocument *kaspDoc = new ::ods::kasp::KaspDocument;
    {
        std::string policypb(datastore);
        policypb += ".policy.pb";
        int fd = open(policypb.c_str(),O_RDONLY);
        if (kaspDoc->ParseFromFileDescriptor(fd)) {
            ods_log_debug("[%s] policies have been loaded", 
                          module_str);
        } else {
            ods_log_error("[%s] policies could not be loaded from \"%s\"", 
                          module_str,policypb.c_str());
            bFailedToLoad = true;
        }
        close(fd);
    }
    
    ::ods::keystate::KeyStateDocument *keystateDoc = 
        new ::ods::keystate::KeyStateDocument;
    {
        std::string keystatepb(datastore);
        keystatepb += ".keystate.pb";
        int fd = open(keystatepb.c_str(),O_RDONLY);
        if (keystateDoc->ParseFromFileDescriptor(fd)) {
            ods_log_debug("[%s] keystates have been loaded", 
                          module_str);
        } else {
            ods_log_error("[%s] keystates could not be loaded from \"%s\"", 
                          module_str,keystatepb.c_str());
            bFailedToLoad = true;
        }
        close(fd);
    }

    if (bFailedToLoad) {
        delete kaspDoc;
        delete keystateDoc;
        ods_log_error("[%s] unable to continue", 
                      module_str);
        return ;
    }
        
    // Go through all the zones and write signer configuration when required.
    for (int i=0; i<keystateDoc->zones_size(); ++i) {

        ::ods::keystate::EnforcerZone *ks_zone = keystateDoc->mutable_zones(i);
        
        if (!ks_zone->signconf_needs_writing() && bforce==0)
            continue;

        const ::ods::kasp::KASP &
        kasp = kaspDoc->kasp();
        
        //printf("%s\n",zone.name().c_str());
        
        const ::ods::kasp::Policy *policy = NULL;
        
        for (int p=0; p<kasp.policies_size(); ++p) {
            // lookup the policy associated with this zone 
            // printf("%s\n",kasp.policies(p).name().c_str());
            if (kasp.policies(p).name() == ks_zone->policy()) {
                policy = &kasp.policies(p);
                ods_log_debug("[%s] policy %s found for zone %s", 
                              module_str,policy->name().c_str(),
                              ks_zone->name().c_str());
                break;
            }
        }
        
        if (policy == NULL) {
            ods_log_error("[%s] policy %s could not be found for zone %s", 
                          module_str,ks_zone->policy().c_str(),
                          ks_zone->name().c_str());
            ods_log_error("[%s] unable to enforce zone %s", 
                          module_str,ks_zone->name().c_str());
            continue;
        }

        ::ods::signconf::SignerConfigurationDocument *doc  = new ::ods::signconf::SignerConfigurationDocument;
        ::ods::signconf::Zone *sc_zone = doc->mutable_signerconfiguration()->mutable_zone();
        sc_zone->set_name(ks_zone->name());
        
        // Get the Signatures parameters straight from the policy.
        ::ods::signconf::Signatures *sc_sigs = sc_zone->mutable_signatures();
        const ::ods::kasp::Signatures &kp_sigs = policy->signatures();
        
        sc_sigs->set_resign( kp_sigs.resign() );
        sc_sigs->set_refresh( kp_sigs.refresh() );
        sc_sigs->set_valdefault( kp_sigs.valdefault() );
        sc_sigs->set_valdenial( kp_sigs.valdenial() );
        sc_sigs->set_jitter( kp_sigs.jitter() );
        sc_sigs->set_inceptionoffset( kp_sigs.inceptionoffset() );
        
        // Get the Denial parameters straight from the policy
        ::ods::signconf::Denial *sc_denial = sc_zone->mutable_denial();
        const ::ods::kasp::Denial &kp_denial = policy->denial();
        
        if (kp_denial.has_nsec() && kp_denial.has_nsec3()) {
            ods_log_error("[%s] policy %s contains both NSEC and NSEC3 in Denial for zone %s", 
                          module_str,ks_zone->policy().c_str(),
                          ks_zone->name().c_str());
            // skip to the next zone.
            continue;
        } else {
            if (!kp_denial.has_nsec() && !kp_denial.has_nsec3()) {
                ods_log_error("[%s] policy %s does not contains NSEC or NSEC3 in Denial for zone %s", 
                              module_str,ks_zone->policy().c_str(),
                              ks_zone->name().c_str());
                // skip to the next zone.
                continue;
            } else {
                // NSEC
                if(!kp_denial.has_nsec())
                    sc_denial->clear_nsec();
                else
                    sc_denial->mutable_nsec();
                
                // NSEC3
                if (!kp_denial.has_nsec3()) 
                    sc_denial->clear_nsec3();
                else {
                    ::ods::signconf::NSEC3 *sc_nsec3 = sc_denial->mutable_nsec3();
                    const ::ods::kasp::NSEC3 &kp_nsec3 = kp_denial.nsec3();
                    if (kp_nsec3.has_optout())
                        sc_nsec3->set_optout( kp_nsec3.optout() );
                    else
                        sc_nsec3->clear_optout();
                    sc_nsec3->set_algorithm( kp_nsec3.algorithm() );
                    sc_nsec3->set_iterations( kp_nsec3.iterations() );
                    sc_nsec3->set_salt( kp_nsec3.salt() );
                }
            }
        }

        // Get the Keys from the zone data and add them to the signer 
        // configuration
        ::ods::signconf::Keys *sc_keys = sc_zone->mutable_keys();
        sc_keys->set_ttl( policy->keys().ttl() );

        for (int k=0; k<ks_zone->keys_size(); ++k) {
            const ::ods::keystate::KeyData &ks_key = ks_zone->keys(k);

            // first check whether we actually shoul write this key into the
            // signer configuration.
            if (!ks_key.publish() && !ks_key.active_ksk() && !ks_key.active_zsk())
                continue;

            // yes we need to write the key to the configuration.
            ::ods::signconf::Key* sc_key = sc_keys->add_keys();
            
            if (ks_key.role() == ::ods::keystate::ZSK)
                sc_key->set_flags( 256 ); // ZSK
            else
                sc_key->set_flags( 257 ); // KSK,CSK
                
            sc_key->set_algorithm( ks_key.algorithm() );
            sc_key->set_locator( ks_key.locator() );
            
            
            // The active flag determines whether the KSK or ZSK
            // flag is written to the signer configuration.
            sc_key->set_ksk( ks_key.active_ksk() &&
                            (ks_key.role() == ::ods::keystate::KSK
                             || ks_key.role() == ::ods::keystate::CSK) );
            sc_key->set_zsk( ks_key.active_zsk() &&
                            (ks_key.role() == ::ods::keystate::ZSK
                             || ks_key.role() == ::ods::keystate::CSK) );
            sc_key->set_publish( ks_key.publish() );
            
            // The deactivate flag was intended to allow smooth key rollover.
            // With the deactivate flag present a normal rollover would be 
            // performed where signatures would be replaced immmediately.
            // With deactivate flag not present a smooth rollover would be 
            // performed where signatures that had not yet passed there refresh
            // timestamp could be recycled and gradually replaced with 
            // new signatures.
            // Currently this flag is not supported by the signer engine.
            // sc_key->set_deactivate(  );
        }
        
        const ::ods::kasp::Zone &kp_zone = policy->zone();
        sc_zone->set_ttl( kp_zone.ttl() );
        sc_zone->set_min( kp_zone.min() );
        sc_zone->set_serial( (::ods::signconf::serial) kp_zone.serial() );

        if (policy->audit_size() > 0)
            sc_zone->set_audit(true);
        else
            sc_zone->clear_audit();

        WriteSignConf(ks_zone->signconf_path(), doc);
        
        ks_zone->set_signconf_needs_writing(false);
        
        delete doc;
    }
    
    // Persist the keystate zones back to disk as they may have
    // been changed while writing the signer configurations
    if (keystateDoc->IsInitialized()) {
        std::string datapath(datastore);
        datapath += ".keystate.pb";
        int fd = open(datapath.c_str(),O_WRONLY|O_CREAT, 0644);
        if (keystateDoc->SerializeToFileDescriptor(fd)) {
            ods_log_debug("[%s] key states have been updated",
                          module_str);
            
            (void)snprintf(buf, ODS_SE_MAXLINE,
                           "update of key states completed.\n");
            ods_writen(sockfd, buf, strlen(buf));
        } else {
            (void)snprintf(buf, ODS_SE_MAXLINE,
                           "error: key states file could not be written.\n");
            ods_writen(sockfd, buf, strlen(buf));
        }
        close(fd);
    } else {
        (void)snprintf(buf, ODS_SE_MAXLINE,
                       "error: a message in the key states is missing "
                       "mandatory information.\n");
        ods_writen(sockfd, buf, strlen(buf));
    }

    delete kaspDoc;
    delete keystateDoc;
}

static task_type * 
signconf_task_perform(task_type *task)
{
    perform_signconf(-1,(engineconfig_type *)task->context,0);
    task_cleanup(task);
    return NULL;
}

task_type *
signconf_task(engineconfig_type *config, const char *what, const char * who)
{
    task_id what_id = task_register(what, "signconf_task_perform",
                                    signconf_task_perform);
	return task_create(what_id, time_now(), who, (void*)config);
}