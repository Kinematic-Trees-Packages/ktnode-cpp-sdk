#!/usr/bin/env bash
set -euo pipefail
: "${KTM_BUILD_OUTPUT:?KTM_BUILD_OUTPUT is required}"
rm -rf "${KTM_BUILD_OUTPUT:?}"/*
mkdir -p "$KTM_BUILD_OUTPUT"
cp -a boilerplate "$KTM_BUILD_OUTPUT/boilerplate"
mkdir -p "$KTM_BUILD_OUTPUT/scripts"
cp -a scripts/package.sh "$KTM_BUILD_OUTPUT/scripts/package.sh"
if [ -d share ]; then cp -a share "$KTM_BUILD_OUTPUT/share"; fi
if [ -f package-hygiene.txt ]; then cp -a package-hygiene.txt "$KTM_BUILD_OUTPUT/package-hygiene.txt"; fi
