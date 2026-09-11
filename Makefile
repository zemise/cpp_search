# LIS Workbench — Makefile for macOS cross-compilation with MinGW-w64
# Maven-style goals: clean compile test package verify install run help
# Default target: help

BUILD_DIR     := build/windows-x64
TOOLCHAIN     := $(CURDIR)/cmake/toolchains/mingw-w64-x86_64.cmake
VERSION       := $(shell grep -o '"v[^"]*"' src/version.h | head -1 | tr -d '"')
LABELPRINT    := $(shell test -d "../../020 LabelPrint/LabelPrint" && cd "../../020 LabelPrint/LabelPrint" && pwd || echo "")

CMAKE_FLAGS   := -DCMAKE_BUILD_TYPE=Release -DBUILD_QT_GUI=OFF

# Packaging
PACKAGE_DIR    := out/windows
UPDATE_DIR     := $(PACKAGE_DIR)/update/updates
PACKAGE_WORK   := $(PACKAGE_DIR)/package-work
DIST_DIR       := $(PACKAGE_DIR)/dist
STAGE_DIR      := $(PACKAGE_DIR)/update/package-root
ZIP_NAME       := LISWorkbench-$(VERSION)-win7-win11.zip
ZIP_PATH       := $(UPDATE_DIR)/$(ZIP_NAME)
MANIFEST_PATH  := $(UPDATE_DIR)/manifest.json
SHA256         := $(shell which sha256sum 2>/dev/null || echo "shasum -a 256")

.DEFAULT_GOAL := help

# ── help ───────────────────────────────────────────────────────
.PHONY: help
help:
	@echo ""
	@echo " LIS Workbench — Maven-style cross-compilation for macOS"
	@echo " Version: $(VERSION)"
	@echo ""
	@echo " Goals:"
	@echo "   make clean      Remove $(BUILD_DIR)"
	@echo "   make compile    Cross-compile lis_workbench.exe + Updater.exe (MinGW)"
	@echo "   make test       Run tests (placeholder)"
	@echo "   make package    Create NSIS installer + update zip + manifest.json"
	@echo "   make verify     Verify binaries and version consistency"
	@echo "   make install    Copy artifacts to out/windows/dist/"
	@echo "   make run        Show run instructions (macOS cannot run .exe)"
	@echo "   make help       Print this help"
	@echo ""
	@echo " Usage:"
	@echo "   make compile"
	@echo "   make clean compile"
	@echo "   make install"
	@echo ""

# ── clean ──────────────────────────────────────────────────────
.PHONY: clean
clean:
	@echo "[clean] Removing $(BUILD_DIR) and out/windows ..."
	rm -rf $(BUILD_DIR) $(PACKAGE_DIR)

# ── compile ────────────────────────────────────────────────────
.PHONY: configure
configure:
	@echo "[cmake] Configuring with MinGW toolchain ..."
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake ../.. $(CMAKE_FLAGS) -DCMAKE_TOOLCHAIN_FILE="$(TOOLCHAIN)" -DLIS_LABELPRINT_DIR="$(LABELPRINT)"

.PHONY: compile
compile: configure
	@echo "[compile] Building lis_workbench.exe + Updater.exe ..."
	cmake --build $(BUILD_DIR) -j
	@echo "  -> $(BUILD_DIR)/lis_workbench.exe"
	@echo "  -> $(BUILD_DIR)/Updater.exe"
	@echo "  -> $(BUILD_DIR)/result_search.exe"

# ── test ───────────────────────────────────────────────────────
.PHONY: test
test: compile
	@echo "[test] No automated tests defined. Skipping."

