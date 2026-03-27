This is a mouse project for PAW3327DB‐TWQU sensor and Supermini nrf52840 board.

# Necessary components:
| component | quanlity | note |
| :-------- | :------: | -----|
| Supermini nrf52840 | 2 | |
| PAW3327DB‐TWQU | 1 | please buy with its len and led |
| TTP223 touch sensor | 2 | buy the chip, not the board |
| 0805 100nf capacitor | 4 | |
| 0805 10uf capacitor | 2 | |
| 0805 33pf capacitor | 2 | |
| 0805 10kΩ resistor | 2 | |
| 0805 33Ω resistor | 1 | |
| Molex picoblade female 1.25mm 4 pins horizontal | 2 | for scroll and middle button |
| Molex picoblade female 1.25mm 3 pins horizontal | 2 | for left side buttons |
| Molex picoblade female 1.25mm 2 pins vertucal | 1 | for battery |
| Molex picoblade male cable 1.25mm 4,3,2 pins | several | just buy more than you need, in case of you want to adjust the cable length |
| 3x2x9 500mah battery | 1 | |
| Low profile keyboard switch | 2 | any brand will work |
| Mouse switch (2 pins) | 3 | So i used mouse switch and noticed that the its height is quite high and block the touch point on scroll pcb, so i instead use 3x6x2.5mm smd button (2 pins) |
| 6x6mm tactile switch | 2 | for DPI and reset button |
| 3 pins spdt slide switch | 1 | to turn on/off battery |


# Mouse schematic:
<img width="2245" height="1587" alt="mouse" src="https://github.com/user-attachments/assets/2994b987-6792-46c2-ad4f-326fce28b2eb" />

This PAW3327DB‐TWQU schematic is followed in the sensor data sheet (P.18 [here](https://gzhls.at/blob/ldb/e/9/c/9/db61cdd17b4364badf6f0cfc864d7ef28cb3.pdf)).
Two 33pf capacitors (C6 and C7) are sensitivity capacitor for TTP223, the range from 0 to 50pf, the larger value the less sensitivity of the TTP223.

A note for the battery that if you use 500mah or larger battery, please connect the BOOST pad on the Supermini nrf52840.

<img width="300" alt="image" src="https://github.com/user-attachments/assets/3bf085cd-d4c8-43bd-872d-8e1adc9aac41" />


# PCB:
This design has 3 parts but i had to combine them in to one board to cut cost. You can safely cut it with handsaw, and use sand paper to smooth the edge.

<img width="500" alt="image" src="https://github.com/user-attachments/assets/e6efb828-783c-4dfa-882b-063c12f4255b" />


# Mouse case
| component | quanlity | note |
| :-------- | :------: | -----|
| M2 Hex Female-Female Standoff 4mm | 4 | for main pcb |
| M2 Hex Female-Female Standoff 10mm | 2 | for scroll pcb standoff |
| M2 bolt 6mm | several | |
| M2 nut | several | |

I followed PAW3327DB‐TWQU [datasheet](https://gzhls.at/blob/ldb/e/9/c/9/db61cdd17b4364badf6f0cfc864d7ef28cb3.pdf) (P.11-15).
In this repo i design one with autocad (i dont have knowledge about 3d print), it just a prototype but i used it for sevaral months now, seems fine.


# Firmware
There are 2 codes, one for the mouse and one for receiver. To build it please install Visual Code and install nRF Connect extension for the sake of simplicity, it will install SDK and compiler for you.
Then open those 2 folders in VS Code.

<img width="400" alt="image" src="https://github.com/user-attachments/assets/fbf5587e-acdf-48c6-9a3b-524c722bec20" />

To build it, navigate to nRF connect tab and create build configuration.

<img width="400" alt="image" src="https://github.com/user-attachments/assets/2359c7ac-6d88-4163-9e00-a9c9893a43d0" />

For Supermini nrf52840, follow this setting.

<img width="400" alt="image" src="https://github.com/user-attachments/assets/447edff6-7c4a-42a2-99f2-0d57969e7a46" />

After you completed the building, it will create the firmware at ../build/zephyr/zephyr.hex . Then use the python script (with the json file in the same folder) in this repo to convert the hex to uf2 with the command:
`python uf2conv.py "path/to/your/zephyr.hex" -c -f 0xADA52840`
Finally to flash the firmware to the board, connect your board to your computer, quickly connect reset pin to GND 2 times, when you see the led slowly fading it means the board is in bootloader mode. On your computer now will show the drive of the board, simply copy the uf2 firmware to the drive and it will flash the board.

# Notes
- In nRF build configuration, at the board target it does have uf2 option for this board, but for some reason it cant be flash to the board so i have to convert the hex myself
- In both of the code for the mouse and the receiver, there is esb address for rf signal, you should change it to your desire address (base_addr_0), and the address should be the same for both sides.
<img width="400" alt="image" src="https://github.com/user-attachments/assets/4b6a9a19-ab08-438f-8c21-44211b89c958" />

- In mouse code, i commented out some of the interupt code, last time i was testing the motion pin and interupt but it feel delay, so i kept the code with polling style. because of it the battery usage is around 1.5 days of intense use with some break (im not familiar with embedded stuff, so my code is kinda ugly).
- When mouse idle for more than 4 mins, it will shutdown until you press left click.
- DPI value in mouse code is hand measurement, i use a ruler to calculate the resolution value of the mouse sensor, around 102 is equilevent to 3200 DPI.
- Also in mouse code, i commented out the setting function, nRF have local storage for setting, but last time i used it somehow brick the board. So for safety concern i don't use that function.


# Demo (early prototype)
https://github.com/user-attachments/assets/07815808-ed37-4bc4-9238-eaf0a6d82ae9

# Thought
The cost per mouse is arround 12-15$, which is quite cheap to be fair, especially when you buy the components in bulk (1$ for 20 TTP223 sensors!). Goodluck if you want to make mouse!
