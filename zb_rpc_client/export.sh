#!/usr/bin/env bash
# Source this file to set LWIP_DIR for building ppp_lwip_server.
#
#   . ./export.sh

if [ -n "${BASH_SOURCE-}" ] && [ "${BASH_SOURCE[0]}" = "${0}" ]; then
    echo "Source this script, do not execute it: . ${BASH_SOURCE[0]}" >&2
    exit 1
fi

if [ -n "${IDF_PATH:-}" ]; then
    export LWIP_DIR="${IDF_PATH}/components/lwip/lwip"
elif [ -f "$(dirname "${BASH_SOURCE[0]}")/../../components/lwip/lwip/src/Filelists.cmake" ]; then
    _idf_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
    export LWIP_DIR="${_idf_root}/components/lwip/lwip"
fi

if [ -z "${LWIP_DIR:-}" ] || [ ! -f "${LWIP_DIR}/src/Filelists.cmake" ]; then
    echo "LWIP_DIR is not set to a valid lwIP tree." >&2
    echo "Set LWIP_DIR manually or source from an ESP-IDF checkout (IDF_PATH)." >&2
    return 1 2>/dev/null || exit 1
fi

export LWIP_CONTRIB_DIR="${LWIP_DIR}/contrib"
