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
| Mouse switch (2 pins) | 3 | |
| 6x6mm tactile switch | 2 | for DPI and reset button |


# Mouse schematic:
<img width="2245" height="1587" alt="mouse" src="https://github.com/user-attachments/assets/2994b987-6792-46c2-ad4f-326fce28b2eb" />

This PAW3327DB‐TWQU schematic is followed in the sensor data sheet (P.18 [here](https://gzhls.at/blob/ldb/e/9/c/9/db61cdd17b4364badf6f0cfc864d7ef28cb3.pdf)).
Two 33pf capacitors (C6 and C7) are sensitivity capacitor for TTP223, the range from 0 to 50pf, the larger value the less sensitivity of the TTP223.

A note for the battery that if you use 500mah or larger battery, please connect the BOOST pad on the Supermini nrf52840.
<img width="544" height="585" alt="image" src="https://github.com/user-attachments/assets/3bf085cd-d4c8-43bd-872d-8e1adc9aac41" />


