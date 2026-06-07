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

PACKAGE_DIR ?= dist
SCENE ?= assets/levels/01-aperture.scene
ARGS ?=
.PHONY: editor game demo cook data package-game net-test
editor: bootstrap
	cmake --build "$(BUILD)" --target emberfall-editor
	"$(BUILD)/emberfall-editor" "$(SCENE)" $(ARGS)
game: bootstrap
	cmake --build "$(BUILD)" --target lantern-reach
	"$(BUILD)/lantern-reach" $(ARGS)
demo: game
cook: bootstrap
	cmake --build "$(BUILD)" --target emberfall_levels
data: cook
package-game: bootstrap
	cmake --build "$(BUILD)" --target lantern-reach
	./scripts/package.sh "$(BUILD)" "$(PACKAGE_DIR)"
net-test: bootstrap
	cmake --build "$(BUILD)" --target network_tests
	"$(BUILD)/network_tests" assets/levels

REPLAY ?=
.PHONY: replay-check
replay-check: cook
	test -n "$(REPLAY)"
	"$(BUILD)/ember-tool" replay-check "$(BUILD)/levels" "$(REPLAY)"

.PHONY: bench stress render-bench physics-bench
bench: bootstrap
	cmake --build "$(BUILD)" --target runtime_bench
	"$(BUILD)/runtime_bench" ecs
physics-bench: bootstrap
	cmake --build "$(BUILD)" --target runtime_bench
	"$(BUILD)/runtime_bench" physics
render-bench: bootstrap
	cmake --build "$(BUILD)" --target gpu_stress
	"$(BUILD)/gpu_stress" "$(BUILD)/shaders"
stress: bench physics-bench render-bench

RUN_ID ?=
PROFILE ?= full
EVIDENCE_ROOT ?= evidence
EVIDENCE ?=
.PHONY: acceptance verify release controls docs
acceptance:
	test -n "$(RUN_ID)"
	@if [ "$(PROFILE)" != full ]; then $(MAKE) bootstrap; fi
	python3 scripts/evidence.py --run-id "$(RUN_ID)" --profile "$(PROFILE)" --build "$(BUILD)" --output "$(EVIDENCE_ROOT)"
verify:
	test -n "$(EVIDENCE)"
	python3 scripts/verify_evidence.py "$(EVIDENCE)" . "$(BUILD)/ember-tool"
controls:
	test -n "$(EVIDENCE)"
	python3 scripts/evidence_controls.py "$(EVIDENCE)" "$$(pwd)" "$$(pwd)/$(BUILD)/ember-tool" "$(EVIDENCE).controls.jsonl"
release: verify
	@python3 -c 'import json, pathlib, sys; meta = json.loads((pathlib.Path(sys.argv[1])/"ACCEPTANCE.json").read_text()); sys.exit(0 if meta.get("profile") == "full" and meta.get("full_pass") is True else "A verified full campaign is required for release")' "$(EVIDENCE)"
	$(MAKE) package-game
docs:
	python3 scripts/check_docs.py
