#
# Scripted BQ76940-like I2C slave for Renode.
#
# This file is loaded by Renode's monitor with:
#   include @tests/renode/peripherals/bq76940_i2c.py
#
# It targets Renode's Mocks.DummyI2CSlave. The real shipping firmware writes a
# register address, then issues a repeated-start read. DummyI2CSlave exposes the
# write payload via DataReceived, so this model queues response bytes whenever a
# register pointer write is observed.
#

import json
import math

from Antmicro.Renode.Peripherals.Bus import Access, SysbusAccessWidth


BQ_I2C_WRITE_ADDRESS = 0x10
REGISTER_COUNT = 0x60
ADC_LSB_UV = 382
CURRENT_RAW_TO_MA = 2.11

REG_SYS_STAT = 0x00
REG_SYS_CTRL2 = 0x05
REG_VC1_HI = 0x0C
REG_BAT_HI = 0x2A
REG_TS1_HI = 0x2C
REG_TS2_HI = 0x2E
REG_CC_HI = 0x32
REG_ADCGAIN1 = 0x50
REG_ADCOFFSET = 0x51
REG_ADCGAIN2 = 0x59


def _clamp(value, lower, upper):
    return max(lower, min(upper, value))


def _hi(value):
    return (int(value) >> 8) & 0xFF


def _lo(value):
    return int(value) & 0xFF


def _crc8(data, polynomial):
    crc = 0
    for byte in data:
        crc ^= int(byte) & 0xFF
        for _ in range(8):
            if (crc & 0x80) != 0:
                crc = ((crc << 1) ^ polynomial) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc & 0xFF


def _cell_mv_to_raw(cell_mv):
    return _clamp(int(round((int(cell_mv) * 1000.0) / ADC_LSB_UV)), 0, 0x3FFF)


def _ntc_raw(temp_c, ntc):
    r25 = float(ntc.get("r25_ohm", 10000))
    beta = float(ntc.get("beta", 3380))
    pullup = float(ntc.get("pullup_ohm", 10000))
    temp_k = float(temp_c) + 273.15
    ref_k = 25.0 + 273.15
    resistance = r25 * math.exp(beta * ((1.0 / temp_k) - (1.0 / ref_k)))
    voltage_mv = 3300.0 * resistance / (pullup + resistance)
    return _clamp(int(round((voltage_mv * 1000.0) / ADC_LSB_UV)), 0, 0x3FFF)


def _current_ma_to_raw(current_ma):
    # scenario current_ma follows the firmware convention (discharge positive).
    # The real board's SRP/SRN wiring makes the BQ CC *charge-positive*, and the
    # driver negates CC to get back to discharge-positive. To stay faithful to
    # silicon (and round-trip with that negation), encode CC charge-positive here:
    # discharge (current_ma > 0) -> negative CC, charge (current_ma < 0) -> positive CC.
    raw = int(round(abs(float(current_ma)) / CURRENT_RAW_TO_MA))
    raw = _clamp(raw, 0, 0x7D00)
    if current_ma > 0:
        raw = (0x10000 - raw) & 0xFFFF
    return raw


def _active_cell_values(scenario, cells):
    channels = scenario.get("active_cell_channels")
    if channels is None:
        return list(cells)

    values = []
    for channel in channels:
        index = int(channel) - 1
        if 0 <= index < len(cells):
            values.append(cells[index])
    return values


