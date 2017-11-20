# gpio-pwm-ar9331
PWM generator using AR9331 hardware timer.

gpio-pwm-ar9331 is an OpenWRT kernel module which provides PWM output on any AR9331 SoC GPIO pin with up to 125 kHz frequency.


##Intallation
1. Build it using OpenWRT Buildroot.
2. Upload package to device using your favorite method. 
3. Run `opkg install kmod-gpio-pwm-ar9331_<version>.ipk` where `<version>` is version of a package.

##Building
1. Clone this repository to buildrootdir/package (i.e. 
`cd /path/to/buildroot/` then `cd package/` and `git clone https://github.com/ASMfreaK/gpio-pwm-ar9331.git`)
2. Run `make menuconfig` and select `Kernel modules  --->` then `Other modules  --->` and finally `kmod-gpio-pwm-ar9331`. Select other packages if needed and exit the dialog.
3. Run `make`
4. Install the freshly built firmware to BlackSwift.

##Initialisation
`insmod gpio-pwm-ar9331`<br />
Default is timer 3

##Usage

**Start**:  `echo "+ <timer> <GPIO> <freq> <pos>"  > /sys/kernel/debug/pwm-ar9331`<br />
`timer` is timer number (one of 0, 1, 2, 3), `GPIO` is GPIO number, `freq` is square wave frequency in Hz, `pos` is a number between 0 and 65536 representing duty cycle. At different frequencies it can generate up to 16 bit PWM Maximum frequency with 200 MHz system bus (default clock) is 125 kHz; high settings may result in performance penalty or watchdog reset.

**Example**: to blink Black Swift's system LED with 1 Hz frequency at around 1/2 duty cycle using timer 0: `echo "+ 0 27 1 3000" > /sys/kernel/debug/pwm-ar9331`

**Stop**: `echo - > /sys/kernel/debug/pwm-ar9331`

**Status**: `echo ? > /sys/kernel/debug/pwm-ar9331`

