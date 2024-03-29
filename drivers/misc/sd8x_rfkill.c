/*
 * rfkill power contorl for Marvell sd8xxx wlan/bt
 *
 * Copyright (C) 2009 Marvell, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mmc/host.h>
#include <linux/sd8x_rfkill.h>
#include <linux/platform_data/pxa_sdhci.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/mmc/sdhci.h>
#include <linux/clk.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/edge_wakeup_mmp.h>


#include <mach/gpio.h>
#define SD8X_DEV_NAME "sd8x-rfkill"

struct sd8x_rfkill_data {
	enum rfkill_type type;
	bool blocked;
	struct sd8x_rfkill_platform_data *pdata;
};
static struct sd8x_rfkill_data *local_sd8x_data;

static struct wakeup_source wlan_dat1_wakeup;
static struct work_struct wlan_wk;

static void wlan_edge_wakeup(struct work_struct *work)
{
	/*
	 * it is handled in SDIO driver instead, so code not need now
	 * but temparally keep the code here,it may be used for debug
	 */
#if 0
	unsigned int sec = 3;

	/*
	 * Why use a workqueue to call this function?
	 *
	 * As now dat1_edge_wakeup is called just after CPU exist LPM,
	 * and if __pm_wakeup_event is called before syscore_resume,
	 * WARN_ON(timekeeping_suspended) will happen in ktime_get in
	 * /kernel/time/timekeeping.c
	 *
	 * So we create a workqueue to fix this issue
	 */
	__pm_wakeup_event(&wlan_dat1_wakeup, 1000 * sec);
	pr_info("SDIO pade edge wake up+++\n");
#endif
}

static void dat1_edge_wakeup(int irq, void *p_rsv)
{
	queue_work(system_wq, &wlan_wk);
}

static void wlan_wakeup_init(void)
{
	 INIT_WORK(&wlan_wk, wlan_edge_wakeup);
	 wakeup_source_init(&wlan_dat1_wakeup,
		"wifi_hs_wakeups");
}

int add_sd8x_rfkill_device(int gpio_power_down, int gpio_reset,
			   struct mmc_host ***pmmc,
			   rfkill_plat_set_power set_power)
{
	int ret;
	struct platform_device *pdev = NULL;
	struct sd8x_rfkill_platform_data *pdata = NULL;

	pdata = kzalloc(sizeof(struct sd8x_rfkill_platform_data), GFP_KERNEL);
	if (!pdata) {
		printk(KERN_CRIT "no memory\n");
		goto err_out;
	}
	pdata->gpio_power_down = gpio_power_down;
	pdata->gpio_reset = gpio_reset;

	pdev = kzalloc(sizeof(struct platform_device), GFP_KERNEL);
	if (!pdev) {
		printk(KERN_CRIT "no memory\n");
		goto err_out;
	}
	pdev->name = SD8X_DEV_NAME;
	pdev->id = -1, pdev->dev.platform_data = pdata;

	ret = platform_device_register(pdev);
	if (ret) {
		dev_err(&pdev->dev, "unable to register device: %d\n", ret);
		goto err_out;
	}
	*pmmc = &(pdata->mmc);
	pdata->set_power = set_power;
	return 0;

err_out:
	kfree(pdata);
	kfree(pdev);
	pr_debug("%s: error\n", __func__);
	return -1;
}
EXPORT_SYMBOL(add_sd8x_rfkill_device);

