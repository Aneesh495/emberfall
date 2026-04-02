BUILD ?= build
CONFIG ?= Release
FRONTEND ?= ON
.PHONY: bootstrap doctor build test
bootstrap:
	cmake -S . -B "$(BUILD)" -G Ninja -DCMAKE_BUILD_TYPE="$(CONFIG)" -DEMBERFALL_FRONTEND="$(FRONTEND)"
doctor:
	@./scripts/doctor.sh
build: bootstrap
	cmake --build "$(BUILD)"
test: build
	ctest --test-dir "$(BUILD)" --output-on-failure
