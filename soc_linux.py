#!/usr/bin/env python3

#
# This file is part of Linux-on-LiteX-VexRiscv
#
# Copyright (c) 2019-2024, Linux-on-LiteX-VexRiscv Developers
# SPDX-License-Identifier: BSD-2-Clause

import os
import json
import shutil
import subprocess

from migen import *

from litex.soc.interconnect.csr import *
from litex.soc.integration.soc import SoCRegion
from litex.soc.interconnect import wishbone

from litex.soc.cores.cpu.vexriscv_smp import VexRiscvSMP
from litex.soc.cores.gpio    import GPIOOut, GPIOIn
from litex.soc.cores.spi     import SPIMaster
from litex.soc.cores.bitbang import I2CMaster
from litex.soc.cores.pwm     import PWM

from litex.tools.litex_json2dts_linux import generate_dts

# SoCLinux -----------------------------------------------------------------------------------------

def SoCLinux(soc_cls, **kwargs):
    class _SoCLinux(soc_cls):
        def __init__(self, **kwargs):
            # 检查是否启用异构系统
            self.with_heterogeneous = kwargs.pop("with_heterogeneous", False)
            
            # SoC ----------------------------------------------------------------------------------
            # 不要硬编码cpu_type和cpu_variant，让它们通过kwargs传递
            soc_cls.__init__(self, **kwargs)
            
            # 如果启用异构系统，添加小核和相关硬件
            if self.with_heterogeneous:
                self._add_heterogeneous_cores()

        # Heterogeneous System Support ------------------------------------------------------------
        def _add_heterogeneous_cores(self):
            """添加异构系统支持"""
            print("\n========== 添加异构系统支持 ==========")
            print("主系统: VexRiscv-SMP (4核) - Linux")
            print("小核0: VexRiscv简化版 - I/O处理")
            print("小核1: VexRiscv最小版 - 实时任务")
            
            # 1. 添加共享内存区域
            self._add_shared_memory()
            
            # 2. 添加核间中断系统
            self._add_inter_core_interrupts()
            
            # 3. 添加邮箱通信
            self._add_mailbox_system()
            
            # 4. 添加硬件互斥锁
            self._add_hardware_mutex()
            
            # 5. 添加小核
            self._add_small_cores()
            
            # 6. 添加常量定义
            self.add_constant("HETEROGENEOUS_ENABLED", 1)
            self.add_constant("NUM_SMALL_CORES", 2)
            self.add_constant("SHARED_MEM_BASE", 0x80100000)
            self.add_constant("SHARED_MEM_SIZE", 0x8000)
            
            print("\n✓ 异构系统支持已添加")

        def _add_shared_memory(self):
            """添加共享内存区域"""
            print("  添加共享内存 (32KB @ 0x80100000)...")
            
            # 在系统总线上预留共享内存区域
            self.bus.add_region(
                "shared_mem",
                SoCRegion(
                    origin = 0x80100000,
                    size   = 0x8000,  # 32KB
                    mode   = "rw",
                    cached = False     # 非缓存，保证一致性
                )
            )

        def _add_inter_core_interrupts(self):
            """添加核间中断机制"""
            print("  添加核间中断系统...")
            
            # 中断状态寄存器（只读）
            self.submodules.ipi_status = CSRStatus(32, name="ipi_status",
                description="Inter-processor interrupt status")
            
            # 中断触发寄存器（写1触发）
            self.submodules.ipi_trigger = CSRStorage(32, name="ipi_trigger",
                description="Inter-processor interrupt trigger")
            
            # 中断清除寄存器（写1清除）
            self.submodules.ipi_clear = CSRStorage(32, name="ipi_clear",
                description="Inter-processor interrupt clear")
            
            # 中断使能寄存器
            self.submodules.ipi_enable = CSRStorage(32, name="ipi_enable",
                description="Inter-processor interrupt enable")
            
            # 内部中断信号
            ipi_pending = Signal(32)
            
            # 中断逻辑
            self.sync += [
                # 触发中断
                If(self.ipi_trigger.re,
                    ipi_pending.eq(ipi_pending | self.ipi_trigger.storage)
                ),
                # 清除中断
                If(self.ipi_clear.re,
                    ipi_pending.eq(ipi_pending & ~self.ipi_clear.storage)
                )
            ]
            
            # 连接到状态寄存器
            self.comb += self.ipi_status.status.eq(ipi_pending & self.ipi_enable.storage)
            
            # 保存中断信号供小核使用
            self.ipi_pending = ipi_pending

        def _add_mailbox_system(self):
            """添加邮箱通信系统"""
            print("  添加邮箱系统...")
            
            # 为每个小核创建邮箱（双向）
            for core_id in range(2):
                # 主核到小核的邮箱
                setattr(self.submodules, f"mbox_main_to_core{core_id}_cmd",
                    CSRStorage(32, name=f"mbox_main_to_core{core_id}_cmd"))
                setattr(self.submodules, f"mbox_main_to_core{core_id}_data",
                    CSRStorage(32, name=f"mbox_main_to_core{core_id}_data"))
                setattr(self.submodules, f"mbox_main_to_core{core_id}_status",
                    CSRStatus(8, name=f"mbox_main_to_core{core_id}_status"))
                
                # 小核到主核的邮箱
                setattr(self.submodules, f"mbox_core{core_id}_to_main_resp",
                    CSRStatus(32, name=f"mbox_core{core_id}_to_main_resp"))
                setattr(self.submodules, f"mbox_core{core_id}_to_main_data",
                    CSRStatus(32, name=f"mbox_core{core_id}_to_main_data"))
                setattr(self.submodules, f"mbox_core{core_id}_to_main_ctrl",
                    CSRStorage(8, name=f"mbox_core{core_id}_to_main_ctrl"))

        def _add_hardware_mutex(self):
            """添加硬件互斥锁"""
            print("  添加硬件互斥锁...")
            
            # 16个硬件互斥锁
            NUM_MUTEXES = 16
            
            # 互斥锁请求寄存器
            self.submodules.hw_mutex_request = CSRStorage(NUM_MUTEXES, 
                name="hw_mutex_request",
                description="Hardware mutex request (write 1 to acquire)")
            
            # 互斥锁状态寄存器
            self.submodules.hw_mutex_status = CSRStatus(NUM_MUTEXES,
                name="hw_mutex_status", 
                description="Hardware mutex status (1=locked)")
            
            # 互斥锁释放寄存器
            self.submodules.hw_mutex_release = CSRStorage(NUM_MUTEXES,
                name="hw_mutex_release",
                description="Hardware mutex release (write 1 to release)")
            
            # 内部锁状态
            mutex_locked = Signal(NUM_MUTEXES)
            
            # 简化的互斥锁逻辑
            for i in range(NUM_MUTEXES):
                self.sync += [
                    # 请求锁
                    If(self.hw_mutex_request.storage[i] & self.hw_mutex_request.re,
                        If(~mutex_locked[i],
                            mutex_locked[i].eq(1)
                        )
                    ),
                    # 释放锁
                    If(self.hw_mutex_release.storage[i] & self.hw_mutex_release.re,
                        mutex_locked[i].eq(0)
                    )
                ]
            
            # 连接状态
            self.comb += self.hw_mutex_status.status.eq(mutex_locked)

        def _add_small_cores(self):
            """添加小核"""
            print("  添加小核...")
            
            # 小核0：I/O处理核
            self._add_io_core(0, base_addr=0x80200000)
            
            # 小核1：实时任务核  
            self._add_rt_core(1, base_addr=0x80300000)

        def _add_io_core(self, core_id, base_addr):
            """添加I/O处理小核 - 使用第二个版本的完整实现"""
            print(f"    添加小核{core_id} (I/O处理) @ 0x{base_addr:08x}")
            
            # 创建Wishbone接口信号
            ibus_adr = Signal(30, name=f"io_core_ibus_adr")
            ibus_dat_w = Signal(32, name=f"io_core_ibus_dat_w")
            ibus_dat_r = Signal(32, name=f"io_core_ibus_dat_r")
            ibus_sel = Signal(4, name=f"io_core_ibus_sel")
            ibus_cyc = Signal(name=f"io_core_ibus_cyc")
            ibus_stb = Signal(name=f"io_core_ibus_stb")
            ibus_ack = Signal(name=f"io_core_ibus_ack")
            ibus_we = Signal(name=f"io_core_ibus_we")
            ibus_cti = Signal(3, name=f"io_core_ibus_cti")
            ibus_bte = Signal(2, name=f"io_core_ibus_bte")
            ibus_err = Signal(name=f"io_core_ibus_err")

            dbus_adr = Signal(30, name=f"io_core_dbus_adr")
            dbus_dat_w = Signal(32, name=f"io_core_dbus_dat_w")
            dbus_dat_r = Signal(32, name=f"io_core_dbus_dat_r")
            dbus_sel = Signal(4, name=f"io_core_dbus_sel")
            dbus_cyc = Signal(name=f"io_core_dbus_cyc")
            dbus_stb = Signal(name=f"io_core_dbus_stb")
            dbus_ack = Signal(name=f"io_core_dbus_ack")
            dbus_we = Signal(name=f"io_core_dbus_we")
            dbus_cti = Signal(3, name=f"io_core_dbus_cti")
            dbus_bte = Signal(2, name=f"io_core_dbus_bte")
            dbus_err = Signal(name=f"io_core_dbus_err")

            # 实例化VexRiscv_IOCore
            self.specials += Instance("VexRiscv_IOCore",
                name=f"vexriscv_io_core_{core_id}",
                
                # 时钟和复位
                i_clk = ClockSignal("sys"),
                i_reset = ResetSignal("sys"),
                
                # 配置 - 使用第一个版本的中断信号
                i_externalResetVector = base_addr,
                i_timerInterrupt = 0,
                i_softwareInterrupt = 0,
                i_externalInterruptArray = Cat(self.ipi_pending[core_id], Signal(31)),
                
                # 指令总线
                o_iBusWishbone_CYC = ibus_cyc,
                o_iBusWishbone_STB = ibus_stb,
                i_iBusWishbone_ACK = ibus_ack,
                o_iBusWishbone_ADR = ibus_adr,
                i_iBusWishbone_DAT_MISO = ibus_dat_r,
                o_iBusWishbone_DAT_MOSI = ibus_dat_w,
                o_iBusWishbone_SEL = ibus_sel,
                i_iBusWishbone_ERR = ibus_err,
                o_iBusWishbone_CTI = ibus_cti,
                o_iBusWishbone_BTE = ibus_bte,
                o_iBusWishbone_WE = ibus_we,
                
                # 数据总线
                o_dBusWishbone_CYC = dbus_cyc,
                o_dBusWishbone_STB = dbus_stb,
                i_dBusWishbone_ACK = dbus_ack,
                o_dBusWishbone_ADR = dbus_adr,
                i_dBusWishbone_DAT_MISO = dbus_dat_r,
                o_dBusWishbone_DAT_MOSI = dbus_dat_w,
                o_dBusWishbone_SEL = dbus_sel,
                i_dBusWishbone_ERR = dbus_err,
                o_dBusWishbone_CTI = dbus_cti,
                o_dBusWishbone_BTE = dbus_bte,
                o_dBusWishbone_WE = dbus_we,
            )

            # 创建并连接Wishbone接口
            self._connect_wishbone(core_id, ibus_adr, ibus_dat_w, ibus_dat_r, ibus_sel,
                                 ibus_cyc, ibus_stb, ibus_ack, ibus_we, ibus_cti, 
                                 ibus_bte, ibus_err, dbus_adr, dbus_dat_w, dbus_dat_r,
                                 dbus_sel, dbus_cyc, dbus_stb, dbus_ack, dbus_we,
                                 dbus_cti, dbus_bte, dbus_err, base_addr)

            # 添加源文件
            verilog_paths = [
                f"/home/eda/Desktop/6_core/litex-custom-uart/VexRiscv_IOCore.v",
                f"/home/eda/FUWEI/litex-custom-uart/VexRiscv_IOCore.v",
            ]
            
            for path in verilog_paths:
                if os.path.exists(path):
                    self.platform.add_source(path)
                    print(f"  ✓ 已添加Verilog源文件: {path}")
                    break
            
            print(f"  ✓ I/O处理核已添加 (基址: 0x{base_addr:08x})")

        def _add_rt_core(self, core_id, base_addr):
            """添加实时任务小核"""
            print(f"    添加小核{core_id} (实时任务) @ 0x{base_addr:08x}")
            
            # 创建Wishbone接口信号
            ibus_adr = Signal(30, name=f"rt_core_ibus_adr")
            ibus_dat_w = Signal(32, name=f"rt_core_ibus_dat_w")
            ibus_dat_r = Signal(32, name=f"rt_core_ibus_dat_r")
            ibus_sel = Signal(4, name=f"rt_core_ibus_sel")
            ibus_cyc = Signal(name=f"rt_core_ibus_cyc")
            ibus_stb = Signal(name=f"rt_core_ibus_stb")
            ibus_ack = Signal(name=f"rt_core_ibus_ack")
            ibus_we = Signal(name=f"rt_core_ibus_we")
            ibus_cti = Signal(3, name=f"rt_core_ibus_cti")
            ibus_bte = Signal(2, name=f"rt_core_ibus_bte")
            ibus_err = Signal(name=f"rt_core_ibus_err")

            dbus_adr = Signal(30, name=f"rt_core_dbus_adr")
            dbus_dat_w = Signal(32, name=f"rt_core_dbus_dat_w")
            dbus_dat_r = Signal(32, name=f"rt_core_dbus_dat_r")
            dbus_sel = Signal(4, name=f"rt_core_dbus_sel")
            dbus_cyc = Signal(name=f"rt_core_dbus_cyc")
            dbus_stb = Signal(name=f"rt_core_dbus_stb")
            dbus_ack = Signal(name=f"rt_core_dbus_ack")
            dbus_we = Signal(name=f"rt_core_dbus_we")
            dbus_cti = Signal(3, name=f"rt_core_dbus_cti")
            dbus_bte = Signal(2, name=f"rt_core_dbus_bte")
            dbus_err = Signal(name=f"rt_core_dbus_err")

            # 实例化VexRiscv_RTCore
            self.specials += Instance("VexRiscv_RTCore",
                name=f"vexriscv_rt_core_{core_id}",
                
                # 时钟和复位
                i_clk = ClockSignal("sys"),
                i_reset = ResetSignal("sys"),
                
                # 配置
                i_externalResetVector = base_addr,
                i_timerInterrupt = 0,
                i_softwareInterrupt = 0,
                i_externalInterruptArray = Cat(self.ipi_pending[core_id], Signal(31)),
                
                # 指令总线
                o_iBusWishbone_CYC = ibus_cyc,
                o_iBusWishbone_STB = ibus_stb,
                i_iBusWishbone_ACK = ibus_ack,
                o_iBusWishbone_ADR = ibus_adr,
                i_iBusWishbone_DAT_MISO = ibus_dat_r,
                o_iBusWishbone_DAT_MOSI = ibus_dat_w,
                o_iBusWishbone_SEL = ibus_sel,
                i_iBusWishbone_ERR = ibus_err,
                o_iBusWishbone_CTI = ibus_cti,
                o_iBusWishbone_BTE = ibus_bte,
                o_iBusWishbone_WE = ibus_we,
                
                # 数据总线
                o_dBusWishbone_CYC = dbus_cyc,
                o_dBusWishbone_STB = dbus_stb,
                i_dBusWishbone_ACK = dbus_ack,
                o_dBusWishbone_ADR = dbus_adr,
                i_dBusWishbone_DAT_MISO = dbus_dat_r,
                o_dBusWishbone_DAT_MOSI = dbus_dat_w,
                o_dBusWishbone_SEL = dbus_sel,
                i_dBusWishbone_ERR = dbus_err,
                o_dBusWishbone_CTI = dbus_cti,
                o_dBusWishbone_BTE = dbus_bte,
                o_dBusWishbone_WE = dbus_we,
            )

            # 创建并连接Wishbone接口
            self._connect_wishbone(core_id, ibus_adr, ibus_dat_w, ibus_dat_r, ibus_sel,
                                 ibus_cyc, ibus_stb, ibus_ack, ibus_we, ibus_cti, 
                                 ibus_bte, ibus_err, dbus_adr, dbus_dat_w, dbus_dat_r,
                                 dbus_sel, dbus_cyc, dbus_stb, dbus_ack, dbus_we,
                                 dbus_cti, dbus_bte, dbus_err, base_addr)

            # 添加源文件
            verilog_paths = [
                f"/home/eda/Desktop/6_core/litex-custom-uart/VexRiscv_RTCore.v",
                f"/home/eda/FUWEI/litex-custom-uart/VexRiscv_RTCore.v",
            ]
            
            for path in verilog_paths:
                if os.path.exists(path):
                    self.platform.add_source(path)
                    print(f"  ✓ 已添加Verilog源文件: {path}")
                    break
            
            print(f"  ✓ 实时任务核已添加 (基址: 0x{base_addr:08x})")

        def _connect_wishbone(self, core_id, ibus_adr, ibus_dat_w, ibus_dat_r, 
                              ibus_sel, ibus_cyc, ibus_stb, ibus_ack, ibus_we,
                              ibus_cti, ibus_bte, ibus_err, dbus_adr, dbus_dat_w,
                              dbus_dat_r, dbus_sel, dbus_cyc, dbus_stb, dbus_ack,
                              dbus_we, dbus_cti, dbus_bte, dbus_err, base_addr):
            """连接Wishbone总线"""
            # 指令总线
            ibus = wishbone.Interface(data_width=32, adr_width=30)
            self.comb += [
                ibus.adr.eq(ibus_adr),
                ibus.dat_w.eq(ibus_dat_w),
                ibus_dat_r.eq(ibus.dat_r),
                ibus.sel.eq(ibus_sel),
                ibus.cyc.eq(ibus_cyc),
                ibus.stb.eq(ibus_stb),
                ibus_ack.eq(ibus.ack),
                ibus.we.eq(ibus_we),
                ibus.cti.eq(ibus_cti),
                ibus.bte.eq(ibus_bte),
                ibus_err.eq(ibus.err),
            ]

            # 数据总线
            dbus = wishbone.Interface(data_width=32, adr_width=30)
            self.comb += [
                dbus.adr.eq(dbus_adr),
                dbus.dat_w.eq(dbus_dat_w),
                dbus_dat_r.eq(dbus.dat_r),
                dbus.sel.eq(dbus_sel),
                dbus.cyc.eq(dbus_cyc),
                dbus.stb.eq(dbus_stb),
                dbus_ack.eq(dbus.ack),
                dbus.we.eq(dbus_we),
                dbus.cti.eq(dbus_cti),
                dbus.bte.eq(dbus_bte),
                dbus_err.eq(dbus.err),
            ]

            # 添加到系统总线
            self.bus.add_master(name=f"small_core_{core_id}_ibus", master=ibus)
            self.bus.add_master(name=f"small_core_{core_id}_dbus", master=dbus)

            # 分配内存区域
            self.bus.add_region(
                f"small_core_{core_id}_mem",
                SoCRegion(
                    origin=base_addr,
                    size=0x00100000,  # 1MB
                    cached=False,
                    mode="rw"
                )
            )

        # RGB Led ----------------------------------------------------------------------------------
        def add_rgb_led(self):
            rgb_led_pads = self.platform.request("rgb_led", 0)
            for n in "rgb":
                self.add_module(name=f"rgb_led_{n}0", module=PWM(getattr(rgb_led_pads, n)))

        # Switches ---------------------------------------------------------------------------------
        def add_switches(self):
            self.switches = GPIOIn(Cat(self.platform.request_all("user_sw")), with_irq=True)
            self.irq.add("switches")

        # SPI --------------------------------------------------------------------------------------
        def add_spi(self, data_width, clk_freq):
            spi_pads = self.platform.request("spi")
            self.spi = SPIMaster(spi_pads, data_width, self.clk_freq, clk_freq)

        # I2C --------------------------------------------------------------------------------------
        def add_i2c(self):
            self.i2c0 = I2CMaster(self.platform.request("i2c", 0))

        # Ethernet configuration -------------------------------------------------------------------
        def configure_ethernet(self, remote_ip):
            remote_ip = remote_ip.split(".")
            try:
                self.constants.pop("REMOTEIP1")
                self.constants.pop("REMOTEIP2")
                self.constants.pop("REMOTEIP3")
                self.constants.pop("REMOTEIP4")
            except:
                pass
            self.add_constant("REMOTEIP1", int(remote_ip[0]))
            self.add_constant("REMOTEIP2", int(remote_ip[1]))
            self.add_constant("REMOTEIP3", int(remote_ip[2]))
            self.add_constant("REMOTEIP4", int(remote_ip[3]))

        # DTS generation ---------------------------------------------------------------------------
        def generate_dts(self, board_name):
            json_src = os.path.join("build", board_name, "csr.json")
            dts = os.path.join("build", board_name, "{}.dts".format(board_name))

            with open(json_src) as json_file, open(dts, "w") as dts_file:
                dts_content = generate_dts(json.load(json_file), polling=False)
                dts_file.write(dts_content)

        # DTS compilation --------------------------------------------------------------------------
        def compile_dts(self, board_name, symbols=False):
            dts = os.path.join("build", board_name, "{}.dts".format(board_name))
            dtb = os.path.join("build", board_name, "{}.dtb".format(board_name))
            subprocess.check_call(
                "dtc {} -O dtb -o {} {}".format("-@" if symbols else "", dtb, dts), shell=True)

        # DTB combination --------------------------------------------------------------------------
        def combine_dtb(self, board_name, overlays=""):
            dtb_in = os.path.join("build", board_name, "{}.dtb".format(board_name))
            dtb_out = os.path.join("images", "rv32.dtb")
            if overlays == "":
                shutil.copyfile(dtb_in, dtb_out)
            else:
                subprocess.check_call(
                    "fdtoverlay -i {} -o {} {}".format(dtb_in, dtb_out, overlays), shell=True)

        # Documentation generation -----------------------------------------------------------------
        def generate_doc(self, board_name):
            from litex.soc.doc import generate_docs
            doc_dir = os.path.join("build", board_name, "doc")
            generate_docs(self, doc_dir)
            os.system("sphinx-build -M html {}/ {}/_build".format(doc_dir, doc_dir))

    return _SoCLinux(**kwargs)