class BQ76940I2CPeripheral:
    def __init__(self, dummy, scenario_path):
        self.dummy = dummy
        self.scenario_path = scenario_path
        self.pointer = 0
        self.hook_read_count = 0
        self.hook_write_count = 0
        self.registers = [0] * REGISTER_COUNT
        self.load_scenario(scenario_path)

    def load_scenario(self, scenario_path):
        with open(scenario_path, "r") as stream:
            scenario = json.load(stream)

        cells = scenario.get("cell_mv", [])
        active_cells = _active_cell_values(scenario, cells)
        ntc = scenario.get("ntc", {})
        temperatures = scenario.get("temperatures_c", [25.0])
        ntc_faults = scenario.get("ntc_faults", [])
        current_ma = int(scenario.get("current_ma", 0))

        self.registers = [0] * REGISTER_COUNT

        # Make Get_offset() in the shipping firmware compute GAIN = 382 uV.
        self.registers[REG_ADCGAIN1] = 0x08
        self.registers[REG_ADCGAIN2] = 0x20
        self.registers[REG_ADCOFFSET] = 0x00

        for index, cell_mv in enumerate(cells[:15]):
            raw = _cell_mv_to_raw(cell_mv)
            reg = REG_VC1_HI + index * 2
            self.registers[reg] = _hi(raw)
            self.registers[reg + 1] = _lo(raw)

        pack_raw = _clamp(int(round(sum(active_cells) / 4.0)), 0, 0xFFFF)
        self.registers[REG_BAT_HI] = _hi(pack_raw)
        self.registers[REG_BAT_HI + 1] = _lo(pack_raw)

        for index, temp_c in enumerate(temperatures[:2]):
            fault = ntc_faults[index] if index < len(ntc_faults) else ""
            if fault == "open":
                raw = 0x3FFF
            elif fault == "short":
                raw = 0
            else:
                raw = _ntc_raw(temp_c, ntc)
            reg = REG_TS1_HI + index * 2
            self.registers[reg] = _hi(raw)
            self.registers[reg + 1] = _lo(raw)

        current_raw = _current_ma_to_raw(current_ma)
        self.registers[REG_CC_HI] = _hi(current_raw)
        self.registers[REG_CC_HI + 1] = _lo(current_raw)

        thresholds = scenario.get("protection_thresholds", {})
        ov = int(thresholds.get("cell_ov_mv", 4200))
        uv = int(thresholds.get("cell_uv_mv", 2800))
        if active_cells and max(active_cells) >= ov:
            self.registers[REG_SYS_STAT] |= 1 << 2
        if active_cells and min(active_cells) <= uv:
            self.registers[REG_SYS_STAT] |= 1 << 3

        self.dummy.Log(LogLevel.Info, "BQ76940 scenario loaded: {0}", scenario.get("name", scenario_path))

    def write(self, data):
        data = [int(x) & 0xFF for x in data]
        if len(data) == 0:
            self.dummy.Log(LogLevel.Warning, "BQ76940 received empty I2C write")
            return

        register = data[0]
        self.pointer = register

        if len(data) == 1:
            self._queue_read_window(register)
            return

        value = data[1]
        if len(data) >= 3:
            expected = _crc8([BQ_I2C_WRITE_ADDRESS, register, value], 0x07)
            actual = data[2]
            if actual != expected:
                self.dummy.Log(
                    LogLevel.Warning,
                    "BQ76940 write CRC mismatch at 0x{0:X2}: got 0x{1:X2}, expected 0x{2:X2}",
                    register,
                    actual,
                    expected,
                )
                return

        self._write_register(register, value)

    def _write_register(self, register, value):
        if register >= len(self.registers):
            self.dummy.Log(LogLevel.Warning, "BQ76940 write out of range: 0x{0:X2}", register)
            return

        if register == REG_SYS_STAT:
            # BQ769x0 status bits are cleared by writing 1s.
            self.registers[register] = self.registers[register] & (~value & 0xFF)
        else:
            self.registers[register] = value & 0xFF

        self.dummy.Log(LogLevel.Debug, "BQ76940 write 0x{0:X2}=0x{1:X2}", register, value)

    def read_register(self, register):
        if register >= len(self.registers):
            self.dummy.Log(LogLevel.Warning, "BQ76940 read out of range: 0x{0:X2}", register)
            return 0

        value = self.registers[register]
        self.hook_read_count += 1
        if self.hook_read_count <= 16:
            self.dummy.Log(LogLevel.Info, "BQ76940 firmware read 0x{0:X2}->0x{1:X2}", register, value)
        self.dummy.Log(LogLevel.Debug, "BQ76940 read 0x{0:X2}->0x{1:X2}", register, value)
        return value

    def write_register_from_firmware(self, register, value):
        self.hook_write_count += 1
        if self.hook_write_count <= 16:
            self.dummy.Log(LogLevel.Info, "BQ76940 firmware write 0x{0:X2}=0x{1:X2}", register, value)
        self._write_register(register, value)

    def _queue_read_window(self, register):
        for offset in range(1):
            address = register + offset
            self.dummy.EnqueueResponseByte(int(self.registers[address] if address < len(self.registers) else 0))
        self.dummy.Log(LogLevel.Debug, "BQ76940 queued read from 0x{0:X2}", register)


