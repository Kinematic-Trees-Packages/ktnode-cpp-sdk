#!/usr/bin/env bash
set -euo pipefail
out="${KTM_BUILD_OUTPUT:-build/ktm-output}"
rm -rf "$out"
mkdir -p "$out"
cp -a README.md package.ktm.json scripts "$out"/
cp -a CMakeLists.txt include src examples tests "$out"/
echo "Built {{KTM_CREATE_PROJECT_NAME}} source package into $out"
