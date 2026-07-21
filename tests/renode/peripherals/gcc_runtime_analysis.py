_gcc_runtime_analysis = None


class GccRuntimeAnalysis:
    STACK_TOP = 0x20002800
    SCAN_BOTTOM = 0x20001178
    FILL = 0xA5
    GUARD_BYTES = 64

    def __init__(self):
        self.bus = monitor.Machine.SystemBus
        self.min_sp = self.STACK_TOP
        self.min_sp_symbol = "reset"
        self.hook_hits = {}

    def install(self):
        for address in range(self.SCAN_BOTTOM, self.STACK_TOP):
            self.bus.WriteByte(address, self.FILL)

        symbols = [
            "main",
            "Get_Update_Data",
            "Update_val",
            "IIC1_read_one_byte",
            "IIC1_write_one_byte_CRC",
            "I2C1_Start",
            "I2C1_SendByte",
            "I2C1_ReceiveByte",
            "sprintf",
            "_malloc_r",
            "_sbrk",
            "USART1_IRQHandler",
            "TIM2_IRQHandler",
            "SysTick_Handler",
        ]
        for cpu in self.bus.GetCPUs():
            for symbol in symbols:
                found, addresses = self.bus.TryGetAllSymbolAddresses(symbol, context=cpu)
                if not found:
                    continue

                def hook(current_cpu, _, current_symbol=symbol):
                    sp = int(current_cpu.GetRegister(13).RawValue) & 0xFFFFFFFF
                    self.hook_hits[current_symbol] = self.hook_hits.get(current_symbol, 0) + 1
                    if sp < self.min_sp:
                        self.min_sp = sp
                        self.min_sp_symbol = current_symbol

                for address in addresses:
                    cpu.AddHook(address & ~1, hook)

    def dump_and_assert(self):
        first_changed = None
        for address in range(self.SCAN_BOTTOM, self.STACK_TOP):
            if self.bus.ReadByte(address) != self.FILL:
                first_changed = address
                break

        watermark_used = 0 if first_changed is None else self.STACK_TOP - first_changed
        hook_used = self.STACK_TOP - self.min_sp
        used = max(watermark_used, hook_used)
        free_above_floor = (self.STACK_TOP - self.SCAN_BOTTOM) - used
        guard_errors = sum(
            1
            for address in range(self.SCAN_BOTTOM, self.SCAN_BOTTOM + self.GUARD_BYTES)
            if self.bus.ReadByte(address) != self.FILL
        )
        required_hits = ["main", "Get_Update_Data", "Update_val", "IIC1_read_one_byte", "sprintf"]
        missing_hits = [symbol for symbol in required_hits if self.hook_hits.get(symbol, 0) == 0]

        if guard_errors != 0:
            raise Exception("GCC runtime stack guard was modified: {} bytes".format(guard_errors))
        if free_above_floor < 1024:
            raise Exception("GCC runtime stack margin is only {} bytes".format(free_above_floor))
        if missing_hits:
            raise Exception("GCC runtime stack test missed required paths: {}".format(", ".join(missing_hits)))

        print(
            'BMS_GCC_STACK {{"used_bytes":{0},"watermark_used_bytes":{1},'
            '"hook_used_bytes":{2},"free_above_floor_bytes":{3},'
            '"guard_errors":{4},"min_sp":"0x{5:08X}","min_sp_symbol":"{6}",'
            '"update_hits":{7},"iic_read_hits":{8},"sprintf_hits":{9}}}'.format(
                used,
                watermark_used,
                hook_used,
                free_above_floor,
                guard_errors,
                self.min_sp,
                self.min_sp_symbol,
                self.hook_hits.get("Update_val", 0),
                self.hook_hits.get("IIC1_read_one_byte", 0),
                self.hook_hits.get("sprintf", 0),
            )
        )


def mc_setup_gcc_runtime_analysis():
    global _gcc_runtime_analysis
    _gcc_runtime_analysis = GccRuntimeAnalysis()
    _gcc_runtime_analysis.install()


def mc_assert_gcc_runtime_analysis():
    if _gcc_runtime_analysis is None:
        raise Exception("GCC runtime analysis is not installed")
    _gcc_runtime_analysis.dump_and_assert()
