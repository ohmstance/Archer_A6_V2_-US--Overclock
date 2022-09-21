# Overclocked Bootloader for TP-Link Archer A6 V2 (US/TW)

This is a modified GPL code for Archer A6 V2 (US/TW) which builds a factory-uboot image with overclocking applied. The CPU, RAM, and AHB has been overclocked to 1.05 GHz, 700 MHz, and 350 MHz, from 775 MHz, 650 MHz, and 258 MHz respectively. The modifications seem to be stable on my device, but I cannot guarantee for yours. Flash it at your own risk.

One of the main issue with published GPL code for the US variant is that it seems to be meant for EU/RU version which has 8M flash instead of 16M flash and single u-boot partition instead of double. The code for the 16M variant (US/TW) is missing, with nothing pointing to double u-boot. Some string such as product name needed to be changed from 'C6V2' to 'A6V2' for it to trigger necessary preprocessor directives, although both Archer A6 V2 and Archer C6 V2 are identical. It's not as simple as changing build options. The provided GPL code is half-finished, as if an earlier, incomplete version of the code was published instead of the finalized version. 

I have modified it to produce a compatible factory-uboot for Archer A6 V2 with 16M flash. My modifications aim to build a usable factory-uboot partition. Other produced binaries were not tested.

## Installation
Make sure to back-up your factory-uboot partition! You can do this in OpenWRT by:

```dd if=/dev/mtd0 of=factory-boot.bin```

Then transfer the file to your computer.

### OpenWRT shell
The most easiest way if OpenWRT is installed on your router.
1. Transfer the factory-uboot image to your router
2. ```insmod mtd-rw i_want_a_brick=1```
3. ```mtd write factory-uboot-oc.bin /dev/mtd0```
4. Restart router

### Serial-TFTP
If device is unstable, and couldn't boot into OpenWRT, use TFTP via u-boot to flash stock factory-uboot.
1. Set up a TFTP server
2. Place factory-uboot image in the TFTP server directory
3. Change your ethernet device's speed to 100M
4. Set IP as 192.168.0.10 and subnet mask as 255.255.255.0
5. Connect to your router via serial header and ethernet
6. Start router and interrupt boot process by entering keys
7. Make sure you're in the first u-boot (factory u-boot) not the second one (fs u-boot) as the second one cannot write to write-protected area
8. ```tftpboot 0x80060000 factory-uboot-oc.bin``` (Can require multiple attempts and reconnecting ethernet to complete transfer)
9. ```erase 0x9f000000 +0x20000```
10. ```cp.b 0x80060000 0x9f000000 0x20000```
11. Restart router

## Result
Using ath10k-ct, with stock clocks, I've plateaud at around 220 Mb/s on iperf3 (iphone 13 to desktop), while overclocked it achieved 290 Mb/s. RAM/AHB speed does significantly affect wireless speed for this device. Installing ath10k instead brings speed up to 310 Mb/s.

## Addendum
I couldn't test WiFi performance on stock firmware as I bricked my device due to me erasing the factory-uboot partition. As I was making sure about my final commit regarding TFTP recovery timeout, I found myself unable to write my newly built u-boot to the factory-uboot partition due to it being write protected. Although the second u-boot does not allow writes to write-protected area of the flash, it allowed *erasing* them.
