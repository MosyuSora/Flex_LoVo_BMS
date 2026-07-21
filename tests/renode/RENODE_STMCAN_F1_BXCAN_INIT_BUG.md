# Renode STMCAN F1 bxCAN Initialization Bug

## Summary

Renode 1.16.0 `CAN.STMCAN` does not acknowledge bxCAN initialization when
`MCR.INRQ` is set while `MCR.SLEEP` is still set. This is the initialization
sequence used by Zephyr's STM32 bxCAN driver and by the STM32F1 hardware path.

This is a Renode peripheral-model bug, not a BMS application or board-specific
firmware defect.

## Environment

- Renode: 1.16.0, commit `20ad06d9-202508030343`
- Model: `CAN.STMCAN`
- Target: STM32F103 bxCAN, CAN1 at `0x40006400`
- Driver: Zephyr `can_stm32_bxcan.c`
- Test image: BMS Zephyr shipping-source build with test telemetry enabled

## Reproduction

The model resets the registers as follows:

```text
MCR = 0x00010002   # SLEEP=1
MSR = 0x00000C02   # SLAK=1, INAK=0
```

Zephyr then performs:

```text
read  MCR                 -> 0x00010002
write MCR |= INRQ         -> 0x00010003
read  MSR                 -> 0x00000C00
```

The model does not set `MSR.INAK` because its condition requires both
`INRQ=1` and `SLEEP=0`. Zephyr times out in `can_stm32_enter_init_mode()` and
reports `-ENODEV` from device initialization. The BMS telemetry is:

```text
BMS_CAN_STATE {"setup_result":-19,"filter_result":-1}
```

## Expected behavior

When `MCR.INRQ=1`, the model should acknowledge initialization with
`MSR.INAK=1`, even if `MCR.SLEEP` is still set. Zephyr clears `SLEEP` only after
observing `INAK`.

Expected sequence:

```text
write MCR 0x00010003
read  MSR                  -> INAK=1
write MCR 0x00010001
read  MSR                  -> INAK=1, SLAK=0
write MCR 0x00010000
read  MSR                  -> INAK=0, SLAK=0
```

## Proposed fix

In `STMCAN.WriteDoubleWord()` change the MCR state priority from:

```csharp
if(InitRequest && !SleepRequest)
```

to:

```csharp
if(InitRequest)
{
    registers.CAN_MSR.InitAck = true;
    registers.CAN_MSR.SleepAck = false;
}
else if(SleepRequest)
{
    registers.CAN_MSR.SleepAck = true;
    registers.CAN_MSR.InitAck = false;
}
else
{
    registers.CAN_MSR.SleepAck = false;
    registers.CAN_MSR.InitAck = false;
}
```

The existing sleep-mode branch should remain unchanged.

## Validation

With the corrected model and the unmodified Zephyr firmware:

```text
MCR: 0x10002 -> 0x10003
MSR: 0xC01   # INAK=1
BMS_CAN_STATE setup_result=0 filter_result=0
BMS_CAN_TX id=1..7 result=0
```

The test platform can select the corrected Renode with:

```powershell
& .\tests\renode\run_zephyr_uart_smoke.ps1 `
  -Scenario normal -SkipBuild -DurationSeconds 0.1 `
  -RenodePath C:\tools\renode-bms-canfix\bin\Renode.exe
```

The `-CanInitWorkaround` option is retained only as a diagnostic fallback and
must not be used for the formal result after the model fix is available.
