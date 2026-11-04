#!/usr/bin/env bash
# S-035 harness build (s035_combo.cpp alongside): clang-19 ASan in WSL over a private copy of a
# tree's src/. usage: build.sh <tree, a /mnt/c path> <tag>; the binary is ~/s035_obj_<tag>/s035_combo.
# Then: ~/s035_obj_<tag>/s035_combo outside|combo|closefree
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$1
TAG=$2
R=$HOME/s035_tree_$TAG
O=$HOME/s035_obj_$TAG
mkdir -p "$R" "$O"
rsync -a --delete --exclude '/build' --include '/src/***' --exclude '*' "$SRC/" "$R/"
CXX=${CXX:-clang++-19}
FL="-fno-pic -std=gnu++17 -g -O1 -fno-omit-frame-pointer -fsanitize=address -fexceptions -fno-strict-aliasing -fsigned-char -mms-bitfields -fno-delete-null-pointer-checks -Wno-invalid-offsetof -D_DEBUG -DBANISHED_UNIT_TEST -fno-rtti"
INC="-I$R/src/game -I$R/src/engine -I$R/src/render/common -I$R/src/app -I$R/src/diag -I$R/src/platform/common -I$R/src"
SRCS="$R/src/engine/ui/combodialog.cpp $R/src/engine/ui/comboelement.cpp"
if [ -f "$R/src/engine/ui/combodialog_close.cpp" ]; then
    SRCS="$SRCS $R/src/engine/ui/combodialog_close.cpp"
    FL="$FL -DS035_HAVE_FORGET"
fi
OBJS=""
for s in $SRCS "$HERE/s035_combo.cpp"; do
    o="$O/$(basename "$s" .cpp).o"
    $CXX $FL $INC -c "$s" -o "$o"
    OBJS="$OBJS $o"
done
# Only the stubbed bodies run; the rest of what the two vtables name stays unresolved (address 0).
$CXX -fsanitize=address -no-pie -o "$O/s035_combo" $OBJS -Wl,--unresolved-symbols=ignore-all
echo "BUILT $O/s035_combo"
