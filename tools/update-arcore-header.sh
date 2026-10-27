#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
version=1.56.0
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/pocketar-header.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT
source_url="https://raw.githubusercontent.com/google-ar/arcore-android-sdk/v$version/libraries/include/arcore_c_api.h"
curl --fail --location --retry 3 --output "$work_dir/arcore_c_api.h" "$source_url"
if ! grep -q '^#define ARCORE_C_API_H_' "$work_dir/arcore_c_api.h"; then
    echo 'Downloaded file is not an ARCore C API header' >&2
    exit 1
fi
mkdir -p thirdparty/arcore/include
cp "$work_dir/arcore_c_api.h" thirdparty/arcore/include/arcore_c_api.h
{
    echo "ARCore SDK $version"
    echo "$source_url"
    shasum -a 256 thirdparty/arcore/include/arcore_c_api.h
} > thirdparty/arcore/header-source.txt
echo "Updated ARCore header from $source_url"
