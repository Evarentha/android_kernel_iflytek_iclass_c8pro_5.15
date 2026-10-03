// SPDX-License-Identifier: GPL-2.0-only
/* Contiguous shared-log ABI used by the ROC1 factory Trusty firmware. */
#include <linux/log2.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/panic_notifier.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/trusty/smcall.h>

#include "trusty.h"
#include "trusty-log.h"

#define LEGACY_LOG_SIZE (4 * PAGE_SIZE)
#define LEGACY_LINE_SIZE 256

struct trusty_legacy_log {
	struct device *dev;
	struct page *pages;
	struct log_rb *ring;
	u32 get;
	spinlock_t lock;
	struct notifier_block call_nb;
	struct notifier_block panic_nb;
	char line[LEGACY_LINE_SIZE];
};

static int trusty_legacy_log_smc(struct trusty_legacy_log *s, u32 call)
{
	phys_addr_t pa = page_to_phys(s->pages);

	/* The factory ABI takes a buffer address and bytes, not a PFN list. */
	return trusty_std_call32(s->dev->parent, call, lower_32_bits(pa),
				upper_32_bits(pa),
				call == SMC_SC_SHARED_LOG_ADD ? LEGACY_LOG_SIZE : 0);
}

static void trusty_legacy_log_dump(struct trusty_legacy_log *s)
{
	struct log_rb *ring = s->ring;
	u32 size = READ_ONCE(ring->sz);
	u32 get = s->get, put, alloc;
	size_t count, i;

	if (!is_power_of_2(size) || size > LEGACY_LOG_SIZE - sizeof(*ring)) {
		dev_err_ratelimited(s->dev, "Invalid shared log ring size %u\n", size);
		return;
	}

	while ((put = READ_ONCE(ring->put)) != get) {
		rmb();
		count = min_t(u32, put - get, sizeof(s->line) - 1);
		for (i = 0; i < count; i++) {
			s->line[i] = ring->data[(get + i) & (size - 1)];
			if (s->line[i] == '\n') {
				i++;
				break;
			}
		}
		s->line[i] = '\0';
		rmb();
		alloc = READ_ONCE(ring->alloc);
		if (alloc - get > size) {
			get = alloc - size;
			continue;
		}
		pr_info("trusty: %s", s->line);
		get += i;
	}
	s->get = get;
}

static int trusty_legacy_log_notify(struct notifier_block *nb,
				    unsigned long action, void *data)
{
	struct trusty_legacy_log *s = container_of(nb, struct trusty_legacy_log, call_nb);
	unsigned long flags;

	if (action != TRUSTY_CALL_RETURNED)
		return NOTIFY_DONE;
	spin_lock_irqsave(&s->lock, flags);
	trusty_legacy_log_dump(s);
	spin_unlock_irqrestore(&s->lock, flags);
	return NOTIFY_OK;
}

static int trusty_legacy_log_panic(struct notifier_block *nb,
				   unsigned long action, void *data)
{
	struct trusty_legacy_log *s = container_of(nb, struct trusty_legacy_log, panic_nb);

	/* As in the factory driver, do not wait on a lock held by a stopped CPU. */
	trusty_legacy_log_dump(s);
	return NOTIFY_OK;
}

static int trusty_legacy_log_probe(struct platform_device *pdev)
{
	struct trusty_legacy_log *s;
	int ret;

	ret = trusty_std_call32(pdev->dev.parent, SMC_SC_SHARED_LOG_VERSION,
				TRUSTY_LOG_API_VERSION, 0, 0);
	if (ret != TRUSTY_LOG_API_VERSION)
		return dev_err_probe(&pdev->dev, -ENXIO,
				     "Unsupported shared log version %d\n", ret);

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	s->dev = &pdev->dev;
	spin_lock_init(&s->lock);
	s->pages = alloc_pages(GFP_KERNEL | __GFP_ZERO, get_order(LEGACY_LOG_SIZE));
	if (!s->pages) {
		ret = -ENOMEM;
		goto free_state;
	}
	s->ring = page_address(s->pages);
	ret = trusty_legacy_log_smc(s, SMC_SC_SHARED_LOG_ADD);
	if (ret < 0)
		goto free_pages;

	s->call_nb.notifier_call = trusty_legacy_log_notify;
	ret = trusty_call_notifier_register(pdev->dev.parent, &s->call_nb);
	if (ret)
		goto unregister_buffer;
	s->panic_nb.notifier_call = trusty_legacy_log_panic;
	ret = atomic_notifier_chain_register(&panic_notifier_list, &s->panic_nb);
	if (ret)
		goto unregister_call;

	platform_set_drvdata(pdev, s);
	dev_info(&pdev->dev, "Registered factory contiguous log buffer (%lu bytes)\n",
		 LEGACY_LOG_SIZE);
	return 0;

unregister_call:
	trusty_call_notifier_unregister(pdev->dev.parent, &s->call_nb);
unregister_buffer:
	if (trusty_legacy_log_smc(s, SMC_SC_SHARED_LOG_RM)) {
		/* Secure firmware may still write here: retain the allocation. */
		dev_err(&pdev->dev, "Unable to revoke shared log buffer\n");
		goto free_state;
	}
free_pages:
	__free_pages(s->pages, get_order(LEGACY_LOG_SIZE));
free_state:
	kfree(s);
	return ret;
}

static int trusty_legacy_log_remove(struct platform_device *pdev)
{
	struct trusty_legacy_log *s = platform_get_drvdata(pdev);

	atomic_notifier_chain_unregister(&panic_notifier_list, &s->panic_nb);
	trusty_call_notifier_unregister(pdev->dev.parent, &s->call_nb);
	if (!trusty_legacy_log_smc(s, SMC_SC_SHARED_LOG_RM))
		__free_pages(s->pages, get_order(LEGACY_LOG_SIZE));
	else
		dev_err(&pdev->dev, "Unable to revoke shared log buffer\n");
	kfree(s);
	return 0;
}

static const struct of_device_id trusty_legacy_log_match[] = {
	{ .compatible = "sprd,roc1-trusty-log-v1" },
	{ }
};
MODULE_DEVICE_TABLE(of, trusty_legacy_log_match);

static struct platform_driver trusty_legacy_log_driver = {
	.probe = trusty_legacy_log_probe,
	.remove = trusty_legacy_log_remove,
	.driver = {
		.name = "sprd-trusty-log-legacy",
		.of_match_table = trusty_legacy_log_match,
	},
};
module_platform_driver(trusty_legacy_log_driver);
MODULE_DESCRIPTION("ROC1 factory Trusty contiguous shared log");
MODULE_LICENSE("GPL v2");
