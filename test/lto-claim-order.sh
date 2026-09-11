#!/usr/bin/env bash
. $(dirname $0)/common.inc

[ $MACHINE = $(uname -m) ] || skip

CLANGXX="${TEST_CLANGXX:-clang++}"
$CLANGXX --version >& /dev/null || skip
echo 'int main() {}' | $CLANGXX -B. -flto -o /dev/null -xc++ - >& /dev/null || skip

# LLVM's LTO expects IR files in command-line order.  A linker-script input
# is discovered after ordinary command-line files, so this reproduces the
# ordering hazard that can make the losing COMDAT copy retain an unresolved
# internal initializer.
cat <<EOF > $t/a.h
int f();
template <typename T> struct S { static int x; };
template <typename T> int S<T>::x = f();
EOF

cat <<EOF | $CLANGXX -flto -c -o $t/a.o -xc++ -
#include "$t/a.h"
int f() { static int n; return ++n; }
int a() { return S<int>::x; }
EOF

cat <<EOF | $CLANGXX -flto -c -o $t/b.o -xc++ -
#include "$t/a.h"
int b() { return S<int>::x; }
EOF

cat <<EOF | $CLANGXX -c -o $t/c.o -xc++ -
#include <cstdio>
int a();
int b();
int main() { printf("%d %d\n", a(), b()); }
EOF

echo "INPUT($t/a.o)" > $t/script

$CLANGXX -B. -flto -o $t/exe $t/script $t/b.o $t/c.o
$QEMU $t/exe | grep '^1 1$'
