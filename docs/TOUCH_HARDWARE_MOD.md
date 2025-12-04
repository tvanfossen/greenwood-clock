# ESP32-P4 Touch Hardware Modification Guide

## Overview

The ESP32-P4 Function EV Board requires a hardware modification to enable touchscreen functionality. This involves adding a jumper wire to connect the touch interrupt pin.

## Required Materials

- 1x Jumper wire (or solid core wire)
- Tweezers or small pliers (optional, for precise placement)

## Hardware Modification Steps

### Touch Interrupt Connection

The ESP32-P4 touch controller requires a physical connection between the touch panel's interrupt pin and the ESP32-P4 GPIO pin.

**Important:** The specific GPIO pin and connection point depend on your board revision. Please consult the ESP32-P4 Function EV Board schematic for the exact location.

### Common Connection Points

Based on typical ESP32-P4 Function EV Board configurations:

1. **Touch Interrupt Pin (Touch Panel)**: Usually labeled as `INT` or `TP_INT` on the connector
2. **ESP32-P4 GPIO Pin**: Typically GPIO pin assigned for touch interrupt

### Installation Steps

1. **Power Off**: Ensure the device is completely powered off and disconnected from all power sources

2. **Locate Connection Points**:
   - Find the touch panel connector on the board
   - Identify the interrupt pin (INT/TP_INT)
   - Locate the corresponding GPIO pin on the ESP32-P4

3. **Install Jumper Wire**:
   - Cut a jumper wire to appropriate length (keep it short to minimize interference)
   - Strip a small amount of insulation from both ends
   - Connect one end to the touch panel interrupt pin
   - Connect the other end to the designated GPIO pin
   - Ensure connections are secure and not creating any shorts

4. **Verify Connections**:
   - Visually inspect the jumper wire placement
   - Ensure no other pins are accidentally bridged
   - Check that the wire is securely attached

5. **Secure Wire** (Optional but Recommended):
   - Use a small amount of hot glue or electrical tape to secure the wire
   - This prevents the wire from coming loose during use

## Board-Specific Notes

### ESP32-P4-Function-EV-Board Revision Information

**Please check your board revision and consult the official schematic before proceeding.**

The board schematic can be found at:
- [ESP32-P4 Function EV Board Documentation](https://github.com/espressif/esp-dev-kits/tree/master/esp32-p4-function-ev-board)

### Alternative: Check BSP Documentation

The board support package (BSP) used in this project may contain specific information:
```c
// Check: managed_components/espressif__esp32_p4_function_ev_board/
// Look for touch configuration in board init files
```

## Verification

After completing the hardware modification:

1. **Visual Inspection**:
   - Verify jumper is properly connected
   - Check for any loose connections
   - Ensure no short circuits

2. **Software Test**:
   - Flash the modified firmware with touch enabled
   - Monitor serial output for touch events
   - Test touch responsiveness

## Safety Warnings

- **ALWAYS** power off the device before making hardware modifications
- Be careful not to short any pins during installation
- Use appropriate ESD (Electrostatic Discharge) precautions when handling the board
- If you're unsure about the modification, consult the official documentation or seek assistance

## Troubleshooting

### Touch Not Responding

1. **Check Physical Connection**:
   - Verify jumper wire is properly seated
   - Check for loose connections

2. **Check Software Configuration**:
   - Ensure touch is enabled in code (main.cpp:130)
   - Verify GPIO pin configuration matches hardware

3. **Check Serial Logs**:
   ```
   I (xxxx) main: [2] Touch input enabled
   I (xxxx) ui: Touch: PRESSED
   I (xxxx) ui: Touch: RELEASED
   ```

### Intermittent Touch Response

- Check for loose jumper wire connection
- Verify wire is not picking up electrical interference
- Ensure wire is properly shielded/secured

## References

- [ESP32-P4 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-p4_datasheet_en.pdf)
- [ESP32-P4 Function EV Board User Guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/index.html)
- [ESP-IDF Touch Sensor Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/api-reference/peripherals/touch_pad.html)

## Board Photo Reference

*Note: Add photos showing the exact connection points for your specific board revision*

---

**Created**: 2025-12-02
**Last Updated**: 2025-12-02
**Board**: ESP32-P4-Function-EV-Board
**Project**: Greenwood Clock
