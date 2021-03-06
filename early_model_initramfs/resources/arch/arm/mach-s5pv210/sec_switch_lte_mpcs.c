/*
 * UART/USB path switching driver for Samsung Electronics devices.
 *
 * Copyright (C) 2010 Samsung Electronics.
 *
 * Authors: Ikkeun Kim <iks.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/switch.h>
#include <linux/fsa9480.h>
#include <linux/mfd/max8998.h>
#include <linux/regulator/consumer.h>
#include <linux/moduleparam.h>
#include <asm/mach/arch.h>
#include <mach/param.h>
#include <mach/gpio.h>
#include <mach/sec_switch.h>
#include <mach/regs-clock.h>
#include <mach/regs-gpio.h>
#include <plat/gpio-cfg.h>
#include <linux/i2c.h>

struct sec_switch_struct {
	struct sec_switch_platform_data *pdata;
	int switch_sel;
};

struct sec_switch_wq {
	struct delayed_work work_q;
	struct sec_switch_struct *sdata;
	struct list_head entry;
};

static int initial_path_sel = 0;
static int finish_sec_switch_init = 0;
#ifdef CONFIG_MACH_CHIEF
struct sec_switch_struct *secsw_switch;
#endif
/* for sysfs control (/sys/class/sec/switch/) */
extern struct device *switch_dev;
/*extern ssize_t fsa9480_show_device_type(struct device *dev,
                                   struct device_attribute *attr,
                                   char *buf);*/

#define UART_PATH_MTM  (1<<5)
#define CABLE_CONNECTED      4
extern int usb_cable, uart_cable;

int curr_uart_switch_mode;

EXPORT_SYMBOL(curr_uart_switch_mode);

static void uart_switch_mode(int mode)
{
	curr_uart_switch_mode = mode;
	
	pr_info("%s : mode = %d\n", __func__, mode);
	if (mode == SWITCH_PDA)	{
	#ifdef CONFIG_MACH_CHIEF
		gpio_set_value(GPIO_UART_SEL, 1);
		fsa9480_manual_switching(SWITCH_PORT_AUTO);
	#else
		gpio_set_value(GPIO_USB_SW_EN1, 1);
		gpio_set_value(GPIO_UART_SEL, 1);
		fsa9480_manual_switching(SWITCH_PORT_AUTO);
	#endif
	} else if (mode == SWITCH_MODEM) {
        #ifdef CONFIG_MACH_CHIEF
        	gpio_set_value(GPIO_UART_SEL, 0);
    		gpio_set_value(GPIO_UART_SEL1, 0);
		fsa9480_manual_switching(SWITCH_PORT_AUTO);
	#else
		gpio_set_value(GPIO_USB_SW_EN1, 1);
     		gpio_set_value(GPIO_UART_SEL1, 0);
        	fsa9480_manual_switching(SWITCH_PORT_VAUDIO);
        #endif
	} else {
	#ifdef CONFIG_MACH_CHIEF
		gpio_set_value(GPIO_UART_SEL1, 1);
		gpio_set_value(GPIO_UART_SEL, 0);		
		fsa9480_manual_switching(SWITCH_PORT_AUTO);
	#else
		gpio_set_value(GPIO_USB_SW_EN1, 1);
		gpio_set_value(GPIO_UART_SEL, 0);
		fsa9480_manual_switching(SWITCH_PORT_AUTO);
        #endif
	}
}

static void usb_switch_mode(int mode)
{
	pr_info("%s : mode = %d\n", __func__, mode);
	if (mode == SWITCH_PDA)	{
	#ifdef CONFIG_MACH_CHIEF
		if (secsw_switch->pdata && secsw_switch->pdata->set_vbus_status)
		secsw_switch->pdata->set_vbus_status((u8)USB_VBUS_AP_ON);
		mdelay(10);
		fsa9480_manual_switching(SWITCH_PORT_AUTO);
        #else
		gpio_set_value(GPIO_USB_SW_EN1, 1);
		fsa9480_manual_switching(SWITCH_PORT_AUTO);
	#endif
	} else if (mode == SWITCH_MODEM) {
	
	#ifdef CONFIG_MACH_CHIEF
		gpio_set_value(GPIO_USB_SEL, 0);
	        if (secsw_switch->pdata && secsw_switch->pdata->set_vbus_status)
        	secsw_switch->pdata->set_vbus_status((u8)USB_VBUS_CP_ON);
        	mdelay(10);
        	fsa9480_manual_switching(SWITCH_PORT_AUDIO);
        #else 
       		gpio_set_value(GPIO_USB_SW_EN1, 1);
		gpio_set_value(GPIO_UART_SEL1, 1);
		fsa9480_manual_switching(SWITCH_PORT_VAUDIO);
	#endif
	} else {
         #ifdef CONFIG_MACH_CHIEF
		gpio_set_value(GPIO_USB_SEL, 1);
         	if (secsw_switch->pdata && secsw_switch->pdata->set_vbus_status)
         	secsw_switch->pdata->set_vbus_status((u8)USB_VBUS_CP_ON);
         	mdelay(10);
        	 fsa9480_manual_switching(SWITCH_PORT_AUDIO);
         #else
		 gpio_set_value(GPIO_USB_SW_EN1, 0);
		 fsa9480_manual_switching(SWITCH_PORT_OPEN);
         #endif
		 	}
	}