_bq76940_instances = {}
_bq76940_gpio_i2c_instances = {}


def _monitor_add_hook(address, script):
    escaped = script.replace("\\", "\\\\").replace('"', '\\"')
    monitor.Parse('cpu AddHook 0x{:X} "{}"'.format(address, escaped))


def mc_setup_bq76940_i2c(path, scenario_path):
    dummy = monitor.Machine[path]
    peripheral = BQ76940I2CPeripheral(dummy, scenario_path)
    dummy.DataReceived += peripheral.write
    _bq76940_instances[path] = peripheral


def mc_bq76940_i2c_self_test(path):
    peripheral = _bq76940_instances[path]

    vc1 = (peripheral.registers[REG_VC1_HI] << 8) | peripheral.registers[REG_VC1_HI + 1]
    ts1 = (peripheral.registers[REG_TS1_HI] << 8) | peripheral.registers[REG_TS1_HI + 1]
    gain = 365 + ((peripheral.registers[REG_ADCGAIN1] & 0x0C) << 1) + ((peripheral.registers[REG_ADCGAIN2] & 0xE0) >> 5)

    if vc1 == 0:
        raise Exception("BQ76940 self-test failed: VC1 raw is zero")
    # TS1 may intentionally be zero/near-full-scale in NTC open/short fault
    # scenarios. Communication self-test is therefore based on VC1 and gain;
    # temperature validity is asserted by the firmware snapshot checks.
    if gain != ADC_LSB_UV:
        raise Exception("BQ76940 self-test failed: firmware gain would be {0}, expected {1}".format(gain, ADC_LSB_UV))

    peripheral.dummy.Log(LogLevel.Info, "BQ76940 self-test passed: VC1=0x{0:X4}, TS1=0x{1:X4}, GAIN={2}", vc1, ts1, gain)


def _setup_bq76940_shipping_iic_hooks_legacy(path):
    peripheral = _bq76940_instances[path]
    bus = monitor.Machine.SystemBus

    def add_hook(symbol, function):
        for cpu in bus.GetCPUs():
            found, addresses = bus.TryGetAllSymbolAddresses(symbol, context=cpu)
            if not found:
                cpu.WarningLog("Symbol '{}' not found; BQ76940 firmware IIC hook was not installed".format(symbol))
                continue

            for address in addresses:
                for hook_address in (address, address | 1):
                    cpu.AddHook(hook_address, function)
                    cpu.InfoLog("Installed BQ76940 firmware IIC hook at {} = 0x{:X}".format(symbol, hook_address))

    def read_one_byte(cpu, _):
        register = int(cpu.GetRegister(0).RawValue) & 0xFFFF
        value = peripheral.read_register(register)
        cpu.SetRegisterUlong(0, value)
        cpu.PC = cpu.LR

    def write_one_byte(cpu, _):
        register = int(cpu.GetRegister(0).RawValue) & 0xFFFF
        value = int(cpu.GetRegister(1).RawValue) & 0xFF
        peripheral.write_register_from_firmware(register, value)
        cpu.PC = cpu.LR

    add_hook("IIC1_read_one_byte", read_one_byte)
    add_hook("IIC1_write_one_byte", write_one_byte)
    add_hook("IIC1_write_one_byte_CRC", write_one_byte)


def shipping_bq_iic_read_hook(cpu, path):
    peripheral = _bq76940_instances[path]
    register = int(cpu.GetRegister(0).RawValue) & 0xFFFF
    value = peripheral.read_register(register)
    cpu.SetRegisterUlong(0, value)
    cpu.PC = cpu.LR