# ── package ────────────────────────────────────────────────────
.PHONY: package
package: compile
	@echo "[package] Building NSIS installer for $(VERSION) ..."
	rm -rf $(PACKAGE_WORK)
	mkdir -p $(PACKAGE_WORK)
	cd packaging && makensis \
		-DAPP_VERSION=$(VERSION) \
		-DAPP_EXE=lis_workbench.exe \
		-DBUILD_DIR=../$(BUILD_DIR) \
		-DOUTPUT_DIR=../$(PACKAGE_WORK) \
		-DOUTPUT_NAME=LISWorkbench-Setup-$(VERSION)-win7-win11.exe \
		LISWorkbench.nsi
	@echo "  -> $(PACKAGE_WORK)/LISWorkbench-Setup-$(VERSION)-win7-win11.exe"

	@echo "[package] Creating update zip ..."
	rm -rf $(STAGE_DIR) $(ZIP_PATH)
	mkdir -p $(STAGE_DIR) $(UPDATE_DIR)
	cp $(BUILD_DIR)/lis_workbench.exe $(STAGE_DIR)/
	cp $(BUILD_DIR)/Updater.exe       $(STAGE_DIR)/
	-test -f $(BUILD_DIR)/result_search.exe && \
		cp $(BUILD_DIR)/result_search.exe $(STAGE_DIR)/
	cd $(STAGE_DIR) && zip -q ../updates/$(ZIP_NAME) *.exe
	@echo "  -> $(ZIP_PATH)"

	@echo "[package] Generating manifest.json ..."
	@hash=$$($(SHA256) $(ZIP_PATH) | cut -d' ' -f1) ; \
	size=$$(wc -c < $(ZIP_PATH) | tr -d ' ') ; \
	published=$$(date -u +"%Y-%m-%dT%H:%M:%SZ") ; \
	echo '{' > $(MANIFEST_PATH) ; \
	echo '  "appId": "lis-workbench",' >> $(MANIFEST_PATH) ; \
	echo '  "version": "$(VERSION)",' >> $(MANIFEST_PATH) ; \
	echo '  "channel": "stable",' >> $(MANIFEST_PATH) ; \
	echo '  "minUpdaterVersion": "1.0.0",' >> $(MANIFEST_PATH) ; \
	echo '  "publishedAt": "'$$published'",' >> $(MANIFEST_PATH) ; \
	echo '  "package": {' >> $(MANIFEST_PATH) ; \
	echo '    "file": "$(ZIP_NAME)",' >> $(MANIFEST_PATH) ; \
	echo '    "sha256": "'$$hash'",' >> $(MANIFEST_PATH) ; \
	echo '    "size": '$$size >> $(MANIFEST_PATH) ; \
	echo '  },' >> $(MANIFEST_PATH) ; \
	echo '  "notes": []' >> $(MANIFEST_PATH) ; \
	echo '}' >> $(MANIFEST_PATH)
	@echo "  -> $(MANIFEST_PATH)"
	@echo "==> Installer  : $(PACKAGE_WORK)/LISWorkbench-Setup-$(VERSION)-win7-win11.exe"
	@echo "==> Update zip : $(ZIP_PATH)"
	@echo "==> Manifest   : $(MANIFEST_PATH)"

# ── verify ─────────────────────────────────────────────────────
.PHONY: verify
verify: compile
	@echo "[verify] Checking artifacts ..."
	@errors=0 ; \
	for f in $(BUILD_DIR)/lis_workbench.exe \
	         $(BUILD_DIR)/Updater.exe ; do \
		if [ -f "$$f" ]; then echo "  PASS: $$f"; else echo "  FAIL: $$f missing"; errors=$$((errors+1)); fi ; \
	done ; \
	if [ -f "$(ZIP_PATH)" ]; then echo "  PASS: $(ZIP_PATH)"; else true; fi ; \
	if [ -f "$(MANIFEST_PATH)" ]; then echo "  PASS: $(MANIFEST_PATH)"; else true; fi ; \
	if [ "$$errors" -gt 0 ]; then echo "[verify] FAILED"; exit 1; fi
	@echo "[verify] PASSED — version=$(VERSION)"

# ── install ────────────────────────────────────────────────────
.PHONY: install
install: package
	@echo "[install] Copying to $(DIST_DIR) ..."
	mkdir -p $(DIST_DIR)/updates
	cp $(BUILD_DIR)/lis_workbench.exe     $(DIST_DIR)/
	cp $(BUILD_DIR)/Updater.exe           $(DIST_DIR)/
	-test -f $(BUILD_DIR)/result_search.exe && \
		cp $(BUILD_DIR)/result_search.exe  $(DIST_DIR)/
	-test -d $(PACKAGE_WORK) && \
		cp $(PACKAGE_WORK)/LISWorkbench-Setup-$(VERSION)-win7-win11.exe $(DIST_DIR)/
	-test -f $(ZIP_PATH) && cp $(ZIP_PATH) $(DIST_DIR)/updates/
	-test -f $(MANIFEST_PATH) && cp $(MANIFEST_PATH) $(DIST_DIR)/updates/
	@echo "  Artifacts: $(DIST_DIR)/"
	@ls -1 $(DIST_DIR)

# ── run ────────────────────────────────────────────────────────
.PHONY: run
run: compile
	@echo "[run] macOS cannot run Windows .exe directly."
	@echo "  Copy $(BUILD_DIR)/*.exe to a Windows machine, or use Wine:"
	@echo "    wine $(BUILD_DIR)/lis_workbench.exe"
