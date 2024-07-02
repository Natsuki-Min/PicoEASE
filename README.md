# PicoEASE
An NXU16 Debugger

#pico
Pinout:
gpio2-p152 (sda), gpio3-p151 (sck), vcc-p154 (3v3), gnd-p172 (gnd), gpio4-vppctrl

Usage:
Claim:All the parameters are in HEX format and without '0x'
'B'
Connect to the chip
'W <address> <data>'
Write to the given address(Register)
'R <address>'
Read the given address(Register)
'T <div>'
Modify the speed of the communication
'A <address> <length>'
Read Flash form the given address and for given bytes(2bytes alignment) in Intel HEX format