def shipping_bq_iic_write_hook(cpu, path):
    peripheral = _bq76940_instances[path]
    register = int(cpu.GetRegister(0).RawValue) & 0xFFFF
    value = int(cpu.GetRegister(1).RawValue) & 0xFF
    peripheral.write_register_from_firmware(register, value)
    cpu.PC = cpu.LR


def _setup_bq76940_shipping_iic_string_hooks_legacy(path):
    bus = monitor.Machine.SystemBus
    initial_registers = {index: value for index, value in enumerate(_bq76940_instances[path].registers) if value != 0}
    initial_registers_repr = repr(initial_registers)

    def add_hook(symbol, hook_name):
        for cpu in bus.GetCPUs():
            found, addresses = bus.TryGetAllSymbolAddresses(symbol, context=cpu)
            if not found:
                cpu.WarningLog("Symbol '{}' not found; BQ76940 firmware IIC hook was not installed".format(symbol))
                continue

            if hook_name == "read":
                script = (
                    "g=globals(); "
                    "g.setdefault('bq_regs', " + initial_registers_repr + "); "
                    "r=int(self.GetRegister(0).RawValue)&0xFFFF; "
                    "v=g['bq_regs'].get(r,0)&0xFF; "
                    "self.InfoLog('BQ76940 firmware read 0x{:02X}->0x{:02X}'.format(r,v)); "
                    "self.SetRegisterUlong(0,v); self.PC=self.LR"
                )
            else:
                script = (
                    "g=globals(); "
                    "g.setdefault('bq_regs', " + initial_registers_repr + "); "
                    "r=int(self.GetRegister(0).RawValue)&0xFFFF; "
                    "v=int(self.GetRegister(1).RawValue)&0xFF; "
                    "g['bq_regs'][r]=(g['bq_regs'].get(r,0)&(~v&0xFF)) if r==0 else v; "
                    "self.InfoLog('BQ76940 firmware write 0x{:02X}=0x{:02X}'.format(r,v)); "
                    "self.PC=self.LR"
                )
            for address in addresses:
                for hook_address in (address, address | 1):
                    _monitor_add_hook(hook_address, script)
                    cpu.InfoLog("Installed BQ76940 firmware IIC string hook at {} = 0x{:X}".format(symbol, hook_address))

    add_hook("IIC1_read_one_byte", "read")
    add_hook("IIC1_write_one_byte", "write")
    add_hook("IIC1_write_one_byte_CRC", "write")


def mc_setup_bq76940_firmware_iic_hooks(path):
    raise Exception(
        "Function-level shipping IIC hooks are retired; use setup_bq76940_gpio_i2c "
        "so firmware executes the PB8/PB9 software-IIC state machine"
    )


