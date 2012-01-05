#include "keystate/keystate_ds_seen_task.h"
#include "shared/file.h"
#include "shared/duration.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include "keystate/keystate.pb.h"
#include "xmlext-pb/xmlext-rd.h"

#include "protobuf-orm/pb-orm.h"
#include "daemon/orm.h"

#include <memory>
#include <fcntl.h>

static const char *module_str = "keystate_ds_seen_task";

static void
list_keys_submitted(OrmConn conn, int sockfd, const char *datastore)
{
	#define LOG_AND_RETURN(errmsg)\
		do{ods_log_error_and_printf(sockfd,module_str,errmsg);return;}while(0)

	// list all keys that have submitted flag set.
	ods_printf(sockfd,
			   "Database set to: %s\n"
			   "Submitted Keys:\n"
			   "Zone:                           "
			   "Key role:     "
			   "Keytag:       "
			   "Id:                                      "
			   "\n"
			   ,datastore
			   );
	
	OrmTransaction transaction(conn);
	if (!transaction.started())
		LOG_AND_RETURN("transaction not started");

	{	OrmResultRef rows;
		::ods::keystate::EnforcerZone enfzone;
		if (!OrmMessageEnum(conn,enfzone.descriptor(),rows))
			LOG_AND_RETURN("zone enumeration failed");

		for (bool next=OrmFirst(rows); next; next=OrmNext(rows)) {
			
			if (!OrmGetMessage(rows, enfzone, /*zones + keys*/true))
				LOG_AND_RETURN("retrieving zone from database failed");

			for (int k=0; k<enfzone.keys_size(); ++k) {
				const ::ods::keystate::KeyData &key = enfzone.keys(k);
				
				// ZSKs are never trust anchors so skip them.
				if (key.role() == ::ods::keystate::ZSK)
					continue;
				
				// Skip KSKs with a zero length id, they are placeholder keys.
				if (key.locator().size()==0)
					continue;
				
				if (key.ds_at_parent()!=::ods::keystate::submitted)
					continue;
				
				std::string keyrole = keyrole_Name(key.role());
				ods_printf(sockfd,
						   "%-31s %-13s %-13u %-40s\n",
						   enfzone.name().c_str(),
						   keyrole.c_str(),
						   key.keytag(),
						   key.locator().c_str()
						   );
			}
				
		}		
	}
	
	#undef LOG_AND_RETURN
}

static void
change_keys_submitted_to_seen(OrmConn conn, int sockfd,
							  const char *zone, const char *id, uint16_t keytag)
{
	#define LOG_AND_RETURN(errmsg)\
		do{ods_log_error_and_printf(sockfd,module_str,errmsg);return;}while(0)
	#define LOG_AND_RETURN_1(errmsg,p)\
		do{ods_log_error_and_printf(sockfd,module_str,errmsg,p);return;}while(0)
		
	OrmTransactionRW transaction(conn);
	if (!transaction.started())
		LOG_AND_RETURN("transaction not started");
	
	std::string qzone;
	if (!OrmQuoteStringValue(conn, std::string(zone), qzone))
		LOG_AND_RETURN("quoting string value failed");
	
	{	OrmResultRef rows;
		::ods::keystate::EnforcerZone enfzone;
		if (!OrmMessageEnumWhere(conn,enfzone.descriptor(),
								 rows,"name = %s",qzone.c_str()))
			LOG_AND_RETURN("zone enumeration failed");
		
		if (!OrmFirst(rows))
			LOG_AND_RETURN_1("zone %s not found",zone);
		
		OrmContextRef context;
		if (!OrmGetMessage(rows, enfzone, /*zones + keys*/true, context))
			LOG_AND_RETURN("retrieving zone from database failed");
		
		// we no longer need the query result, so release it.
		rows.release();
		
		// Try to change the state of a specific 'submitted' key to 'seen'.
		bool bKeyStateMatched = false;
		bool bZoneModified = false;
		for (int k=0; k<enfzone.keys_size(); ++k) {
			const ::ods::keystate::KeyData &key = enfzone.keys(k);
			
			// ZSKs are never trust anchors so skip them.
			if (key.role() == ::ods::keystate::ZSK)
				continue;
			
			// Skip KSKs with a zero length id, they are placeholder keys.
			if (key.locator().size()==0)
				continue;
			
			if ((id && key.locator()==id) || (keytag && key.keytag()==keytag)) {
				bKeyStateMatched = true;
				
				if (key.ds_at_parent()!=::ods::keystate::submitted) {
					ods_printf(sockfd,
							   "Key that matches id \"%s\" in zone "
							   "\"%s\" is not submitted but %s\n",
							   key.locator().c_str(), zone,
							   dsatparent_Name(key.ds_at_parent()).c_str());
					break;
				}
				
				enfzone.mutable_keys(k)->set_ds_at_parent(::ods::keystate::seen);
				enfzone.set_next_change(0); // reschedule immediately
				bZoneModified = true;
			}
		}
		
		
		// Report back the status of the operation.
		if (!bKeyStateMatched) {
			if (id)
				ods_printf(sockfd,
						   "No KSK key matches id \"%s\" in zone \"%s\"\n",
						   id,
						   zone);
			else
				ods_printf(sockfd,
						   "No KSK key matches keytag \"%u\" in zone \"%s\"\n",
						   keytag,
						   zone);
		} else {
			if (bZoneModified) {
				// Update key states for the zone in the database.
				if (!OrmMessageUpdate(context))
					LOG_AND_RETURN_1("unable to update zone %s in the database",zone);

				// Commit updated records to the database.
				if (!transaction.commit())
					LOG_AND_RETURN_1("unable to commit updated zone %s to the database",zone);
				
				ods_log_debug("[%s] key states have been updated",module_str);
				ods_printf(sockfd,"update of key states completed.\n");
			} else {
				ods_log_debug("[%s] key states are unchanged",module_str);
				ods_printf(sockfd,"key states are unchanged\n");
			}
		}
	}
	
	#undef LOG_AND_RETURN
	#undef LOG_AND_RETURN_1
}

void 
perform_keystate_ds_seen(int sockfd, engineconfig_type *config,
                         const char *zone, const char *id, uint16_t keytag)
{
	GOOGLE_PROTOBUF_VERIFY_VERSION;
	OrmConnRef conn;
	if (ods_orm_connect(sockfd, config, conn)) {
		// list key states with ds-seen state
		if (!(zone && (id || keytag)))
			list_keys_submitted(conn, sockfd, config->datastore);
		else
			change_keys_submitted_to_seen(conn, sockfd, zone, id, keytag);
	}
}
