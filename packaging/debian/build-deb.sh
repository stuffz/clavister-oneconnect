#!/bin/sh
# Builds a .deb for Ubuntu 24.04 (noble) inside a docker container:
#
#   packaging/debian/build-deb.sh
#
# The package must be compiled against noble's glibc, yaml-cpp and Qt, so the
# build runs in ubuntu:24.04 regardless of the host distribution. The .deb
# lands in dist/, which `make clean` leaves alone.
set -eu

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$REPO/dist"

# DOCKER may be multi-word, e.g. DOCKER="sudo docker"; word splitting is wanted.
# shellcheck disable=SC2086
exec ${DOCKER:-docker} run --rm \
    -v "$REPO":/repo:ro \
    -v "$REPO/dist":/out \
    -w /repo \
    ubuntu:24.04 \
    sh /repo/packaging/debian/build-inside.sh
