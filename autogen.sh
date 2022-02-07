#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
PKG_NAME="vcdimager"


if grep "^AM_PROG_LIBTOOL" configure.ac >/dev/null; then
	echo "Running libtoolize..."
	libtoolize --force --copy
fi

echo "Running aclocal $aclocalinclude ..."
mkdir -p m4
aclocal

if grep "^AM_CONFIG_HEADER" configure.ac >/dev/null; then
	echo "Running autoheader..."
	autoheader --force
fi

echo "Running automake ..."
automake --add-missing --copy --force
echo "Running autoconf ..."
autoconf --force

touch $srcdir/docs/version.texi
touch $srcdir/docs/version-vcd-info.texi
touch $srcdir/docs/version-vcdxrip.texi
test -f $srcdir/docs/stamp-vti && rm $srcdir/docs/stamp-vti
test -f $srcdir/docs/stamp-1 && rm $srcdir/docs/stamp-1
test -f $srcdir/docs/stamp-2 && rm $srcdir/docs/stamp-2
  
rm -rf autom4te.cache
