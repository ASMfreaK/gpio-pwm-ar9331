/*
 *  GPIO square wave generator for AR9331
 *
 *    Copyright (C) 2013-2015 Gerhard Bertelsmann <info@gerhard-bertelsmann.de>
 *    Copyright (C) 2015 Dmitriy Zherebkov <dzh@black-swift.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/clk.h>

#include <asm/mach-ath79/ar71xx_regs.h>
#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/irq.h>

#include <asm/siginfo.h>
#include <linux/math64.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

////////////////////////////////////////////////////////////////////////////////////////////

#define DRV_NAME	"gpio-pwm-ar9331"
#define FILE_NAME	"pwm-ar9331"

////////////////////////////////////////////////////////////////////////////////////////////

//static int timer=3;
//module_param(timer, int, 0);
//MODULE_PARM_DESC(timer, "system timer number (0-3)");

////////////////////////////////////////////////////////////////////////////////////////////

//#define DEBUG_OUT

#ifdef	DEBUG_OUT
#define debug(fmt,args...)	printk (KERN_INFO DRV_NAME ": " fmt ,##args)
#else
#define debug(fmt,args...)
#endif	/* DEBUG_OUT */

static unsigned int _timer_frequency=200000000;
static spinlock_t	_lock;
static unsigned int	_gpio_prev[4]={0, 0, 0, 0};

////////////////////////////////////////////////////////////////////////////////////////////

#define ATH79_TIMER0_IRQ		ATH79_MISC_IRQ(0)
#define AR71XX_TIMER0_RELOAD	0x04

#define ATH79_TIMER1_IRQ		ATH79_MISC_IRQ(8)
#define AR71XX_TIMER1_RELOAD	0x98

#define ATH79_TIMER2_IRQ		ATH79_MISC_IRQ(9)
#define AR71XX_TIMER2_RELOAD	0xA0

#define ATH79_TIMER3_IRQ		ATH79_MISC_IRQ(10)
#define AR71XX_TIMER3_RELOAD	0xA8

struct _timer_desc_struct
{
	unsigned int	irq;
	unsigned int	reload_reg;
} _timers[4]=
{
		{	ATH79_TIMER0_IRQ, AR71XX_TIMER0_RELOAD	},
		{	ATH79_TIMER1_IRQ, AR71XX_TIMER1_RELOAD	},
		{	ATH79_TIMER2_IRQ, AR71XX_TIMER2_RELOAD	},
		{	ATH79_TIMER3_IRQ, AR71XX_TIMER3_RELOAD	}
};

////////////////////////////////////////////////////////////////////////////////////////////

#define GPIO_OFFS_READ		0x04
#define GPIO_OFFS_SET		0x0C
#define GPIO_OFFS_CLEAR		0x10

#define PWM_MAX			65536

////////////////////////////////////////////////////////////////////////////////////////////

void __iomem *ath79_timer_base=NULL;

void __iomem *gpio_addr=NULL;
void __iomem *gpio_readdata_addr=NULL;
void __iomem *gpio_setdataout_addr=NULL;
void __iomem *gpio_cleardataout_addr=NULL;

////////////////////////////////////////////////////////////////////////////////////////////

typedef struct
{
	int			timer;
	int			irq;
	int			gpio;
	unsigned int		frequency;
	unsigned int		timeout_total;
	unsigned int		timeout_front;
	unsigned int		timeout_back;
	unsigned int		new_pos;
	int			update;
	int			value;
} _timer_handler;

static _timer_handler	_handler[4];

static struct dentry* in_file;

////////////////////////////////////////////////////////////////////////////////////////////

inline void set_timer_reload(int timer, unsigned int freq)
{
	__raw_writel(freq, ath79_timer_base+_timers[timer].reload_reg);
}

////////////////////////////////////////////////////////////////////////////////////////////

