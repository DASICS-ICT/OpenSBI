#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2019 Institute of Computing Technology, Chinese Academy of Sciences.
#
# Authors:
#   Yisong Chang <changyisong@ict.ac.cn>
#

# Compiler flags
platform-cppflags-y =
platform-cflags-y = -DSMM_BASE=$(FW_TEXT_START) -DSMM_SIZE=$(FW_PAYLOAD_OFFSET)
platform-asflags-y =
platform-ldflags-y =

FW_PAYLOAD=y
FW_PAYLOAD_OFFSET=0x200000
FW_ONE_PAGE_TO_PAYLOAD=0x1FF000
FW_PAYLOAD_PATH=$(OPENSBI_PAYLOAD)

