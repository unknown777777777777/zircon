# Copyright 2018 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

ifeq ($(PLATFORM_VID),)
$(error PLATFORM_VID not defined)
endif
ifeq ($(PLATFORM_PID),)
$(error PLATFORM_PID not defined)
endif
ifeq ($(PLATFORM_BOARD_NAME),)
$(error PLATFORM_BOARD_NAME not defined)
endif
ifeq ($(PLATFORM_MDI_SRCS),)
$(error PLATFORM_MDI_SRCS not defined)
endif

BOARD_MDI := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-mdi.bin
BOARD_PLATFORM_ID := --vid $(PLATFORM_VID) --pid $(PLATFORM_PID) --board $(PLATFORM_BOARD_NAME)
BOARD_ZIRCON := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-zircon.bin
BOARD_KERNEL_BOOTDATA := $(BUILDDIR)/$(PLATFORM_BOARD_NAME)-kernel-bootdata.bin

$(BOARD_MDI): PLATFORM_MDI_SRCS:=$(PLATFORM_MDI_SRCS)
$(BOARD_ZIRCON): BOARD_MDI:=$(BOARD_MDI)
$(BOARD_ZIRCON): BOARD_ZIRCON:=$(BOARD_ZIRCON)
$(BOARD_ZIRCON): BOARD_PLATFORM_ID:=$(BOARD_PLATFORM_ID)
$(BOARD_KERNEL_BOOTDATA): BOARD_MDI:=$(BOARD_MDI)
$(BOARD_KERNEL_BOOTDATA): BOARD_PLATFORM_ID:=$(BOARD_PLATFORM_ID)

# rule for building MDI binary blob
$(BOARD_MDI): $(MDIGEN) $(PLATFORM_MDI_SRCS) $(MDI_INCLUDES)
	$(call BUILDECHO,generating $@)
	@$(MKDIR)
	$(NOECHO)$(MDIGEN) -o $@ $(PLATFORM_MDI_SRCS)

GENERATED += $(BOARD_MDI)
EXTRA_BUILDDEPS += $(BOARD_MDI)

# full bootdata for standalone zircon build
$(BOARD_ZIRCON): $(MKBOOTFS) $(OUTLKBIN) $(BOARD_MDI) $(USER_BOOTDATA)
	$(call BUILDECHO,generating $@)
	@$(MKDIR)
	$(NOECHO)$(MKBOOTFS) $(BOARD_PLATFORM_ID) -o $@ $(OUTLKBIN) $(BOARD_MDI) $(USER_BOOTDATA)

# kernel bootdata for fuchsia build
$(BOARD_KERNEL_BOOTDATA): $(MKBOOTFS) $(BOARD_MDI)
	$(call BUILDECHO,generating $@)
	@$(MKDIR)
	$(NOECHO)$(MKBOOTFS) -o $@ $(BOARD_PLATFORM_ID) $(BOARD_MDI)

kernel-only: $(BOARD_KERNEL_BOOTDATA)
kernel: $(KERNEL_BOOTDATA)

GENERATED += $(BOARD_ZIRCON) $(BOARD_KERNEL_BOOTDATA)
EXTRA_BUILDDEPS += $(BOARD_ZIRCON) $(BOARD_KERNEL_BOOTDATA)

# clear variables passed in
PLATFORM_VID :=
PLATFORM_PID :=
PLATFORM_BOARD_NAME :=
PLATFORM_MDI_SRCS :=

# clear variables we set here
BOARD_MDI :=
BOARD_ZIRCON :=
BOARD_KERNEL_BOOTDATA :=
BOARD_PLATFORM_ID :=