static int sd8x_power_on(struct sd8x_rfkill_platform_data *pdata, int on)
{
	int gpio_power_down = pdata->gpio_power_down;
	int gpio_reset = pdata->gpio_reset;

	pr_debug("%s: on=%d\n", __func__, on);
	if (gpio_power_down
	    && gpio_request(gpio_power_down, "sd8xxx power down")) {
		printk(KERN_INFO "gpio %d request failed\n", gpio_power_down);
		return -1;
	}

	if (gpio_reset && gpio_request(gpio_reset, "sd8xxx reset")) {
		printk(KERN_INFO "gpio %d request failed\n", gpio_reset);
		gpio_free(gpio_power_down);
		return -1;
	}

	if (on) {
		if (gpio_power_down) {
			msleep(1);
			gpio_direction_output(gpio_power_down, 1);
		}

		if (gpio_reset) {
			gpio_direction_output(gpio_reset, 0);
			msleep(1);
			gpio_direction_output(gpio_reset, 1);
		}
	} else {
		if (gpio_power_down)
			gpio_direction_output(gpio_power_down, 0);
	}

	if (gpio_power_down)
		gpio_free(gpio_power_down);
	if (gpio_reset)
		gpio_free(gpio_reset);
	return 0;
}

unsigned long mmc_detect_change_sync(struct mmc_host *host,
					    unsigned long delay,
					    unsigned long timeout);
static int sd8x_set_block(void *data, bool blocked)
{
	bool pre_blocked;
	int ret = 0;
	int on = 0;
	struct sd8x_rfkill_data *sd8x_data = (struct sd8x_rfkill_data *)data;
	struct sd8x_rfkill_platform_data *pdata = sd8x_data->pdata;
	struct sdhci_host *host = mmc_priv(pdata->mmc);

	if (!pdata->wlan_rfkill) {
		if (sd8x_data->pdata->set_power)
			sd8x_data->pdata->set_power(0);
		ret = sd8x_power_on(sd8x_data->pdata, 0);
		return 0;
	}

	pre_blocked = sd8x_data->blocked;

	pr_debug
	    ("%s: try to set block state of type(%d) as %d, pre_blocked=%d\n",
	     __func__, sd8x_data->type, blocked, pre_blocked);
	if (!blocked && pre_blocked)
		on = 1;
	else if (blocked && !pre_blocked)
		on = 0;
	else
		return 0;

	printk(KERN_INFO "sd8x set_power %s start\n", on ? "on" : "off");

	/*
	 * As we known, if SD8787 is working, we need to keep SDIO Host's
	 * Source CLK free running.
	 *
	 * But if PM Runtime is enabled, the host's RPM callback may dynamic
	 * on/off the SRC CLK
	 *
	 * Here we call "pm_runtime_get" to make sure Source Clock will not
	 * be disabled untill pm_runtime_put is called again in this function
	 */
	if (on)
		pm_runtime_get_noresume(host->mmc->parent);

	if (pdata->set_power)
		pdata->set_power(on);
	ret = sd8x_power_on(pdata, on);

	if (ret)
		goto out;

	if (pdata->mmc) {
		int retry = on ? 5 : 1;
		unsigned long timeout_secs = on ? 2 : 1;
		unsigned long timeout = msecs_to_jiffies(timeout_secs * 1000);

		while (retry) {
			if (0 == mmc_detect_change_sync(pdata->mmc,
							msecs_to_jiffies(10),
							timeout)) {
				printk(KERN_WARNING
				       "mmc detect has taken %u ms, "
				       "something wrong\n",
				       jiffies_to_msecs(timeout));
			}
			if ((on && (pdata->mmc->card))
			    || (!on && !(pdata->mmc->card)))
				break;

			printk(KERN_INFO "Retry mmc detection\n");

			retry--;
			ret = sd8x_power_on(pdata, on);
			if (ret)
				goto out;
		}
		if (!retry && on) {
			ret = -1;
			printk(KERN_INFO "rfkill fails to wait mmc device %s\n",
			       on ? "up" : "down");
		}
	} else
		printk(KERN_DEBUG "rfkill is not linked with mmc_host\n");

out:
	if (!ret) {
		sd8x_data->blocked = blocked;

		if (pdata->gpio_edge_wakeup >= 0) {
			if (on)
				request_mfp_edge_wakeup(pdata->gpio_edge_wakeup,
					dat1_edge_wakeup, NULL,
					pdata->mmc->parent);
			else
				remove_mfp_edge_wakeup(pdata->gpio_edge_wakeup);
		}

		/* put_sync here to balance RPM ref */
		if (!on)
			pm_runtime_put_sync(host->mmc->parent);
	}
	else if (on) {
		if (pdata->set_power)
			pdata->set_power(0);
		sd8x_power_on(pdata, 0);
		/* put_sync here to balance RPM ref */
		pm_runtime_put_sync(host->mmc->parent);
	}
	printk(KERN_INFO "sd8x set_power %s end\n", on ? "on" : "off");

	return ret;
}

