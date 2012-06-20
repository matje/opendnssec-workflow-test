#!/usr/bin/env bash
#
# Change the kasp.db location and change Datastore in conf.xml accordingly

if [ -n "$HAVE_MYSQL" ]; then
	return 0
fi &&

ods_reset_env &&

ods_setup_conf conf.xml conf2.xml &&
mv -- "$INSTALL_ROOT/var/opendnssec/kasp.db" "$INSTALL_ROOT/var/opendnssec/kasp2.db" &&

log_this_timeout ods-control-start 30 ods-control start &&
syslog_waitfor 60 'ods-enforcerd: .*\[engine\] enforcer started' &&
syslog_waitfor 60 'ods-signerd: .*\[engine\] signer started' &&

log_this_timeout ods-control-stop 30 ods-control stop &&
syslog_waitfor 60 'ods-enforcerd: .*\[engine\] enforcer shutdown' &&
syslog_waitfor 60 'ods-signerd: .*\[engine\] signer shutdown' &&
return 0

ods_kill
return 1