class BQ76940GpioI2CAdapter:
    GPIOB_ODR = 0x40010C0C
    GPIOB_BSRR = 0x40010C10
    GPIOB_BRR = 0x40010C14
    SCL_MASK = 1 << 8
    SDA_MASK = 1 << 9

    def __init__(self, peripheral, gpio_port):
        self.peripheral = peripheral
        self.gpio_port = gpio_port
        self.master_scl = True
        self.master_sda = True
        self.slave_sda_low = False
        self.state = "idle"
        self.expecting = "address"
        self.current_byte = 0
        self.bit_count = 0
        self.pointer = 0
        self.pending_register = 0
        self.pending_value = 0
        self.tx_byte = 0
        self.tx_bit = 0
        self.tx_is_crc = False
        self.tx_is_first_data = True
        self.ack_master_sda = True
        self.ack_clock_seen = False
        self.pending_high_sample = False
        self.odr_shadow = self.SCL_MASK | self.SDA_MASK
        self.rx_log_count = 0
        self.rx_trace = []
        self.start_count = 0
        self.stop_count = 0
        self.tx_byte_count = 0
        self.crc_error_count = 0
        self.address_nack_count = 0
        self.api_write_count = 0
        self.register_write_count = 0

    def on_gpio_write(self, address, value):
        self.register_write_count += 1
        value = int(value) & 0xFFFFFFFF
        old_scl = self.master_scl
        old_sda = self._effective_sda()

        if address == self.GPIOB_BSRR:
            if (value & self.SCL_MASK) != 0:
                self.master_scl = True
            if (value & self.SDA_MASK) != 0:
                self.master_sda = True
            if (value & (self.SCL_MASK << 16)) != 0:
                self.master_scl = False
            if (value & (self.SDA_MASK << 16)) != 0:
                self.master_sda = False
        elif address == self.GPIOB_BRR:
            if (value & self.SCL_MASK) != 0:
                self.master_scl = False
            if (value & self.SDA_MASK) != 0:
                self.master_sda = False
        elif address == self.GPIOB_ODR:
            self.odr_shadow = value & 0xFFFF
            self.master_scl = (self.odr_shadow & self.SCL_MASK) != 0
            self.master_sda = (self.odr_shadow & self.SDA_MASK) != 0

        self._handle_line_change(old_scl, old_sda)

    def on_gpio_api_write(self, pin_mask, high):
        self.api_write_count += 1
        old_scl = self.master_scl
        old_sda = self._effective_sda()
        pin_mask = int(pin_mask) & 0xFFFF
        if (pin_mask & self.SCL_MASK) != 0:
            self.master_scl = bool(high)
        if (pin_mask & self.SDA_MASK) != 0:
            self.master_sda = bool(high)
        self._handle_line_change(old_scl, old_sda)

    def gpio_api_read(self, pin_mask):
        pin_mask = int(pin_mask) & 0xFFFF
        if (pin_mask & self.SCL_MASK) != 0:
            return 1 if self.master_scl else 0
        if (pin_mask & self.SDA_MASK) != 0:
            return 1 if self._effective_sda() else 0
        return 0

    def _effective_sda(self):
        return self.master_sda and not self.slave_sda_low

    def _drive_slave_sda(self, low):
        self.slave_sda_low = bool(low)
        if self.gpio_port is not None:
            # STM32 software IIC releases SDA by writing 1 to an open-drain pin;
            # the virtual BQ pulls the input low only for ACK and read data bits.
            self.gpio_port.OnGPIO(9, not self.slave_sda_low)

    def _handle_line_change(self, old_scl, old_sda):
        new_sda = self._effective_sda()
        if old_sda and not new_sda and self.master_scl:
            self._start()
            return

        if not old_sda and new_sda and self.master_scl:
            self._stop()
            return

        if not old_scl and self.master_scl:
            self._scl_rising()
        elif old_scl and not self.master_scl:
            self._scl_falling()

    def _start(self):
        self.start_count += 1
        self.state = "receive"
        self.expecting = "address"
        self.current_byte = 0
        self.bit_count = 0
        self.pending_high_sample = False
        self._drive_slave_sda(False)
        self.peripheral.dummy.Log(LogLevel.Debug, "BQ76940 GPIO IIC START")

    def _stop(self):
        self.stop_count += 1
        self.peripheral.dummy.Log(LogLevel.Debug, "BQ76940 GPIO IIC STOP")
        self.state = "idle"
        self.current_byte = 0
        self.bit_count = 0
        self.pending_high_sample = False
        self._drive_slave_sda(False)

    def _scl_rising(self):
        if self.state == "receive":
            if self.bit_count == 0 and self._effective_sda():
                self.pending_high_sample = True
                return
            self._sample_receive_bit(1 if self._effective_sda() else 0)
        elif self.state == "ack":
            self.ack_clock_seen = True
        elif self.state == "read_ack":
            self.ack_master_sda = self._effective_sda()

    def _scl_falling(self):
        if self.pending_high_sample:
            self.pending_high_sample = False
            self._sample_receive_bit(1)

        if self.state == "ack":
            if not self.ack_clock_seen:
                return
            self.ack_clock_seen = False
            self._drive_slave_sda(False)
            if self.expecting == "transmit":
                self._load_tx_byte()
                self.tx_is_crc = False
                self.tx_is_first_data = True
                self.state = "transmit"
                self.tx_bit = 0
                self._drive_tx_bit()
            else:
                self.state = "receive"
        elif self.state == "transmit":
            self.tx_bit += 1
            if self.tx_bit >= 8:
                self._drive_slave_sda(False)
                self.state = "read_ack"
            else:
                self._drive_tx_bit()
        elif self.state == "read_ack":
            if self.ack_master_sda:
                self.state = "idle"
            elif not self.tx_is_crc:
                # Real chip (CRC enabled) appends a CRC byte after every data byte in a
                # sequential read; single-byte reads dodge it by NACKing early. Master
                # ACKed (wants more), so emit the CRC of the data byte just sent.
                if self.tx_is_first_data:
                    self.tx_byte = _crc8([BQ_I2C_WRITE_ADDRESS | 1, self.tx_byte], 0x07)
                    self.tx_is_first_data = False
                else:
                    self.tx_byte = _crc8([self.tx_byte], 0x07)
                self.tx_is_crc = True
                self.state = "transmit"
                self.tx_bit = 0
                self._drive_tx_bit()
            else:
                # CRC byte done: advance the auto-increment pointer, send next data byte.
                self.pointer = (self.pointer + 1) & 0xFF
                self._load_tx_byte()
                self.tx_is_crc = False
                self.state = "transmit"
                self.tx_bit = 0
                self._drive_tx_bit()

    def _sample_receive_bit(self, bit):
        if self.state != "receive":
            return
        self.current_byte = ((self.current_byte << 1) | (1 if bit else 0)) & 0xFF
        self.bit_count += 1
        if self.bit_count == 8:
            ack = self._process_received_byte(self.current_byte)
            self.current_byte = 0
            self.bit_count = 0
            self.pending_high_sample = False
            self.ack_clock_seen = False
            self.state = "ack" if ack else "ignore"
            self._drive_slave_sda(ack)

    def _process_received_byte(self, byte):
        byte = int(byte) & 0xFF
        self.rx_log_count += 1
        if len(self.rx_trace) < 24:
            self.rx_trace.append(byte)
        if self.rx_log_count <= 24:
            self.peripheral.dummy.Log(LogLevel.Info, "BQ76940 GPIO IIC rx byte 0x{0:X2} while expecting {1}", byte, self.expecting)
        if self.expecting == "address":
            address = (byte >> 1) & 0x7F
            read = (byte & 1) != 0
            if address != 0x08:
                self.address_nack_count += 1
                self.peripheral.dummy.Log(LogLevel.Debug, "BQ76940 GPIO IIC ignored address 0x{0:X2}", address)
                return False
            if read:
                self.expecting = "transmit"
            else:
                self.expecting = "register"
            return True

        if self.expecting == "register":
            self.pointer = byte
            self.pending_register = byte
            self.expecting = "data"
            return True

        if self.expecting == "data":
            self.pending_value = byte
            self.peripheral.write_register_from_firmware(self.pending_register, self.pending_value)
            self.expecting = "crc"
            return True

        if self.expecting == "crc":
            expected = _crc8([BQ_I2C_WRITE_ADDRESS, self.pending_register, self.pending_value], 0x07)
            if byte != expected:
                self.crc_error_count += 1
                self.peripheral.dummy.Log(
                    LogLevel.Warning,
                    "BQ76940 GPIO IIC CRC mismatch at 0x{0:X2}: got 0x{1:X2}, expected 0x{2:X2}",
                    self.pending_register,
                    byte,
                    expected,
                )
            self.pending_register = (self.pending_register + 1) & 0xFF
            self.expecting = "data"
            return True

        return True

    def _load_tx_byte(self):
        self.tx_byte_count += 1
        self.tx_byte = self.peripheral.read_register(self.pointer)

    def assert_and_dump_stats(self):
        if self.start_count == 0 or self.stop_count == 0:
            raise Exception("Software IIC firmware path did not produce START/STOP conditions")
        if self.rx_log_count == 0 or self.tx_byte_count == 0:
            raise Exception("Software IIC firmware path did not transfer BQ76940 bytes")
        if self.crc_error_count != 0:
            raise Exception("Software IIC firmware path produced {} CRC errors".format(self.crc_error_count))
        if self.address_nack_count != 0:
            raise Exception("Software IIC firmware path addressed {} unexpected devices".format(self.address_nack_count))

        print(
            'BMS_SOFT_IIC_STATS {{"starts":{0},"stops":{1},"rx_bytes":{2},'
            '"tx_bytes":{3},"crc_errors":{4},"address_nacks":{5},"rx_trace":{6},'
            '"api_writes":{7},"register_writes":{8}}}'.format(
                self.start_count,
                self.stop_count,
                self.rx_log_count,
                self.tx_byte_count,
                self.crc_error_count,
                self.address_nack_count,
                self.rx_trace,
                self.api_write_count,
                self.register_write_count,
            )
        )

    def _drive_tx_bit(self):
        bit = (self.tx_byte >> (7 - self.tx_bit)) & 1
        self._drive_slave_sda(bit == 0)

    def self_test(self):
        self._bitbang_start()
        self._bitbang_byte(BQ_I2C_WRITE_ADDRESS)
        self._bitbang_byte(REG_ADCGAIN1)
        self._bitbang_start()
        self._bitbang_byte(BQ_I2C_WRITE_ADDRESS | 1)
        value = self._bitbang_read_byte(nack=True)
        self._bitbang_stop()
        if value != self.peripheral.registers[REG_ADCGAIN1]:
            raise Exception("GPIO IIC self-test failed: read 0x{0:02X}, expected 0x{1:02X}".format(value, self.peripheral.registers[REG_ADCGAIN1]))
        self.peripheral.dummy.Log(LogLevel.Info, "BQ76940 GPIO IIC self-test passed: ADCGAIN1=0x{0:X2}", value)

    def _master_set(self, scl, sda):
        old_scl = self.master_scl
        old_sda = self._effective_sda()
        self.master_scl = bool(scl)
        self.master_sda = bool(sda)
        self._handle_line_change(old_scl, old_sda)

    def _bitbang_start(self):
        self._master_set(True, True)
        self._master_set(True, False)
        self._master_set(False, False)

    def _bitbang_stop(self):
        self._master_set(False, False)
        self._master_set(True, False)
        self._master_set(True, True)

    def _bitbang_byte(self, byte):
        for bit_index in range(8):
            bit = (byte >> (7 - bit_index)) & 1
            self._master_set(False, bit != 0)
            self._master_set(True, bit != 0)
            self._master_set(False, bit != 0)
        self._master_set(False, True)
        self._master_set(True, True)
        self._master_set(False, True)

    def _bitbang_read_byte(self, nack):
        value = 0
        self._master_set(False, True)
        for _ in range(8):
            self._master_set(True, True)
            value = ((value << 1) | (1 if self._effective_sda() else 0)) & 0xFF
            self._master_set(False, True)
        self._master_set(False, bool(nack))
        self._master_set(True, bool(nack))
        self._master_set(False, bool(nack))
        self._master_set(False, True)
        return value


