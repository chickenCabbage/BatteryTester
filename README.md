# BatteryTester
ATmega-based battery tester for FIRST Robotics batteries, designed for ease-of-use for FIRST students and with built-in safety features.

The tester is soldered onto perfboard and is run by a "bare" ATmega328P. It tests battery voltage and state-of-charge using the internal ADC.
Battery internal resistance is tested using high-current loads switched with MOSFETs - by measuring the voltage at the battery terminals under different currents, the tester can calculate the resistance per Ohm's Law.
