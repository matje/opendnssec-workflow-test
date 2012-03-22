#!/usr/bin/env bash
source `dirname "$0"`/lib.sh && init || exit 1

LDNS="ldns-1.6.12"
LDNS_URL="http://nlnetlabs.nl/downloads/ldns/$LDNS.tar.gz"
LDNS_FILENAME="$LDNS.tar.gz"
LDNS_HASH_TYPE="sha1"
LDNS_HASH="1d61df0f666908551d5a62768f77d63e727810aa"

check_if_built ldns && exit 0
start_build ldns

LDNS_SRC=`fetch_src "$LDNS_URL" "$LDNS_FILENAME" "$LDNS_HASH_TYPE" "$LDNS_HASH"`

build_ok=0
case "$DISTRIBUTION" in
	centos | \
	redhat | \
	fedora | \
	sl | \
	ubuntu | \
	debian | \
	opensuse | \
	suse | \
	freebsd | \
	sunos | \
	openbsd )
		(
			gunzip -c "$LDNS_SRC" | tar xf - &&
			cd "$LDNS" &&
			./configure --prefix="$INSTALL_ROOT" \
				--disable-gost &&
			$MAKE &&
			$MAKE install
		) &&
		build_ok=1
		;;
	netbsd )
		(
			gunzip -c "$LDNS_SRC" | tar xf - &&
			cd "$LDNS" &&
			patch -p1 < ../ldns-1.6.12-doxyparse.pl-netbsd.patch &&
			./configure --prefix="$INSTALL_ROOT" \
				--disable-gost &&
			$MAKE &&
			$MAKE install
		) &&
		build_ok=1
		;;
esac

if [ "$build_ok" -eq 1 ]; then
	set_build_ok ldns || exit 1
	exit 0
fi

exit 1