static struct rfkill *sd8x_rfkill_register(struct device *parent,
					   enum rfkill_type type, char *name,
					   struct sd8x_rfkill_platform_data
					   *pdata)
{
	int err;
	struct rfkill *dev = NULL;
	struct rfkill_ops *ops = NULL;

	ops = kzalloc(sizeof(struct rfkill_ops), GFP_KERNEL);
	if (!ops)
		goto err_out;
	ops->set_block = sd8x_set_block;

	local_sd8x_data->type = type;
	local_sd8x_data->blocked = true;
	local_sd8x_data->pdata = pdata;

	dev = rfkill_alloc(name, parent, type, ops, local_sd8x_data);
	if (!dev)
		goto err_out;

	/* init device software states, and block it by default */
	rfkill_init_sw_state(dev, true);

	err = rfkill_register(dev);
	if (err)
		goto err_out;

	return dev;

err_out:
	kfree(ops);
	if (dev)
		rfkill_destroy(dev);
	return 0;
}

static void sd8x_rfkill_free(struct sd8x_rfkill_platform_data *pdata)
{
	if (pdata->wlan_rfkill) {
		rfkill_unregister(pdata->wlan_rfkill);
		rfkill_destroy(pdata->wlan_rfkill);
	}

	kfree(local_sd8x_data);
}

#ifdef CONFIG_OF
static const struct of_device_id sd8x_rfkill_of_match[] = {
	{
		.compatible = "mrvl,sd8x-rfkill",
	},
	{},
};

MODULE_DEVICE_TABLE(of, sd8x_rfkill_of_match);

static int sd8x_rfkill_probe_dt(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node, *sdh_np;
	struct sd8x_rfkill_platform_data *pdata = pdev->dev.platform_data;
	struct platform_device *sdh_pdev;
	struct sdhci_host *host;
	int sdh_phandle, gpio;

	if (of_property_read_u32(np, "sd-host", &sdh_phandle)) {
		dev_err(&pdev->dev, "failed to find sd-host in dt\n");
		return -1;
	}

	/* we've got the phandle for sdh */
	sdh_np = of_find_node_by_phandle(sdh_phandle);
	if (unlikely(IS_ERR(sdh_np))) {
		dev_err(&pdev->dev, "failed to find device_node for sdh\n");
		return -1;
	}

	sdh_pdev = of_find_device_by_node(sdh_np);
	if (unlikely(IS_ERR(sdh_pdev))) {
		dev_err(&pdev->dev, "failed to find platform_device for sdh\n");
		return -1;
	}

	/* sdh_pdev->dev->driver_data was set as sdhci_host in sdhci driver */
	host = platform_get_drvdata(sdh_pdev);

	/*
	 * If we cannot find host, it's because sdh device is not registered
	 * yet. Probe again later.
	 */
	if (!host)
		return -EPROBE_DEFER;

	pdata->mmc = host->mmc;

	/* get gpios from dt */
	gpio = of_get_named_gpio(np, "edge-wakeup-gpio", 0);
	if (unlikely(gpio < 0)) {
		dev_warn(&pdev->dev, "edge-wakeup-gpio undefined\n");
		pdata->gpio_edge_wakeup = -1;
	} else {
		pdata->gpio_edge_wakeup = gpio;
	}

	gpio = of_get_named_gpio(np, "pd-gpio", 0);
	if (unlikely(gpio < 0)) {
		dev_err(&pdev->dev, "pd-gpio undefined\n");
		return -1;
	} else {
		pdata->gpio_power_down = gpio;
	}

	gpio = of_get_named_gpio(np, "rst-gpio", 0);
	if (unlikely(gpio < 0)) {
		dev_err(&pdev->dev, "rst-gpio undefined\n");
		return -1;
	} else {
		pdata->gpio_reset = gpio;
	}

	gpio = of_get_named_gpio(np, "hostwu-gpio", 0);
	if (unlikely(gpio < 0)) {
		dev_err(&pdev->dev, "hostwu-gpio undefined on dts : ignored\n");
	} else {
		if(!gpio_request(gpio, "WIB_HOSTWU")) {
			gpio_direction_output(gpio, 0);
			gpio_free(gpio);
		} else {
			dev_err(&pdev->dev, "hostwu-gpio request failed : ignored\n");
		}
	}

	return 0;
}

