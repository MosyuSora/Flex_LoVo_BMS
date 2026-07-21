from Antmicro.Renode.Peripherals.CPU import RegisterValue


def mc_setup_firmware_can_logger():
    bus = monitor.Machine.SystemBus

    def can_send_logger(cpu, _):
        msg = int(cpu.GetRegister(0).RawValue)
        length = int(cpu.GetRegister(1).RawValue) & 0xFF
        app_id = int(cpu.GetRegister(2).RawValue) & 0x1FFFFFFF
        data = [bus.ReadByte(msg + offset) for offset in range(min(length, 8))]
        payload = " ".join("%02X" % byte for byte in data)
        print('BMS_CAN_FRAME {{"id":"0x{0:04X}","dlc":{1},"data":"{2}"}}'.format(app_id, min(length, 8), payload))
        cpu.InfoLog(
            'BMS_CAN_FRAME {{"id":"0x{0:04X}","dlc":{1},"data":"{2}"}}',
            app_id,
            min(length, 8),
            payload,
        )
        cpu.SetRegisterUlong(0, 0)
        cpu.PC = cpu.LR

    for cpu in bus.GetCPUs():
        found, addresses = bus.TryGetAllSymbolAddresses("Can_Send_Msg", context=cpu)
        if not found:
            cpu.WarningLog("Symbol 'Can_Send_Msg' not found; virtual upper-computer CAN logger was not installed")
            continue

        for address in addresses:
            for hook_address in (address & ~1, address | 1):
                cpu.AddHook(hook_address, can_send_logger)
                cpu.InfoLog("Installed virtual upper-computer CAN logger at Can_Send_Msg = 0x{:X}".format(hook_address))


def mc_dump_firmware_can_buffers():
    bus = monitor.Machine.SystemBus
    frames = [
        ("can_buf1", 0x0001),
        ("can_buf2", 0x0002),
        ("can_buf3", 0x0003),
        ("can_buf4", 0x0004),
        ("can_buf5", 0x0005),
        ("can_buf6", 0x0006),
        ("can_buf7", 0x0007),
    ]

    for cpu in bus.GetCPUs():
        for symbol, app_id in frames:
            found, addresses = bus.TryGetAllSymbolAddresses(symbol, context=cpu)
            if not found:
                cpu.WarningLog("Symbol '{}' not found; virtual upper-computer CAN dump skipped it".format(symbol))
                continue
            address = list(addresses)[0] & ~1
            data = [bus.ReadByte(address + offset) for offset in range(8)]
            payload = " ".join("%02X" % byte for byte in data)
            print('BMS_CAN_FRAME {{"id":"0x{0:04X}","dlc":8,"data":"{1}"}}'.format(app_id, payload))
            cpu.InfoLog(
                'BMS_CAN_FRAME {{"id":"0x{0:04X}","dlc":8,"data":"{1}"}}',
                app_id,
                payload,
            )


def mc_patch_firmware_for_smoke():
    bus = monitor.Machine.SystemBus

    def patch_halfword(address, value):
        bus.WriteWord(address & ~1, value & 0xFFFF)

    def patch_return(symbol):
        for cpu in bus.GetCPUs():
            found, addresses = bus.TryGetAllSymbolAddresses(symbol, context=cpu)
            if not found:
                cpu.WarningLog("Symbol '{}' not found; smoke patch was not installed".format(symbol))
                continue
            for address in addresses:
                patch_halfword(address, 0x4770)
                cpu.InfoLog("Patched smoke helper {} to return at 0x{:X}".format(symbol, address & ~1))

    def patch_return_u8(symbol, value):
        for cpu in bus.GetCPUs():
            found, addresses = bus.TryGetAllSymbolAddresses(symbol, context=cpu)
            if not found:
                cpu.WarningLog("Symbol '{}' not found; smoke value patch was not installed".format(symbol))
                continue
            for address in addresses:
                base = address & ~1
                patch_halfword(base, 0x2000 | (value & 0xFF))
                patch_halfword(base + 2, 0x4770)
                cpu.InfoLog("Patched smoke helper {} to return {} at 0x{:X}".format(symbol, value, base))

    for symbol in [
        "SystemInit",
        "SYSTICK_Init",
        "NVIC_PriorityGroupConfig",
        "uart_init",
        "USART2_Config",
        "LED_GPIO_Config",
        "IO_CTRL_Config",
        "I2C1_Configuration",
        "CAN_Mode_Init",
        "TIM2_Config",
        "UartSend",
        "USART1_Printf",
        "USART2_Printf",
        "IWDG_Feed",
        "LED4_ONOFF",
        "LEDXToggle",
        "delay_ms",
        "IWDG_Init",
    ]:
        patch_return(symbol)

    patch_return_u8("Can_Receive_Msg", 0)


def mc_boot_firmware_from_vector_table(vector_base):
    bus = monitor.Machine.SystemBus
    initial_sp = bus.ReadDoubleWord(vector_base)
    reset_handler = bus.ReadDoubleWord(vector_base + 4)

    for cpu in bus.GetCPUs():
        cpu.SetRegisterUlong(13, initial_sp)
        cpu.PC = RegisterValue.Create(reset_handler, 32)
        cpu.InfoLog("Booting firmware from vector table: SP=0x{:X}, PC=0x{:X}".format(initial_sp, reset_handler))
