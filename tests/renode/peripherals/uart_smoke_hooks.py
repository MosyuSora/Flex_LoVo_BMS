def mc_setup_uart_tx_logger():
    bus = monitor.Machine.SystemBus

    def uart_tx_logger(cpu, _):
        data_address = int(cpu.GetRegister(1).RawValue)
        length = int(cpu.GetRegister(2).RawValue) & 0xFFFF
        data = [bus.ReadByte(data_address + offset) for offset in range(length)]
        text = "".join(chr(byte) if 32 <= byte <= 126 or byte in (10, 13) else "." for byte in data)
        payload = " ".join("%02X" % byte for byte in data)
        print('BMS_UART_TX {{"symbol":"HAL_UART_Transmit","length":%d,"data":"%s","text":%r}}' % (length, payload, text))
        cpu.InfoLog(
            'BMS_UART_TX {{"symbol":"HAL_UART_Transmit","length":{0},"data":"{1}","text":"{2}"}}',
            length,
            payload,
            text.replace("\r", "\\r").replace("\n", "\\n"),
        )
        cpu.SetRegisterUlong(0, 0)
        cpu.PC = cpu.LR

    for cpu in bus.GetCPUs():
        found, addresses = bus.TryGetAllSymbolAddresses("HAL_UART_Transmit", context=cpu)
        if not found:
            cpu.WarningLog("Symbol 'HAL_UART_Transmit' not found; UART TX logger was not installed")
            continue

        for address in addresses:
            for hook_address in (address & ~1, address | 1):
                cpu.AddHook(hook_address, uart_tx_logger)
                cpu.InfoLog("Installed UART TX logger at HAL_UART_Transmit = 0x{:X}".format(hook_address))
