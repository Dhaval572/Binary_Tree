VCPKG_ROOT ?= $(HOME)/.local/share/vcpkg
export VCPKG_ROOT

.DEFAULT_GOAL := help

.PHONY: setup help configure debug release test benchmark docs clean

help:
	@echo ""
	@echo " BinaryTree - Available Commands"
	@echo " --------------------------------"
	@echo " make setup       - Install dependencies and build Debug"
	@echo " make debug       - Configure and build Debug"
	@echo " make release     - Configure and build Release"
	@echo " make test        - Build Debug and run tests"
	@echo " make benchmark   - Build and run Release benchmarks"
	@echo " make docs        - Generate Doxygen API documentation"
	@echo " make clean       - Clean Debug and Release builds"
	@echo ""

setup:
	@./scripts/linux/setup.sh

configure:
	cmake --preset debug

debug:
	cmake --preset debug
	cmake --build --preset debug

release:
	cmake --preset release
	cmake --build --preset release

test:
	cmake --preset debug
	cmake --build --preset debug
	ctest --preset debug

benchmark:
	cmake --preset release
	cmake --build --preset release --target run_benchmarks

docs:
	cmake --preset debug
	cmake --build --preset debug --target docs

clean:
	@if [ -d build ]; then \
		rm -rf build/ && echo "Build directory cleaned successfully."; \
	else \
		echo "Already clean. No build directory found."; \
	fi
