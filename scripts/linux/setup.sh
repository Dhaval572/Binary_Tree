#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$PROJECT_ROOT"

# Initialize environment.
source "${PROJECT_ROOT}/scripts/linux/env.sh"

# Install vcpkg if missing.
if [[ ! -d "${VCPKG_ROOT}/.git" ]]; then
    echo "Cloning vcpkg..."

    mkdir -p "$(dirname "${VCPKG_ROOT}")"
    git clone https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}"
fi

# Bootstrap vcpkg if necessary.
if [[ ! -x "${VCPKG_ROOT}/vcpkg" ]]; then
    echo "Bootstrapping vcpkg..."
    "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics
fi

echo "Configuring BinaryTree..."
cmake --preset debug

echo "Building BinaryTree..."
cmake --build --preset debug

echo "Running tests..."
ctest --preset debug

echo "Setup completed successfully!"