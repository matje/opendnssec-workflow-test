# $Id$

full_bindir=`eval eval eval eval eval echo "${bindir}" | sed "s#NONE#${prefix}#" | sed "s#NONE#${ac_default_prefix}#"`
full_sbindir=`eval eval eval eval eval echo "${sbindir}" | sed "s#NONE#${prefix}#" | sed "s#NONE#${ac_default_prefix}#"`
full_libdir=`eval eval eval eval eval echo "${libdir}" | sed "s#NONE#${prefix}#" | sed "s#NONE#${ac_default_prefix}#"`
full_libexecdir=`eval eval eval eval eval echo "${libexecdir}" | sed "s#NONE#${prefix}#" | sed "s#NONE#${ac_default_prefix}#"`
full_datadir=`eval eval eval eval eval echo "${datadir}" | sed "s#NONE#${prefix}#" | sed "s#NONE#${ac_default_prefix}#"`
full_sysconfdir=`eval eval eval eval eval echo "${sysconfdir}" | sed "s#NONE#${prefix}#" | sed "s#NONE#${ac_default_prefix}#"`
full_localstatedir=`eval eval eval eval eval echo "${localstatedir}" | sed "s#NONE#${prefix}#" | sed "s#NONE#${ac_default_prefix}#"`

opendnsseclibdir=$full_libdir/opendnssec
opendnsseclibexecdir=$full_libexecdir/opendnssec
opendnssecdatadir=$full_datadir/opendnssec
opendnssecsysconfdir=$full_sysconfdir/opendnssec
opendnsseclocalstatedir="$full_localstatedir/opendnssec"

OPENDNSSEC_CONFIG_DIR=$opendnssecsysconfdir
OPENDNSSEC_SCHEMA_DIR=$opendnssecsysconfdir
OPENDNSSEC_STATE_DIR=$opendnssecsysconfdir

AC_SUBST([OPENDNSSEC_CONFIG_DIR])
AC_SUBST([OPENDNSSEC_SCHEMA_DIR])
AC_SUBST([OPENDNSSEC_STATE_DIR])
