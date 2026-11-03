# VALVER SMART DISPENSER - StallGuard4 (experimental)

## Hardware required
The existing STEP/DIR/EN wiring alone cannot measure mechanical load. UART must be added separately to each BIGTREETECH TMC2209 V1.3. This proposed mapping is coded in `firmware/controller/src/main.cpp`:

| Axis | ESP32-S3 RX | ESP32-S3 TX | TMC2209 PDN_UART |
| --- | --- | --- | --- |
| Z1 conductivo | GPIO17 | GPIO18 through 1 kOhm | RX direct and TX via 1 kOhm to same PDN_UART |
| Z2 estructural | GPIO8 | GPIO9 through 1 kOhm | RX direct and TX via 1 kOhm to same PDN_UART |

Connect logic VIO to 3.3 V, and share logic GND. Keep existing STEP/DIR/EN. The BTT V1.3 UART jumper / R10 solder assignment must match the actual PDN_UART pad. Confirm board markings with the BTT manual, not pin numbers guessed from orientation. Both drivers use independent UARTs at 115200 baud, address 0.

**Disconnect USB and 24 V before changing cables.** TMC2209 motor power needs VM=+24V and supply negative at GND. Never connect 24 V to logic GPIO/VIO.

## Interface workflow
1. Flash firmware 0.5.0-stallguard-uart.
2. Connect HMI, click Sensores; verify Z1 UART OK and Z2 UART OK individually.
3. Select the axis, choose SGTHRS 0..255, then Apply sensitivity.
4. Click Activate load stop for that axis only after verifying the UART. The HMI deliberately disables activation when UART is missing.
5. Jog carefully in a clear section of travel; check SG_RESULT while changing load gradually. A higher SG_RESULT represents lower load; if SG_RESULT <= 2 * SGTHRS for two samples after the startup ignore window, firmware disables EN and reports STALL_DETECTED.
6. To disable protection for normal test motion, click Deactivate.

Default threshold 40 is ONLY a placeholder, not a calibrated value. Higher SGTHRS makes StallGuard4 more sensitive. The firmware only samples for speeds >= 400 STEP pulses/second and after a 400 ms startup interval. Actual minimum usable speed depends strongly on microstep setting, motor and current; 400 is not a validated sensitivity threshold.

The current implementation polls UART every 30 ms, not a hard real-time collision detector. It does not measure force in N and cannot guarantee gentle needle-table contact. StallGuard4 is often unreliable at low speed, and a motor can apply significant force before a stall is recognized.

**Do not use StallGuard4 as the sole table contact detector or as a substitute for a physical upper limit / emergency stop.** Test with the needle removed from the machine until protection has been validated on each axis at the actual speed, motor current, temperature, and mechanics. For genuinely gentle contact use a dedicated force/contact sensor.

Source: Analog Devices TMC2209 datasheet Rev 1.09, sections 5.3, 11.2, 11.5; BIGTREETECH TMC2209 V1.3 user manual.
