
---

## PicoEASE
**An NXU16 Debugger**

### pico Pinout:
- gpio2-sda
- gpio3-sck
- vcc-3v3
- gnd-gnd
- gpio4-vppctrl

### Usage:
***Claim:*** All the parameters are in HEX format and without '0x'
- **'B'**
  - Connect to the chip
- **'W \<address\> \<data\>'**
  - Write to the given address (Register)
- **'R \<address\>'**
  - Read the given address (Register)
- **'T \<div\>'**
  - Modify the speed of the communication
- **'X \<command\>'**
  - Run an assembly command (or two) and return the EA and R0's data. It's used to read and write the RAM.
  - e.g. F00CD000 for `lea 0xd000`, E3009050 for `l r0 [ea+]`
- **'A \<address\> \<length\>'**
  - Read Flash from the given address for the specified bytes (2 bytes alignment) in Intel HEX format.
- **'G \<address\> \<length\>'**
  - Read Flash from the given address for the specified bytes (2 bytes alignment) in Bin format(Faster).
***Caution!*** The following commands need VPP supply! Due to the auto increase address, the length can't be larger than one segment, otherwise it will loop back. Also include 'Q' command e.g. 1FFFF+1 -> 10000.
- **'E \<address\>'**
  - Erase one block where the address is. One block is the size of 0x4000 in cw1.
- **'F \<address\> \<length\>'**
  - Fill the flash from the given address with 0xFF for the specified length.
- **'D'**
  - Erase all the Flash.
- **'C \<address\> \<data\>'**
  - Write 2bytes to the address
- **'Q \<address\>'**
  - Enter flash writing mode. When in this mode, just type HEX (must be 2-byte alignment, must be below 1kb) and enter , e.g. `"ABCDEF1234567890\n"`, it will write to the flash and automatically increase the address. `"Q\n"` for exit.
- **'S'**
  - Initialize the Flash and Security ID,use it when you forget the password or you accidentally erased the password and exited debug without fill the password.
### picolistening
- gpio2-sda
- gpio3-sck
- gnd-gnd
- gpio4-open (must)

This is for listening to the communication from the uease. Use `prase.py` to convert the contents from the Serial to readable format.Due to my low programming skills，the speed of the pio must be 24 times faster than uEASE ,so run it under 250Mhz,instead of 133Mhz default

### **BOTH MUST COMPILE IN ARDUINO PICO INSTEAD OF MBED OS**
