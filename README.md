# gpio-sqwave
Square wave generator using AR9331 hardware timer.

gpio-sqwave is an OpenWRT kernel module which provides square wave output on any AR9331 SoC GPIO pin with up to 125 kHz frequency.

##Installation

*insmod gpio-sqwave [timer=0|1|2|3]*<br />
Default is timer 3

##Usage

**Start**: *echo "<GPIO> <freq>" > /sys/kernel/debug/sqwave*<br />
"*GPIO*" is GPIO number, "*freq*" is square wave frequency in Hz. Maximum frequency with 200 MHz system bus (default clock) is 125 kHz; high settings may result in performance penalty or watchdog reset.

**Example**: to blink Black Swift's system LED with 1 Hz frequency: *echo "27 1" > /sys/kernel/debug/sqwave*

**Stop**: *echo - > /sys/kernel/debug/sqwave*

**Status**: *echo ? > /sys/kernel/debug/sqwave*

Check http://www.black-swift.com/wiki for more information.