inline void set_gpio_value(int gpio, int val)
{
	if(val)
	{
		__raw_writel(1 << gpio, gpio_setdataout_addr);
	}
	else
	{
		__raw_writel(1 << gpio, gpio_cleardataout_addr);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////

void recalculate_timeouts(_timer_handler* handler){
	unsigned int flags = 0;
	spin_lock_irqsave(&_lock,flags);

	handler->timeout_total = _timer_frequency/handler->frequency;

	debug("New timeout: %u\n",handler->timeout_total);

	handler->timeout_front =(unsigned int)(
		((unsigned long long)handler->timeout_total
		* (unsigned long long)handler->new_pos)
		/ PWM_MAX);

	debug("New front timeout: %u\n",handler->timeout_front);

	handler->timeout_back =
		handler->timeout_total - handler->timeout_front;

	debug("New back timeout: %u\n",handler->timeout_back);
	spin_unlock_irqrestore(&_lock,flags);
}

////////////////////////////////////////////////////////////////////////////////////////////

static int is_space(char symbol)
{
	return (symbol == ' ') || (symbol == '\t');
}

////////////////////////////////////////////////////////////////////////////////////////////

static int is_digit(char symbol)
{
	return (symbol >= '0') && (symbol <= '9');
}

////////////////////////////////////////////////////////////////////////////////////////////

static irqreturn_t timer_interrupt(int irq, void* dev_id)
{
	_timer_handler* handler=(_timer_handler*)dev_id;

	if(handler && (handler->irq == irq) && (handler->gpio >= 0))
	{
		if(handler->value)
		{
			__raw_writel(1 << handler->gpio, gpio_setdataout_addr);
			set_timer_reload(handler->timer, handler->timeout_back);
		}
		else
		{
			__raw_writel(1 << handler->gpio, gpio_cleardataout_addr);
			set_timer_reload(handler->timer, handler->timeout_front);
		}
		handler->value=!handler->value;
	}

	return(IRQ_HANDLED);
}

////////////////////////////////////////////////////////////////////////////////////////////

static int add_irq(const unsigned int timer, void* data)
{
	int err=0;
	int irq_number=_timers[timer].irq;

	debug("Adding IRQ %d handler\n",irq_number);

	err=request_irq(irq_number, timer_interrupt, 0, DRV_NAME, data);

	if(!err)
	{
		debug("Got IRQ %d.\n", irq_number);
		return irq_number;
	}
	else
	{
		debug("Timer IRQ handler: trouble requesting IRQ %d error %d\n",irq_number, err);
	}

    return -1;
}

////////////////////////////////////////////////////////////////////////////////////////////

static void stop(const unsigned int timer)
{
	unsigned long flags=0;

	spin_lock_irqsave(&_lock,flags);

	if(_handler[timer].irq >= 0)
	{
		free_irq(_handler[timer].irq, (void*)&_handler);
		_handler[timer].irq=-1;

		_handler[timer].timer=-1;

		if(_handler[timer].gpio >= 0)
		{
			set_gpio_value(_handler[timer].gpio, _gpio_prev[timer]);
			gpio_free(_handler[timer].gpio);
			_handler[timer].gpio=-1;
		}

		_handler[timer].frequency=0;
		_handler[timer].value=0;

		debug("Timer stopped.\n");
	}

	spin_unlock_irqrestore(&_lock,flags);
}

////////////////////////////////////////////////////////////////////////////////////////////

static int start(const unsigned int timer, int gpio,unsigned int frequency,unsigned int pos)
{
	int irq=-1;
	unsigned long flags=0;

	stop(timer);

	spin_lock_irqsave(&_lock,flags);
	// need some time (10 ms) before first IRQ - even after "lock"?!
	set_timer_reload(timer, _timer_frequency/100);
	debug("Getting IRQ\n");
	irq=add_irq(timer, &_handler);
	debug("Got IRQ\n");
	if(irq >= 0)
	{
		_handler[timer].timer=timer;
		_handler[timer].irq=irq;

		gpio_request(gpio, DRV_NAME);
		if(gpio_direction_output(gpio,0) == 0)
		{
			_handler[timer].gpio=gpio;

			// save current GPIO state
			_gpio_prev[timer]=__raw_readl(gpio_readdata_addr+GPIO_OFFS_READ) & (1 << gpio);


			_handler[timer].frequency = frequency;
			_handler[timer].new_pos = pos;
			recalculate_timeouts(&(_handler[timer]));
			set_timer_reload(timer, _handler[timer].timeout_front);

			debug("New frequency: %u.\n", frequency);

			spin_unlock_irqrestore(&_lock,flags);
			return 0;
		}
		else
		{
			debug("Can't set GPIO %d as output.\n", gpio);
		}
	}

	spin_unlock_irqrestore(&_lock,flags);

	stop(timer);
	return -1;
}

////////////////////////////////////////////////////////////////////////////////////////////

#define skip_whitespace() while((in_pos < end) && is_space(*in_pos)) ++in_pos

int parse_number(
	char* line, char* end,
	char* in_pos, char *out_pos,
	unsigned int *num)
{
	out_pos=line;
	while((in_pos < end) && is_digit(*in_pos)) *out_pos++=*in_pos++;
	*out_pos=0;
	debug("Got number string %s\n", line);
	if(is_digit(line[0])){
		sscanf(line, "%d", num);
		debug("Got number %d\n", *num);
		return 0;
	}else{
		return -1;
	}
}
#define parse_number_or_break(var, vvs) out_pos=line;\
	while((in_pos < end) && is_digit(*in_pos)) *out_pos++=*in_pos++;\
	*out_pos=0;\
	debug("Got number string %s\n", line);\
	if(is_digit(line[0])){\
		sscanf(line, "%d", &var);\
		debug("Got number %d\n", var);\
	}else{\
		printk(KERN_INFO DRV_NAME " can't read"vvs".\n");\
		break;\
	}

#define check_timer() if (timer > 3 || timer < 0){\
		printk(KERN_INFO DRV_NAME "only can use timers 0-3. Timer specified %d\n", timer);\
		break;}


static ssize_t run_command(struct file *file, const char __user *buf,
                                size_t count, loff_t *ppos)
{
	char buffer[512];
	char line[20];
	char* in_pos=NULL;
	char* end=NULL;
	char* out_pos=NULL;
	unsigned int flags = 0;

	if(count > 512)
		return -EINVAL;	//	file is too big

	copy_from_user(buffer, buf, count);
	buffer[count]=0;

	debug("Command is found (%u bytes length):\n%s\n",count,buffer);

	in_pos=buffer;
	end=in_pos+count-1;

	while(in_pos < end)
	{
		unsigned int timer=-1;
		unsigned int gpio=-1;
		unsigned int frequency=0;
		unsigned int pos=0;

		skip_whitespace();

		if(in_pos >= end) break;

		if(*in_pos == '-')
		{
			skip_whitespace();
			parse_number_or_break(timer, "timer numer");
			check_timer();
			stop(timer);
			return count;
		}
		if(*in_pos == '?')
		{
			skip_whitespace();

			parse_number_or_break(timer,"timer number");

			if(_handler[timer].frequency)
			{
				printk(
					KERN_INFO DRV_NAME " is running "\
					"on GPIO %d "\
					"with frequency %u Hz "\
					"(system timer %d).\n",
					_handler[timer].gpio,
					_handler[timer].frequency,
					_handler[timer].timer
					);
			}
			else
			{
				printk(KERN_INFO DRV_NAME " is not running (timer %d selected).\n",timer);
			}

			break;
		}
		if(*in_pos == '+'){
			++in_pos;
			skip_whitespace();
			if(in_pos >= end) break;

			parse_number_or_break(timer, "timer number");
			check_timer();
			skip_whitespace();

			parse_number_or_break(gpio, "GPIO number");
			skip_whitespace();

			parse_number_or_break(frequency, "frequency");

			skip_whitespace();

			parse_number_or_break(pos, "PWM ratio");

			if((gpio >= 0) && (frequency > 0))
			{
				if(start(timer, gpio, frequency, pos) >= 0)
				{
					debug("Started!\n");
					break;
				}
			}
		}
		printk(KERN_INFO DRV_NAME " can't start.\n");
		return -EFAULT;
	}

	return count;
}

////////////////////////////////////////////////////////////////////////////////////////////

static const struct file_operations irq_fops =
{
	.write = run_command,
};

////////////////////////////////////////////////////////////////////////////////////////////

struct clk	//	defined in clock.c
{
	unsigned long rate;
};

////////////////////////////////////////////////////////////////////////////////////////////

static int __init mymodule_init(void)
{
	struct clk* ahb_clk=clk_get(NULL,"ahb");
	int timer = 0;
	if(ahb_clk)
	{
		_timer_frequency=ahb_clk->rate;
	}

	ath79_timer_base=ioremap_nocache(AR71XX_RESET_BASE, AR71XX_RESET_SIZE);

	gpio_addr=ioremap_nocache(AR71XX_GPIO_BASE, AR71XX_GPIO_SIZE);

	gpio_readdata_addr     = gpio_addr + GPIO_OFFS_READ;
	gpio_setdataout_addr   = gpio_addr + GPIO_OFFS_SET;
	gpio_cleardataout_addr = gpio_addr + GPIO_OFFS_CLEAR;

	for(timer=0; timer<4; timer++){
		_handler[timer].timer=-1;
		_handler[timer].irq=-1;
		_handler[timer].gpio=-1;
		_handler[timer].frequency=0;
		_handler[timer].value=0;
	}

	in_file=debugfs_create_file(FILE_NAME, 0200, NULL, NULL, &irq_fops);

	debug("System timer #%d frequency is %d Hz.\n",timer,_timer_frequency);
	printk(KERN_INFO DRV_NAME " is waiting for commands in file /sys/kernel/debug/" FILE_NAME ".\n");

    return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////

static void __exit mymodule_exit(void)
{
	int timer =0;
	for(timer = 0; timer<4; timer++){
		stop(timer);
	}

	debugfs_remove(in_file);

	return;
}

////////////////////////////////////////////////////////////////////////////////////////////

module_init(mymodule_init);
module_exit(mymodule_exit);

////////////////////////////////////////////////////////////////////////////////////////////

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitriy Zherebkov (Black Swift team)");

////////////////////////////////////////////////////////////////////////////////////////////