def _monitor_add_watchpoint(address):
    bus = monitor.Machine.SystemBus

    def handle_gpio_write(_, __, ___, value):
        for adapter in _bq76940_gpio_i2c_instances.values():
            adapter.on_gpio_write(address, value)

    bus.AddWatchpointHook(address, SysbusAccessWidth.Byte, Access.Write, handle_gpio_write)
    bus.AddWatchpointHook(address, SysbusAccessWidth.Word, Access.Write, handle_gpio_write)
    bus.AddWatchpointHook(address, SysbusAccessWidth.DoubleWord, Access.Write, handle_gpio_write)


def mc_setup_bq76940_gpio_i2c(path):
    if path not in _bq76940_instances:
        raise Exception("BQ76940 instance '{}' is not set up; call setup_bq76940_i2c first".format(path))

    peripheral = _bq76940_instances[path]
    gpio_port = monitor.Machine["sysbus.gpioPortB"]
    adapter = BQ76940GpioI2CAdapter(peripheral, gpio_port)
    _bq76940_gpio_i2c_instances[path] = adapter

    _monitor_add_watchpoint(BQ76940GpioI2CAdapter.GPIOB_ODR)
    _monitor_add_watchpoint(BQ76940GpioI2CAdapter.GPIOB_BSRR)
    _monitor_add_watchpoint(BQ76940GpioI2CAdapter.GPIOB_BRR)
    _setup_bq76940_gpio_api_hooks(adapter)
    peripheral.dummy.Log(LogLevel.Info, "BQ76940 GPIO IIC adapter installed on PB8/PB9")


