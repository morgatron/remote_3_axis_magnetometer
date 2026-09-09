
- The current sensor build for "CREEK" still uses ~30mA between broadcasting batches of samples (in mock data mode, with no sensor attached), and we should understand why. In principle it could get down as low as ~10mA with DFS. All it _needs_ to be doing in this period is communicating over SPI to the ADC (if using FLC100s)
- We should do a similar analysis for the Heltec v4 + LoRa firmware (label: SPRINGBANK)
