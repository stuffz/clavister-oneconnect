#!/bin/sh
# Runs inside ubuntu:24.04 (see build-deb.sh). Expects the repo mounted
# read-only at /repo and a writable output directory at /out.
set -eu

export DEBIAN_FRONTEND=noninteractive

apt-get update -qq
apt-get install -qq -y --no-install-recommends \
    g++ make pkg-config git dpkg-dev binutils \
    libopenconnect-dev libyaml-cpp-dev libsecret-1-dev qt6-base-dev >/dev/null

# noble's Qt (6.4) predates the return of upstream pkg-config files; provide
# the three .pc files the Makefile asks for.
if ! pkg-config --exists Qt6Widgets; then
    QT_INC=/usr/include/x86_64-linux-gnu/qt6
    PC_DIR=/tmp/qt6-pc
    mkdir -p "$PC_DIR"
    for module in Core Gui Widgets; do
        requires=""
        [ "$module" = Gui ] && requires="Qt6Core"
        [ "$module" = Widgets ] && requires="Qt6Gui Qt6Core"
        cat > "$PC_DIR/Qt6$module.pc" <<EOF
Name: Qt6 $module
Description: Qt $module module
Version: $(dpkg-query -W -f '${Version}' qt6-base-dev | cut -d+ -f1)
Requires: $requires
Cflags: -I$QT_INC -I$QT_INC/Qt$module
Libs: -lQt6$module
EOF
    done
    export PKG_CONFIG_PATH="$PC_DIR"
fi

git config --global safe.directory /repo
GIT_HASH="$(git -C /repo rev-parse --short HEAD)"
VERSION="0.1.0+git$(git -C /repo rev-list --count HEAD).$GIT_HASH"

# Build from a writable copy; /repo (and any host build output in it) stays
# untouched.
SRC=/build-src
git -C /repo archive HEAD | (mkdir -p "$SRC" && tar -x -C "$SRC")

make -C "$SRC" -j"$(nproc)" GIT_HASH="$GIT_HASH"

PKG=/pkg
make -C "$SRC" install DESTDIR="$PKG" PREFIX=/usr GIT_HASH="$GIT_HASH"

strip "$PKG/usr/bin/oneconnect" "$PKG/usr/bin/oneconnect-gui" \
    "$PKG/usr/lib/oneconnect/oneconnect-helper"

for png in "$SRC"/resources/icons/oneconnect-*.png; do
    size="$(basename "$png" .png | sed 's/oneconnect-//')"
    install -Dm644 "$png" \
        "$PKG/usr/share/icons/hicolor/${size}x${size}/apps/oneconnect.png"
done

DOC="$PKG/usr/share/doc/clavister-oneconnect"
install -Dm644 "$SRC/LICENSE" "$DOC/copyright"
install -Dm644 "$SRC/README.md" "$DOC/README.md"
for doc in "$SRC"/docs/*.md; do
    install -Dm644 "$doc" "$DOC/docs/$(basename "$doc")"
done
install -Dm644 "$SRC/resources/linux/49-clavister-oneconnect.rules" \
    "$DOC/examples/49-clavister-oneconnect.rules"

# Resolve library dependencies against what this container actually has.
mkdir -p /shlibs/debian
touch /shlibs/debian/control
cd /shlibs
DEPS="$(dpkg-shlibdeps -O "$PKG/usr/bin/oneconnect" "$PKG/usr/bin/oneconnect-gui" \
    "$PKG/usr/lib/oneconnect/oneconnect-helper" 2>/dev/null | sed 's/^shlibs:Depends=//')"

mkdir -p "$PKG/DEBIAN"
cat > "$PKG/DEBIAN/control" <<EOF
Package: clavister-oneconnect
Version: $VERSION
Architecture: amd64
Maintainer: stuffz <8736837+stuffz@users.noreply.github.com>
Section: net
Priority: optional
Depends: $DEPS, vpnc-scripts
Recommends: pkexec, polkitd, qt6-wayland
Installed-Size: $(du -sk --exclude=DEBIAN "$PKG" | cut -f1)
Description: VPN client for Clavister OneConnect gateways
 Console and Qt clients for Clavister OneConnect and other
 AnyConnect-compatible VPN gateways, built on libopenconnect. The GUI runs
 unprivileged and delegates tun device creation to a small pkexec-elevated
 helper.
EOF

dpkg-deb --build --root-owner-group "$PKG" \
    "/out/clavister-oneconnect_${VERSION}_amd64.deb" >/dev/null

dpkg-deb --info "/out/clavister-oneconnect_${VERSION}_amd64.deb"
dpkg-deb --contents "/out/clavister-oneconnect_${VERSION}_amd64.deb" | awk '{print $6}'
