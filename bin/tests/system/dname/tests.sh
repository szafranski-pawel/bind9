#!/bin/sh
#
# Copyright (C) 2011, 2012, 2016, 2017  Internet Systems Consortium, Inc. ("ISC")
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# $Id: tests.sh,v 1.2 2011/03/18 21:14:19 fdupont Exp $

SYSTEMTESTTOP=..
. $SYSTEMTESTTOP/conf.sh

status=0
n=0

echo "I:checking short dname from authoritative"
ret=0
$DIG a.short-dname.example @10.53.0.2 a -p 5300 > dig.out.ns2.short || ret=1
grep "status: NOERROR" dig.out.ns2.short > /dev/null || ret=1
if [ $ret != 0 ]; then echo "I:failed"; fi
status=`expr $status + $ret`

echo "I:checking short dname from recursive"
ret=0
$DIG a.short-dname.example @10.53.0.4 a -p 5300 > dig.out.ns4.short || ret=1
grep "status: NOERROR" dig.out.ns4.short > /dev/null || ret=1
if [ $ret != 0 ]; then echo "I:failed"; fi
status=`expr $status + $ret`

echo "I:checking long dname from authoritative"
ret=0
$DIG a.long-dname.example @10.53.0.2 a -p 5300 > dig.out.ns2.long || ret=1
grep "status: NOERROR" dig.out.ns2.long > /dev/null || ret=1
if [ $ret != 0 ]; then echo "I:failed"; fi
status=`expr $status + $ret`

echo "I:checking long dname from recursive"
ret=0
$DIG a.long-dname.example @10.53.0.4 a -p 5300 > dig.out.ns4.long || ret=1
grep "status: NOERROR" dig.out.ns4.long > /dev/null || ret=1
if [ $ret != 0 ]; then echo "I:failed"; fi
status=`expr $status + $ret`

echo "I:checking (too) long dname from authoritative"
ret=0
$DIG 01234567890123456789012345678901234567890123456789.longlonglonglonglonglonglonglonglonglonglonglonglonglonglong.longlonglonglonglonglonglonglonglonglonglonglonglonglonglong.longlonglonglonglonglonglonglonglonglonglonglonglonglonglong.long-dname.example @10.53.0.2 a -p 5300 > dig.out.ns2.toolong || ret=1
grep "status: YXDOMAIN" dig.out.ns2.toolong > /dev/null || ret=1
if [ $ret != 0 ]; then echo "I:failed"; fi
status=`expr $status + $ret`

echo "I:checking (too) long dname from recursive with cached DNAME"
ret=0 
$DIG 01234567890123456789012345678901234567890123456789.longlonglonglonglonglonglonglonglonglonglonglonglonglonglong.longlonglonglonglonglonglonglonglonglonglonglonglonglonglong.longlonglonglonglonglonglonglonglonglonglonglonglonglonglong.long-dname.example @10.53.0.4 a -p 5300 > dig.out.ns4.cachedtoolong || ret=1
grep "status: YXDOMAIN" dig.out.ns4.cachedtoolong > /dev/null || ret=1
grep '^long-dname\.example\..*DNAME.*long' dig.out.ns4.cachedtoolong > /dev/null || ret=1
if [ $ret != 0 ]; then echo "I:failed"; fi
status=`expr $status + $ret`

echo "I:checking (too) long dname from recursive without cached DNAME"
ret=0
$DIG 01234567890123456789012345678901234567890123456789.longlonglonglonglonglonglonglonglonglonglonglonglonglonglong.longlonglonglonglonglonglonglonglonglonglonglonglonglonglong.longlonglonglonglonglonglonglonglonglonglonglonglonglong.toolong-dname.example @10.53.0.4 a -p 5300 > dig.out.ns4.uncachedtoolong || ret=1
grep "status: YXDOMAIN" dig.out.ns4.uncachedtoolong > /dev/null || ret=1
grep '^toolong-dname\.example\..*DNAME.*long' dig.out.ns4.uncachedtoolong > /dev/null || ret=1
if [ $ret != 0 ]; then echo "I:failed"; fi
status=`expr $status + $ret`

echo "I:checking cname to dname from authoritative"
ret=0
$DIG cname.example @10.53.0.2 a -p 5300 > dig.out.ns2.cname
grep "status: NOERROR" dig.out.ns2.cname > /dev/null || ret=1
if [ $ret != 0 ]; then echo "I:failed"; fi
status=`expr $status + $ret`

echo "I:checking cname to dname from recursive"
ret=0
$DIG cname.example @10.53.0.4 a -p 5300 > dig.out.ns4.cname
grep "status: NOERROR" dig.out.ns4.cname > /dev/null || ret=1
grep '^cname.example.' dig.out.ns4.cname > /dev/null || ret=1
grep '^cnamedname.example.' dig.out.ns4.cname > /dev/null || ret=1
grep '^a.cnamedname.example.' dig.out.ns4.cname > /dev/null || ret=1
grep '^a.target.example.' dig.out.ns4.cname > /dev/null || ret=1
if [ $ret != 0 ]; then echo "I:failed"; fi
status=`expr $status + $ret`

n=`expr $n + 1`
echo "I:checking dname is returned with synthesized cname before dname ($n)"
ret=0
$DIG @10.53.0.4 -p 5300 name.synth-then-dname.example.broken A > dig.out.test$n
grep "status: NXDOMAIN" dig.out.test$n > /dev/null || ret=1
grep '^name.synth-then-dname\.example\.broken\..*CNAME.*name.$' dig.out.test$n > /dev/null || ret=1
grep '^synth-then-dname\.example\.broken\..*DNAME.*\.$' dig.out.test$n > /dev/null || ret=1
if [ $ret != 0 ]; then echo "I:failed"; fi
status=`expr $status + $ret`

n=`expr $n + 1`
echo "I:checking dname is returned with cname to synthesized cname before dname ($n)"
ret=0
$DIG @10.53.0.4 -p 5300 cname-to-synth2-then-dname.example.broken A > dig.out.test$n
grep "status: NXDOMAIN" dig.out.test$n > /dev/null || ret=1
grep '^cname-to-synth2-then-dname\.example\.broken\..*CNAME.*name\.synth2-then-dname\.example\.broken.$' dig.out.test$n > /dev/null || ret=1
grep '^name\.synth2-then-dname\.example\.broken\..*CNAME.*name.$' dig.out.test$n > /dev/null || ret=1
grep '^synth2-then-dname\.example\.broken\..*DNAME.*\.$' dig.out.test$n > /dev/null || ret=1
if [ $ret != 0 ]; then echo "I:failed"; fi
status=`expr $status + $ret`

echo "I:exit status: $status"
[ $status -eq 0 ] || exit 1
