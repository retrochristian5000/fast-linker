#!/usr/bin/env bash
. $(dirname $0)/common.inc

[ $MACHINE = $(uname -m) ] || skip

CLANG="${TEST_CLANG:-clang}"
AR="${TEST_LLVM_AR:-llvm-ar}"
$CLANG --version >& /dev/null || skip
$AR --version >& /dev/null || skip
echo 'int main() {}' | $CLANG -B. -flto=thin -o /dev/null -xc - >& /dev/null || skip

cat <<'EOF' | $CLANG -flto=thin -c -o $t/lib.o -xc -
int lto_value(void) { return 42; }
EOF
$AR rcs $t/libthin.a $t/lib.o

cat <<'EOF' | $CLANG -flto=thin -c -o $t/main.o -xc -
int lto_value(void);
int main(void) { return lto_value() == 42 ? 0 : 1; }
EOF

$CLANG -B. -flto=thin -o $t/exe $t/main.o $t/libthin.a
$QEMU $t/exe