void sec_uart_switch(void)
{
	int switch_sel = UART_PATH_MODEM;
	if (!finish_sec_switch_init) {
		pr_info("%s : skip for initialing\n", __func__);
		initial_path_sel = UART_PATH_MASK;
		return;
	}
	if (sec_get_param_value)
		sec_get_param_value(__SWITCH_SEL, &switch_sel);
	uart_switch_mode((switch_sel & UART_PATH_MASK) >> 2);
}
EXPORT_SYMBOL(sec_uart_switch);


void sec_usb_switch(void)
{
	int switch_sel = USB_PATH_PDA;
	if (!finish_sec_switch_init) {
		pr_info("%s : skip for initialing\n", __func__);
		initial_path_sel = USB_PATH_MASK;
		return;
	}
	if (sec_get_param_value)
		sec_get_param_value(__SWITCH_SEL, &switch_sel);
	usb_switch_mode(switch_sel & USB_PATH_MASK);
}
EXPORT_SYMBOL(sec_usb_switch);

static ssize_t uart_sel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sec_switch_struct *secsw = dev_get_drvdata(dev);
	int uart_sel = (secsw->switch_sel & UART_PATH_MASK) >> 2;
	char *chip_sel[] = {"MODEM", "PDA", "LTEMODEM"};
	return sprintf(buf, "%s UART\n", chip_sel[uart_sel]);
}

static ssize_t uart_sel_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct sec_switch_struct *secsw = dev_get_drvdata(dev);
	int value;
	value = uart_cable;
	printk("%s\n",__func__);
	printk("%s the device type detected is %x \n ",__func__, value);
	if (sec_get_param_value)
		sec_get_param_value(__SWITCH_SEL, &secsw->switch_sel);    
    
	if (strncmp(buf, "PDA", 3) == 0 || strncmp(buf, "pda", 3) == 0) {
	//	if (value == CABLE_CONNECTED)
		    uart_switch_mode(SWITCH_PDA);
		secsw->switch_sel = (secsw->switch_sel & ~UART_PATH_MASK) | UART_PATH_PDA;
		pr_info("[UART Switch] Path : PDA\n");
	}

	if (strncmp(buf, "MODEM", 5) == 0 || strncmp(buf, "modem", 5) == 0) {
	//	if (value == CABLE_CONNECTED)
		   uart_switch_mode(SWITCH_MODEM);
		secsw->switch_sel = (secsw->switch_sel & ~UART_PATH_MASK) | UART_PATH_MODEM;
		pr_info("[UART Switch] Path : MODEM\n");
	}

	if (strncmp(buf, "LTEMODEM", 8) == 0 || strncmp(buf, "ltemodem", 8) == 0) {
	//	if (value == CABLE_CONNECTED)
		   uart_switch_mode(SWITCH_LTE);
		secsw->switch_sel = (secsw->switch_sel & ~UART_PATH_MASK) | UART_PATH_LTE;
		pr_info("[UART Switch] Path : LTEMODEM\n");
	}

	if (sec_set_param_value)
		sec_set_param_value(__SWITCH_SEL, &secsw->switch_sel);

	return size;
}

static ssize_t usb_sel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sec_switch_struct *secsw = dev_get_drvdata(dev);
	int usb_path = secsw->switch_sel & USB_PATH_MASK;
	char *chip_sel[] = {"MODEM", "PDA", "LTEMODEM"};
	return sprintf(buf, "%s USB\n", chip_sel[usb_path]);
}

