#!/usr/bin/env bash
. $(dirname $0)/common.inc

# LLVM keeps claimed COMDAT groups in its LTO output.  A regular archive
# member extracted only after LTO must not resurrect a second copy of a group
# already claimed by IR.
[ $MACHINE = $(uname -m) ] || skip

CLANG="${TEST_CLANG:-clang}"
$CLANG --version >& /dev/null || skip
echo 'int main() {}' | $CLANG -B. -flto -o /dev/null -xc - >& /dev/null || skip
echo '__int128 x;' | $CLANG -c -o /dev/null -xc - >& /dev/null || skip

cat <<'EOF' > $t/uniq.h
inline int bump() {
  static int cnt;
  return ++cnt;
}
EOF

cat <<'EOF' | $CLANG -O2 -I$t -c -o $t/a.o -xc++ -
#include "uniq.h"
extern "C" __int128 __divti3(__int128 a, __int128 b) {
  return (long long)a / (long long)b + bump();
}
EOF

ar rcs $t/libutil.a $t/a.o

cat <<'EOF' | $CLANG -O2 -I$t -flto -c -o $t/main.o -xc++ -
#include "uniq.h"
volatile long long lo = 21, d = 7;
int main() {
  bump();
  __int128 a = lo, b = d;
  return a / b == 5 ? 0 : 1;
}
EOF

$CLANG -B. -O2 -flto -o $t/exe $t/main.o $t/libutil.a
$QEMU $t/exe
