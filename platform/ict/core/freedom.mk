#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2020 Institute of Computing Technology, Chinese Academy of Sciences.
#
# Authors:
#   Yisong Chang <changyisong@ict.ac.cn>
#

platform-genflags-y += -DSERVE_CLINT_ADDR=0x2000000
platform-genflags-y += -DSERVE_PLIC_ADDR=0xc000000
platform-genflags-y += -DSERVE_PLIC_NUM_SOURCES=16