static ssize_t usb_sel_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct sec_switch_struct *secsw = dev_get_drvdata(dev);
	int value;
	value = usb_cable;
	printk("%s\n",__func__);
	printk("%s the device type detected is %x \n ",__func__, value);
	if (sec_get_param_value)
		sec_get_param_value(__SWITCH_SEL, &secsw->switch_sel);

	if (strncmp(buf, "PDA", 3) == 0 || strncmp(buf, "pda", 3) == 0) {
		if (value == CABLE_CONNECTED)
		   usb_switch_mode(SWITCH_PDA);
		secsw->switch_sel = (secsw->switch_sel & ~USB_PATH_MASK) | USB_PATH_PDA;
		pr_info("[USB Switch] Path : PDA\n");
	}

	if (strncmp(buf, "MODEM", 5) == 0 || strncmp(buf, "modem", 5) == 0) {
		if (value == CABLE_CONNECTED)
		   usb_switch_mode(SWITCH_MODEM);
		secsw->switch_sel = (secsw->switch_sel & ~USB_PATH_MASK) | USB_PATH_MODEM;
		pr_info("[USB Switch] Path : MODEM\n");
	}

	if (strncmp(buf, "LTEMODEM", 3) == 0 || strncmp(buf, "ltemodem", 3) == 0) {
		if (value == CABLE_CONNECTED)
		   usb_switch_mode(SWITCH_LTE);
		secsw->switch_sel = (secsw->switch_sel & ~USB_PATH_MASK) | USB_PATH_LTE;
		pr_info("[USB Switch] Path : LTEMODEM\n");
	}

	if (sec_set_param_value)
		sec_set_param_value(__SWITCH_SEL, &secsw->switch_sel);

	return size;
}

static ssize_t usb_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sec_switch_struct *secsw = dev_get_drvdata(dev);
	int cable_state = CABLE_TYPE_NONE;

	if (secsw->pdata && secsw->pdata->get_cable_status)
		cable_state = secsw->pdata->get_cable_status();

	return sprintf(buf, "%s\n", (cable_state == CABLE_TYPE_USB) ?
			"USB_STATE_CONFIGURED" : "USB_STATE_NOTCONFIGURED");
}

static ssize_t usb_state_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t size)
{
	return 0;
}

static ssize_t disable_vbus_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t disable_vbus_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct sec_switch_struct *secsw = dev_get_drvdata(dev);

	if (IS_ERR_OR_NULL(secsw->pdata) ||
	    IS_ERR_OR_NULL(secsw->pdata->set_vbus_status) ||
	    IS_ERR_OR_NULL(secsw->pdata->set_usb_gadget_vbus))
		return size;

	secsw->pdata->set_usb_gadget_vbus(false);
	secsw->pdata->set_vbus_status((u8)USB_VBUS_ALL_OFF);
	msleep(10);
	secsw->pdata->set_usb_gadget_vbus(true);

	return size;
}

static ssize_t uart_debug_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int switch_sel = 0;

	if (sec_get_param_value)
		sec_get_param_value(__SWITCH_SEL, &switch_sel);

	return sprintf(buf, "%s\n", (switch_sel & UART_DEBUG_MASK) ? "ENABLE" : "DISABLE");
}

static ssize_t uart_debug_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct sec_switch_struct *secsw = dev_get_drvdata(dev);

	if (sec_get_param_value)
		sec_get_param_value(__SWITCH_SEL, &secsw->switch_sel);

	if (strncmp(buf, "DISABLE", 7) == 0 || strncmp(buf, "disable", 7) == 0) {
		pr_info("%s : uart debug disable\n", __func__);
		secsw->switch_sel &= ~UART_DEBUG_MASK;
	} else if (strncmp(buf, "ENABLE", 6) == 0 || strncmp(buf, "enable", 6) == 0) {
		pr_info("%s : uart debug enable\n", __func__);
		secsw->switch_sel |=  UART_DEBUG_MASK;
	}

	if (sec_set_param_value)
		sec_set_param_value(__SWITCH_SEL, &secsw->switch_sel);

	return size;
}
#if 0
static int determine_usb_and_store(struct device *dev, struct device_attribute *attr,
                                const char *buf, size_t size)
{

	int value;
	printk ("%s IN\n",__func__);
	
	value = cable_type;
	if (value == USB_CONNECTED)
		usb_sel_store(dev, attr, buf, size);
	
	printk("%s the device type detected is %x \n ",__func__, value);
	printk("%s OUT\n",__func__);
	return 0;	
}

