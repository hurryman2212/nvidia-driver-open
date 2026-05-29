###########################################################################
# This is the top level makefile for the NVIDIA Linux kernel module source
# package.
#
# To build: run `make modules`
# To install the kernel modules through DKMS: run (as root) `make install`
###########################################################################

###########################################################################
# variables
###########################################################################

nv_kernel_o                = src/nvidia/$(OUTPUTDIR)/nv-kernel.o
nv_kernel_o_binary         = kernel-open/nvidia/nv-kernel.o_binary

nv_modeset_kernel_o        = src/nvidia-modeset/$(OUTPUTDIR)/nv-modeset-kernel.o
nv_modeset_kernel_o_binary = kernel-open/nvidia-modeset/nv-modeset-kernel.o_binary

DKMS_CONF ?= dkms.conf
DKMS ?= dkms
DKMS_PACKAGE_NAME := $(shell bash -c '. "$(DKMS_CONF)" >/dev/null 2>&1; printf "%s" "$$PACKAGE_NAME"')
DKMS_PACKAGE_VERSION := $(shell bash -c '. "$(DKMS_CONF)" >/dev/null 2>&1; printf "%s" "$$PACKAGE_VERSION"')

###########################################################################
# rules
###########################################################################

include utils.mk

.PHONY: all
all: modules

###########################################################################
# nv-kernel.o is the OS agnostic portion of nvidia.ko
###########################################################################

.PHONY: $(nv_kernel_o)
$(nv_kernel_o):
	$(MAKE) -C src/nvidia

$(nv_kernel_o_binary): $(nv_kernel_o)
	cd $(dir $@) && ln -sf ../../$^ $(notdir $@)


###########################################################################
# nv-modeset-kernel.o is the OS agnostic portion of nvidia-modeset.ko
###########################################################################

.PHONY: $(nv_modeset_kernel_o)
$(nv_modeset_kernel_o):
	$(MAKE) -C src/nvidia-modeset

$(nv_modeset_kernel_o_binary): $(nv_modeset_kernel_o)
	cd $(dir $@) && ln -sf ../../$^ $(notdir $@)


###########################################################################
# After the OS agnostic portions are built, descend into kernel-open/ and build
# the kernel modules with kbuild.
###########################################################################

.PHONY: modules
modules: $(nv_kernel_o_binary) $(nv_modeset_kernel_o_binary)
	$(MAKE) -C kernel-open modules

###########################################################################
# Install the kernel modules through DKMS.
###########################################################################

.PHONY: check-dkms-package install modules_install uninstall
check-dkms-package:
	@test -n "$(DKMS_PACKAGE_NAME)" || \
		(echo "Unable to read PACKAGE_NAME from $(DKMS_CONF)" >&2; exit 1)
	@test -n "$(DKMS_PACKAGE_VERSION)" || \
		(echo "Unable to read PACKAGE_VERSION from $(DKMS_CONF)" >&2; exit 1)

install: check-dkms-package
	$(DKMS) install .

modules_install: install

uninstall: check-dkms-package
	@status="$$($(DKMS) status -m "$(DKMS_PACKAGE_NAME)")" || exit $$?; \
	versions="$$(printf '%s\n' "$$status" | \
		awk -F '[/,:]' -v package="$(DKMS_PACKAGE_NAME)" \
			'$$1 == package { print $$2 }' | sort -u)"; \
	if [ -z "$$versions" ]; then \
		echo "No DKMS versions of $(DKMS_PACKAGE_NAME) found."; \
		exit 0; \
	fi; \
	for version in $$versions; do \
		$(DKMS) remove -m "$(DKMS_PACKAGE_NAME)" -v "$$version" --all || \
			exit $$?; \
	done

###########################################################################
# clean
###########################################################################

.PHONY: clean
clean: nvidia.clean nvidia-modeset.clean kernel-open.clean

.PHONY: nvidia.clean
nvidia.clean:
	$(MAKE) -C src/nvidia clean

.PHONY: nvidia-modeset.clean
nvidia-modeset.clean:
	$(MAKE) -C src/nvidia-modeset clean

.PHONY: kernel-open.clean
kernel-open.clean:
	$(MAKE) -C kernel-open clean
