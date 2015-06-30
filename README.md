# gpio-pwm-ar9331
PWM generator using AR9331 hardware timer.

gpio-pwm-ar9331 is an OpenWRT kernel module which provides PWM output on any AR9331 SoC GPIO pin with up to 125 kHz frequency.


##Intallation
1. Download binary package from [releases section](https://github.com/ASMfreaK/gpio-pwm-ar9331/releases) or build it using OpenWRT Buildroot.
2. Upload package to device using your favorite method. 
3. Run `opkg install kmod-gpio-pwm-ar9331_<version>.ipk` where `<version>` is version of a package. If it fails, rerun command with `--force-depends` flag i.e. `opkg install --force-depends  kmod-gpio-pwm-ar9331_<version>.ipk`


##Initialisation
`insmod gpio-sqwave [timer=0|1|2|3]`<br />
Default is timer 3

##Usage

**Start**:  `echo "<GPIO> <freq> <pos>"  > /sys/kernel/debug/pwm-ar9331`<br />
`GPIO` is GPIO number, `freq` is square wave frequency in Hz, `pos` is a number between 0 and 65536 representing duty cycle. At different frequencies it can generate up to 16 bit PWM Maximum frequency with 200 MHz system bus (default clock) is 125 kHz; high settings may result in performance penalty or watchdog reset.

**Example**: to blink Black Swift's system LED with 1 Hz frequency at around 1/2 duty cycle: `echo "27 1 3000" > /sys/kernel/debug/pwm-ar9331`

**Stop**: `echo - > /sys/kernel/debug/pwm-ar9331`

**Status**: `echo ? > /sys/kernel/debug/pwm-ar9331`