static int determine_uart_and_store(struct device *dev, struct device_attribute *attr,
                                const char *buf, size_t size)
{

        int value;
        printk ("%s IN\n",__func__);

        value = cable_type;
        if (value == UART_CONNECTED)
                uart_sel_store(dev, attr, buf, size);

        printk("%s the device type detected is %x \n ",__func__, value);
        printk("%s OUT\n",__func__);
        return 0;
}
#endif

#ifdef CONFIG_MACH_CHIEF
// [ lte_factory_test_101203
char lte_factory_test = false;

static ssize_t LteFactoryTest_switch_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	printk("LteFactoryTest_switch_show lte_factory_test=%d\n", lte_factory_test);
	
	return sprintf(buf,"%d\n",lte_factory_test);
}

static ssize_t LteFactoryTest_switch_store(struct device *dev, struct device_attribute *attr,	const char *buf, size_t size)
{
	printk("LteFactoryTest_switch_store buf=%s\n", buf);
	
	struct sec_switch_struct *secsw = dev_get_drvdata(dev);
	
	if (strncmp(buf, "0", 1) == 0 )
	{
	lte_factory_test = false;
	}
	else
	{
	lte_factory_test = true;
	secsw->switch_sel = secsw->switch_sel  | UART_PATH_MTM;
	if (sec_set_param_value)
		sec_set_param_value(__SWITCH_SEL, &secsw->switch_sel);
	}
	
	return size;
}

static DEVICE_ATTR(LteFactoryTest, 
                 0664 /* S_IRUGO |S_IWUGO | S_IRUSR | S_IWUSR*/, 
                   LteFactoryTest_switch_show, 
                   LteFactoryTest_switch_store);
// lte_factory_test_101203 ]
#endif


static DEVICE_ATTR(uart_sel,     0664, uart_sel_show,     uart_sel_store);
static DEVICE_ATTR(usb_sel,      0664, usb_sel_show,      usb_sel_store);
static DEVICE_ATTR(usb_state,    0664, usb_state_show,    usb_state_store);
static DEVICE_ATTR(disable_vbus, 0664, disable_vbus_show, disable_vbus_store);
static DEVICE_ATTR(UartDebug,    0664, uart_debug_show,   uart_debug_store);

