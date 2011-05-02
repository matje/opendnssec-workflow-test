
extern "C" {
#include "shared/duration.h"
#include "shared/file.h"
#include "zone/update_zonelist_task.h"
}

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include "zonelist.pb.h"

#include "xmlext-pb/xmlext.h"

#include <fcntl.h>

static const char *update_zonelist_task_str = "update_zonelist_task";

void 
perform_update_zonelist(int sockfd, engineconfig_type *config)
{
    char buf[ODS_SE_MAXLINE];
	const char *zonelistfile = config->zonelist_filename;
    const char *datastore = config->datastore;

	GOOGLE_PROTOBUF_VERIFY_VERSION;

    /*
     // Dump the meta-information of the KaspDocument.
     ::google::protobuf::Message *msg  = new ::ods::kasp::KaspDocument;
     recurse_dump_descriptor(msg->GetDescriptor());
     delete msg;
     */
	
	// Create a policy and fill it up with some data.
	::ods::zonelist::ZoneListDocument *doc  = new ::ods::zonelist::ZoneListDocument;
	if (read_pb_message_from_xml_file(doc, zonelistfile)) {
		if (doc->has_zonelist()) {
			const ::ods::zonelist::ZoneList  &zonelist = doc->zonelist();
			if (zonelist.zones_size() > 0) {
				if (zonelist.IsInitialized()) {
                    
                    std::string datapath(datastore);
                    datapath += ".zonelist.pb";
                    int fd = open(datapath.c_str(),O_WRONLY|O_CREAT, 0644);
                    if (doc->SerializeToFileDescriptor(fd)) {
                        ods_log_debug("[%s] zonelist has been updated", 
                                      update_zonelist_task_str);

                    } else {
                        (void)snprintf(buf, ODS_SE_MAXLINE, "error: zonelist file could not be written.\n");
                        ods_writen(sockfd, buf, strlen(buf));
                    }
                    close(fd);
				} else {
                    (void)snprintf(buf, ODS_SE_MAXLINE, "error: a zone in the zonelist is missing mandatory information.\n");
                    ods_writen(sockfd, buf, strlen(buf));
                }
			} else {
                (void)snprintf(buf, ODS_SE_MAXLINE, "warning: no zones found in zonelist.\n");
                ods_writen(sockfd, buf, strlen(buf));
            }
		} else {
            (void)snprintf(buf, ODS_SE_MAXLINE, "warning: no zonelist found in zonelist.xml file.\n");
            ods_writen(sockfd, buf, strlen(buf));
        }
    } else {
        (void)snprintf(buf, ODS_SE_MAXLINE, "warning: unable to read the zonelist.xml file.\n");
        ods_writen(sockfd, buf, strlen(buf));
    }
	delete doc;
}

static task_type * 
update_zonelist_task_perform(task_type *task)
{
    perform_update_zonelist(-1,(engineconfig_type *)task->context);
    
    task_cleanup(task);
    return NULL;
}

task_type *
update_zonelist_task(engineconfig_type *config)
{
    task_id what = task_register("update zonelist",
                                 "update_zonelist_task_perform", 
                                 update_zonelist_task_perform);
	return task_create(what, time_now(), "all",(void*)config);
}
