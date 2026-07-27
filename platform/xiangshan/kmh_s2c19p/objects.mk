#
# SPDX-License-Identifier: BSD-2-Clause
#

platform-cppflags-y =
platform-cflags-y =
platform-asflags-y =
platform-ldflags-y =

platform-objs-y += platform.o

FW_DYNAMIC=y
FW_JUMP=y
FW_JUMP_OFFSET=0x400000
FW_JUMP_FDT_OFFSET=0x200000
FW_PAYLOAD=y
FW_PAYLOAD_OFFSET=0x400000
FW_PAYLOAD_FDT_OFFSET=$(FW_JUMP_FDT_OFFSET)
