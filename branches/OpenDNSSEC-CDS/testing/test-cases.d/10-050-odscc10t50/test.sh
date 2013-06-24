#!/usr/bin/env bash
#
# Use a Tokenlabel with 1 character

if [ -n "$HAVE_MYSQL" ]; then
	ods_setup_conf conf.xml conf-mysql.xml
fi &&

ods_reset_env &&

ods_softhsm_init_token "1" "O" "4321" "4321" &&

log_this_timeout ods-control-enforcer-start 30 ods-control enforcer start &&
syslog_waitfor 60 'ods-enforcerd: .*\[engine\] enforcer started' &&

ods_setup_env &&

log_this_timeout ods-control-signer-start 30 ods-control signer start &&
syslog_waitfor 60 'ods-signerd: .*\[engine\] signer started' &&

syslog_waitfor 60 'ods-signerd: .*\[STATS\] ods' &&
test -f "$INSTALL_ROOT/var/opendnssec/signed/ods" &&

log_this_timeout ods-control-stop 30 ods-control stop &&
syslog_waitfor 60 'ods-enforcerd: .*\[engine\] enforcer shutdown' &&
syslog_waitfor 60 'ods-signerd: .*\[engine\] signer shutdown' &&
return 0

ods_kill
return 1
