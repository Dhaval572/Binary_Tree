VCPKG_ROOT ?= $(HOME)/.local/share/vcpkg
export VCPKG_ROOT

.DEFAULT_GOAL := help

.PHONY: help setup configure debug release benchmark benchmark-quick docs clean distclean

help:
	@echo ""
	@echo " BinaryTree - Available Commands"
	@echo " --------------------------------"
	@echo " make setup            - Install dependencies and build Debug"
	@echo " make debug            - Configure and build Debug"
	@echo " make release          - Configure and build Release"
	@echo " make test             - Build Debug and run tests"
	@echo " make benchmark        - Full benchmark (~3min, for publishing)"
	@echo " make benchmark-quick  - Fast benchmark (~10s, for iteration)"
	@echo " make docs             - Generate Doxygen API documentation"
	@echo " make clean            - Remove build artifacts (keeps vcpkg_installed)"
	@echo " make distclean        - Remove entire build/ tree (incl. vcpkg_installed)"
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

# Fast iteration run — filtered to the benchmarks whose numbers
# appear in the doc. ~10s on a 2-core laptop.
benchmark-quick:
	cmake --preset release-bench
	cmake --build --preset release-bench
	./build/release-bench/binary_tree_benchmarks \
		--benchmark_filter='BM_(ValueAccess|IsValid|ChainInsert|DeepChainWalk)' \
		--benchmark_repetitions=3 \
		--benchmark_min_time=0.05s \
		--benchmark_report_aggregates_only=true

# Full run — produces the numbers quoted in the class @par Performance
# docblock. ~3min. Run only when updating the doc.
benchmark:
	cmake --preset release-bench
	cmake --build --preset release-bench
	taskset -c 0 ./build/release-bench/binary_tree_benchmarks \
		--benchmark_repetitions=10 \
		--benchmark_min_time=0.2s \
		--benchmark_report_aggregates_only=true

docs:
	cmake --preset release
	cmake --build --preset release --target docs

clean:
	@if [ -d build ]; then \
		rm -rf build/*/CMakeCache.txt build/*/CMakeFiles \
		       build/*/Makefile build/*/build.ninja build/*/.ninja_deps \
		       build/*/.ninja_log && \
		echo "Build artifacts cleaned (vcpkg_installed kept)."; \
	else \
		echo "Already clean. No build directory found."; \
	fi

distclean:
	@if [ -d build ]; then \
		rm -rf build/ && echo "Full build/ tree removed."; \
	else \
		echo "Already clean. No build directory found."; \
	fi