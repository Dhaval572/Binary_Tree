#!/usr/bin/env bash

if [[ -z "${VCPKG_ROOT:-}" ]]; then
    export VCPKG_ROOT="${HOME}/.local/share/vcpkg"
fi