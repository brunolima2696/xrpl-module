#!/usr/bin/env bash
# Shared compilation entry point for Docker and the remote CI runner.
set -euo pipefail

build_stage="${1:-image}"
build_type="${BUILD_TYPE:-Debug}"
build_jobs="${BUILD_JOBS:-4}"
test_suites="${TEST_SUITES-LoanSet,Vault,Feature,ServerDefinitions}"

case "${build_stage}" in
    dependencies|image) ;;
    *) echo "Stage must be dependencies or image." >&2; exit 2 ;;
esac
case "${build_type}" in
    Debug|Release) ;;
    *) echo "BUILD_TYPE must be Debug or Release." >&2; exit 2 ;;
esac
if ! [[ "${build_jobs}" =~ ^[1-9][0-9]*$ ]]; then
    echo "BUILD_JOBS must be a positive integer." >&2
    exit 2
fi
if [[ -z "${test_suites}" ]]; then
    echo "TEST_SUITES must not be empty." >&2
    exit 2
fi

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_dir}"
bash conan/init.sh
mkdir -p .build
cd .build

conan install .. \
    --output-folder . \
    --build missing \
    --conf "tools.build:jobs=${build_jobs}" \
    --settings "build_type=${build_type}" \
    --options:host='&:tests=True' \
    --options:host='&:xrpld=True'

if [[ "${build_stage}" == dependencies ]]; then
    exit 0
fi

cmake -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -Dxrpld=ON \
    -Dtests=ON \
    ..
cmake --build . --target xrpld --parallel "${build_jobs}"
./xrpld --unittest="${test_suites}" 2>&1 | tee pistis-tests.log
