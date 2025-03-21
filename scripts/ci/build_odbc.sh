#!/usr/bin/env bash
# Builds SOCI ODBC backend in CI builds
#
# Copyright (c) 2013 Mateusz Loskot <mateusz@loskot.net>
#
source ${SOCI_SOURCE_DIR}/scripts/ci/common.sh

ODBC_TEST=${PWD}/../tests/odbc
if test ! -d ${ODBC_TEST}; then
    echo "ERROR: '${ODBC_TEST}' directory not found"
    exit 1
fi

# Disable ASAN -> see https://github.com/SOCI/soci/issues/1008
cmake ${SOCI_DEFAULT_CMAKE_OPTIONS} \
    -DSOCI_ASAN=OFF \
    -DSOCI_ODBC=ON \
   ..

run_make