static void sec_switch_init_work(struct work_struct *work)
{
	struct delayed_work *dw = container_of(work, struct delayed_work, work);
	struct sec_switch_wq *wq = container_of(dw, struct sec_switch_wq, work_q);
	struct sec_switch_struct *secsw = wq->sdata;
	int usb_sel = 0;
	int uart_sel = 0;
#ifdef CONFIG_MACH_CHIEF
	char cmdline[12];
	char* pCmd = NULL;
#endif	

	if (sec_get_param_value &&
	    secsw->pdata &&
	    secsw->pdata->set_vbus_status &&
	    secsw->pdata->get_phy_init_status &&
	    secsw->pdata->get_phy_init_status()) {
		sec_get_param_value(__SWITCH_SEL, &secsw->switch_sel);
		cancel_delayed_work(&wq->work_q);
	} else {
		schedule_delayed_work(&wq->work_q, msecs_to_jiffies(1000));
		return;
	}

	pr_info("%s : initial sec switch value = 0x%X\n", __func__, secsw->switch_sel);
        if(secsw->switch_sel & UART_PATH_MTM){
	secsw->switch_sel = (secsw->switch_sel & ~UART_PATH_MASK) | UART_PATH_MODEM;
	secsw->switch_sel = (secsw->switch_sel & ~UART_PATH_MTM);
}
#ifdef CONFIG_MACH_CHIEF
	if ((secsw->switch_sel & UART_DEBUG_MASK) ||
#else
	if (!(secsw->switch_sel & UART_DEBUG_MASK) ||
#endif
	    ((secsw->switch_sel & UART_PATH_MASK) == UART_PATH_MASK))
		secsw->switch_sel = (secsw->switch_sel & ~UART_PATH_MASK) | UART_PATH_MODEM;

	if ((secsw->switch_sel & USB_PATH_MASK) == USB_PATH_MASK)
		secsw->switch_sel = (secsw->switch_sel & ~USB_PATH_MASK) | USB_PATH_PDA;
#ifdef CONFIG_MACH_CHIEF
// [ lte_factory_test_101203
	if(sec_get_param_value){
		sec_get_param_value(__PARAM_STR_4, cmdline);
		printk(KERN_ERR "[LTE_UART] PARAM_STR_4 : %s\n",cmdline);
		pCmd = strstr(cmdline, "LTE_FACTORY");
		if( pCmd ){            
			lte_factory_test = true;
			uart_sel = SWITCH_LTE;
			memset(cmdline, 0x00, 12);
			sec_set_param_value(__PARAM_STR_4, cmdline);
			printk(KERN_ERR "pCmd (LTE_FACTORY) : lte_factory_test is TRUE\n");
		}
	}
// lte_factory_test_101203 ]
#endif
	if (sec_set_param_value)
		sec_set_param_value(__SWITCH_SEL, &secsw->switch_sel);

	usb_sel = secsw->switch_sel & USB_PATH_MASK;
	uart_sel = (secsw->switch_sel & UART_PATH_MASK) >> 2;

	if (initial_path_sel == USB_PATH_MASK)
		usb_switch_mode(usb_sel);
	else if (initial_path_sel == UART_PATH_MASK)
		uart_switch_mode(uart_sel);

	finish_sec_switch_init = 1;
}

static int sec_switch_probe(struct platform_device *pdev)
{
	struct sec_switch_struct *secsw;
	struct sec_switch_platform_data *pdata = pdev->dev.platform_data;
	struct sec_switch_wq *wq;


	if (!pdata) {
		pr_err("%s : pdata is NULL.\n", __func__);
		return -ENODEV;
	}

	secsw = kzalloc(sizeof(struct sec_switch_struct), GFP_KERNEL);
	if (!secsw) {
		pr_err("%s : failed to allocate memory\n", __func__);
		return -ENOMEM;
	}
#ifdef CONFIG_MACH_CHIEF
         secsw_switch = kzalloc(sizeof(struct sec_switch_struct), GFP_KERNEL);
        if (!secsw_switch) {
                pr_err("%s : failed to allocate memory\n", __func__);
                return -ENOMEM;
        }
       secsw_switch->pdata =pdata;
#endif
	secsw->pdata = pdata;
	secsw->switch_sel = 1;

	dev_set_drvdata(switch_dev, secsw);

	/* create sysfs files */
	if (device_create_file(switch_dev, &dev_attr_uart_sel) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_uart_sel.attr.name);

	if (device_create_file(switch_dev, &dev_attr_usb_sel) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_usb_sel.attr.name);

	if (device_create_file(switch_dev, &dev_attr_usb_state) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_usb_state.attr.name);

	if (device_create_file(switch_dev, &dev_attr_disable_vbus) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_disable_vbus.attr.name);

	if (device_create_file(switch_dev, &dev_attr_UartDebug) < 0)
			pr_err("Failed to create device file(%s)!\n", dev_attr_UartDebug.attr.name);

#ifdef CONFIG_MACH_CHIEF
// [ lte_factory_test_101203
	if (device_create_file(switch_dev, &dev_attr_LteFactoryTest) < 0)
		pr_err(KERN_INFO "Failed to create device file(%s)!\n", dev_attr_LteFactoryTest.attr.name);
// lte_factory_test_101203 ]
#endif

	/* run work queue */
	wq = kmalloc(sizeof(struct sec_switch_wq), GFP_ATOMIC);
	if (wq) {
		wq->sdata = secsw;
		INIT_DELAYED_WORK(&wq->work_q, sec_switch_init_work);
		schedule_delayed_work(&wq->work_q, msecs_to_jiffies(100));
	} else
		return -ENOMEM;

	return 0;
}

static int sec_switch_remove(struct platform_device *pdev)
{
	struct sec_switch_struct *secsw = dev_get_drvdata(&pdev->dev);
	kfree(secsw);
	return 0;
}

static struct platform_driver sec_switch_driver = {
	.probe = sec_switch_probe,
	.remove = sec_switch_remove,
	.driver = {
			.name = "sec_switch",
			.owner = THIS_MODULE,
	},
};

static int __init sec_switch_init(void)
{
	return platform_driver_register(&sec_switch_driver);
}

static void __exit sec_switch_exit(void)
{
	platform_driver_unregister(&sec_switch_driver);
}

module_init(sec_switch_init);
module_exit(sec_switch_exit);

MODULE_AUTHOR("Ikkeun Kim <iks.kim@samsung.com>");
MODULE_DESCRIPTION("Samsung Electronics Corp Switch driver");
MODULE_LICENSE("GPL");