#else
static int sd8x_rfkill_probe_dt(struct platform_device *pdev)
{
	return 0;
}

#endif

static int sd8x_rfkill_probe(struct platform_device *pdev)
{
	struct rfkill *rfkill = NULL;
	struct sd8x_rfkill_platform_data *pdata = pdev->dev.platform_data;
	const struct of_device_id *match;
	struct sd8x_rfkill_data *data = NULL;
	int ret;

	if (NULL == pdata) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			goto err_out2;
		pdev->dev.platform_data = pdata;
	}

	match = of_match_device(of_match_ptr(sd8x_rfkill_of_match),
				&pdev->dev);
	if (match) {
		ret = sd8x_rfkill_probe_dt(pdev);
		if (ret)
			return ret;
	}

	if (pdata->gpio_power_down &&
			(!gpio_request(pdata->gpio_power_down, "WIB_PDn"))) {
		gpio_direction_output(pdata->gpio_power_down, 0);
		gpio_free(pdata->gpio_power_down);
	}

	if ((pdata->gpio_reset) &&
			(!gpio_request(pdata->gpio_reset, "WIB_RSTn"))) {
		gpio_direction_output(pdata->gpio_reset, 0);
		gpio_free(pdata->gpio_reset);
	}

	data = kzalloc(sizeof(struct sd8x_rfkill_data), GFP_KERNEL);
	if (!data)
		goto err_out;
	local_sd8x_data = data;

	rfkill = sd8x_rfkill_register(&pdev->dev,
				      RFKILL_TYPE_WLAN, "sd8xxx-wlan", pdata);
	if (IS_ERR(rfkill))
		goto err_out;
	pdata->wlan_rfkill = rfkill;

	wlan_wakeup_init();

	return 0;

err_out:
	sd8x_rfkill_free(pdata);
err_out2:
	return -1;
}

static int sd8x_rfkill_remove(struct platform_device *pdev)
{
	struct sd8x_rfkill_platform_data *pdata = pdev->dev.platform_data;

	sd8x_rfkill_free(pdata);

	return 0;
}

static int sd8x_rfkill_suspend(struct platform_device *pdev,
			       pm_message_t pm_state)
{
	return 0;
}

static int sd8x_rfkill_resume(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver sd8x_rfkill_platform_driver = {
	.probe = sd8x_rfkill_probe,
	.remove = sd8x_rfkill_remove,
	.driver = {
		   .name = SD8X_DEV_NAME,
		   .owner = THIS_MODULE,
#ifdef CONFIG_OF
		   .of_match_table = sd8x_rfkill_of_match,
#endif

		   },
	.suspend = sd8x_rfkill_suspend,
	.resume = sd8x_rfkill_resume,
};

static int __init sd8x_rfkill_init(void)
{
	return platform_driver_register(&sd8x_rfkill_platform_driver);
}

static void __exit sd8x_rfkill_exit(void)
{
	platform_driver_unregister(&sd8x_rfkill_platform_driver);
}

module_init(sd8x_rfkill_init);
module_exit(sd8x_rfkill_exit);

MODULE_ALIAS("platform:sd8x_rfkill");
MODULE_DESCRIPTION("sd8x_rfkill");
MODULE_AUTHOR("Marvell");
MODULE_LICENSE("GPL");
