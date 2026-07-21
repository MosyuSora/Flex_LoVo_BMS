# Renode CAN capture

The Zephyr Renode tests expose `can1` through a Renode `CANHub`.

## Capture a real CAN trace

Run the normal scenario with the model workaround while the Renode CAN model
still has the F1 bxCAN init-handshake issue:

```powershell
.\tests\renode\run_zephyr_uart_smoke.ps1 `
  -Scenario normal -SkipBuild -CanInitWorkaround -CaptureCan
```

`-CaptureCan` inserts `emulation LogToWireshark canHub` into the generated
Renode script. Wireshark then shows the frames emitted on the CAN hub. The
Renode configuration file must point `wireshark-path` at `Wireshark.exe`.

## Capture register-level initialization

Use both switches when diagnosing the Renode CAN model:

```powershell
.\tests\renode\run_zephyr_uart_smoke.ps1 `
  -Scenario normal -SkipBuild -CanInitWorkaround -TraceCan -CaptureCan
```

`-TraceCan` logs CAN peripheral register accesses such as MCR and MSR. It is
not a CAN frame capture. `BMS_CAN_TX` is firmware telemetry for the result of
calling `can_send`; the Wireshark capture is the bus-level evidence.

The same `-CaptureCan` switch is available on
`run_zephyr_stress_test.ps1`.