def mc_bq76940_gpio_i2c_self_test(path):
    if path not in _bq76940_gpio_i2c_instances:
        raise Exception("BQ76940 GPIO IIC adapter '{}' is not set up".format(path))
    _bq76940_gpio_i2c_instances[path].self_test()


def mc_assert_bq76940_gpio_i2c_firmware_path(path):
    if path not in _bq76940_gpio_i2c_instances:
        raise Exception("BQ76940 GPIO IIC adapter '{}' is not set up".format(path))
    _bq76940_gpio_i2c_instances[path].assert_and_dump_stats()


def _setup_bq76940_gpio_api_hooks(adapter):
    bus = monitor.Machine.SystemBus
    gpio_b = 0x40010C00

    def install(symbol, hook):
        for cpu in bus.GetCPUs():
            found, addresses = bus.TryGetAllSymbolAddresses(symbol, context=cpu)
            if not found:
                cpu.WarningLog("Symbol '{}' not found; BQ76940 GPIO API hook was not installed".format(symbol))
                continue
            for address in addresses:
                for hook_address in (address & ~1, address | 1):
                    cpu.AddHook(hook_address, hook)
                    cpu.InfoLog("Installed BQ76940 GPIO API hook at {} = 0x{:X}".format(symbol, hook_address))

    def set_bits(cpu, _):
        port = int(cpu.GetRegister(0).RawValue) & 0xFFFFFFFF
        pins = int(cpu.GetRegister(1).RawValue) & 0xFFFF
        if port == gpio_b and (pins & (BQ76940GpioI2CAdapter.SCL_MASK | BQ76940GpioI2CAdapter.SDA_MASK)) != 0:
            adapter.on_gpio_api_write(pins, True)
        cpu.PC = cpu.LR

    def reset_bits(cpu, _):
        port = int(cpu.GetRegister(0).RawValue) & 0xFFFFFFFF
        pins = int(cpu.GetRegister(1).RawValue) & 0xFFFF
        if port == gpio_b and (pins & (BQ76940GpioI2CAdapter.SCL_MASK | BQ76940GpioI2CAdapter.SDA_MASK)) != 0:
            adapter.on_gpio_api_write(pins, False)
        cpu.PC = cpu.LR

    def read_input_data_bit(cpu, _):
        port = int(cpu.GetRegister(0).RawValue) & 0xFFFFFFFF
        pins = int(cpu.GetRegister(1).RawValue) & 0xFFFF
        value = 0
        if port == gpio_b:
            value = adapter.gpio_api_read(pins)
        cpu.SetRegisterUlong(0, value)
        cpu.PC = cpu.LR

    def zephyr_set_scl(cpu, _):
        state = int(cpu.GetRegister(1).RawValue) != 0
        adapter.on_gpio_api_write(BQ76940GpioI2CAdapter.SCL_MASK, state)
        cpu.PC = cpu.LR

    def zephyr_set_sda(cpu, _):
        state = int(cpu.GetRegister(1).RawValue) != 0
        adapter.on_gpio_api_write(BQ76940GpioI2CAdapter.SDA_MASK, state)
        cpu.PC = cpu.LR

    def zephyr_get_scl(cpu, _):
        value = adapter.gpio_api_read(BQ76940GpioI2CAdapter.SCL_MASK)
        cpu.SetRegisterUlong(0, value)
        cpu.PC = cpu.LR

    def zephyr_get_sda(cpu, _):
        value = adapter.gpio_api_read(BQ76940GpioI2CAdapter.SDA_MASK)
        cpu.SetRegisterUlong(0, value)
        cpu.PC = cpu.LR

    install("GPIO_SetBits", set_bits)
    install("GPIO_ResetBits", reset_bits)
    install("GPIO_ReadInputDataBit", read_input_data_bit)
    install("i2c_gpio_set_scl", zephyr_set_scl)
    install("i2c_gpio_set_sda", zephyr_set_sda)
    install("i2c_gpio_get_scl", zephyr_get_scl)
    install("i2c_gpio_get_sda", zephyr_get_sda)
