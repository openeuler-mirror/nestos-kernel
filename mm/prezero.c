// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/mm_inline.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>

#include <linux/prezero.h>
#include "internal.h"

DEFINE_STATIC_KEY_FALSE(prezero_enabled_key);
unsigned long prezero_enabled_flag;
static unsigned int prezero_min_order = 9;
static unsigned int prezero_max_percent = 50;
static unsigned int prezero_batch_pages = 4096;
static unsigned int prezero_sleep_msecs = 1000;
static struct task_struct *prezero_kthread[MAX_NUMNODES];
static wait_queue_head_t kprezerod_wait[MAX_NUMNODES];
static unsigned long kprezerod_sleep_expire[MAX_NUMNODES];

static DEFINE_STATIC_KEY_FALSE(prezero_hw_enabled_key);
static bool prezero_hw_flag_cc;
static bool prezero_hw_polling;
static inline bool prezero_hw_enabled(void)
{
	return static_branch_unlikely(&prezero_hw_enabled_key);
}
static int clear_page_hw(struct page *page, int order, int node);

static void my_clear_page(struct page *page, unsigned int order, int node)
{
	int i, numpages = 1 << order;

	if (prezero_hw_enabled() &&
	    !clear_page_hw(page, order, node)) {
		count_vm_event(PREZERO_HW_CLEAR);
		__count_vm_events(PREZERO_HW_CLEAR_PAGES, numpages);
		return;
	}

	for (i = 0; i < numpages; i++)
		clear_highpage(page + i);
}

static int prezero_one_page(struct zone *zone, unsigned int order, int mtype)
{
	struct free_area *area = &zone->free_area[order];
	struct list_head *list = &area->free_list[mtype];
	struct page *page_to_zero = NULL, *page, *next;
	int err = -ENOMEM;

	/*
	 * Perform early check, if free area is empty there is
	 * nothing to process so we can skip this free_list.
	 */
	if (list_empty(list))
		return err;

	/* Isolate a non-zeroed page */
	spin_lock_irq(&zone->lock);
	list_for_each_entry_safe(page, next, list, lru) {
		/* We are going to skip over the pre-zeroed pages. */
		if (PageZeroed(page))
			continue;

		if (__isolate_free_page(page, order))
			page_to_zero = page;
		else
			next = page;

		/*
		 * Make the next page in the free list the new head
		 * of the free list before we release the zone lock.
		 */
		if (&next->lru != list && !list_is_first(&next->lru, list))
			list_rotate_to_front(&next->lru, list);

		break;
	}
	spin_unlock_irq(&zone->lock);

	/* Failed to isolate non-zeroed page */
	if (!page_to_zero)
		return err;

	/* Clear the page */
	my_clear_page(page, order, zone_to_nid(zone));

	/* Putback the pre-zeroed page */
	spin_lock_irq(&zone->lock);
	mtype = get_pageblock_migratetype(page);
	__putback_isolated_page(page, order, mtype);

	/*
	 * If page was not comingled with another page we can consider
	 * the page to be zeroed since the page hasn't been modified,
	 * otherwise we will need to discard the zeroed state of this page.
	 */
	if (PageBuddy(page) && buddy_order(page) == order) {
		__SetPageZeroed(page);
		zone->free_area[order].nr_zeroed++;
		__mod_zone_page_state(zone, NR_ZEROED_PAGES, 1 << order);
	}

	spin_unlock_irq(&zone->lock);

	return err;
}

static void prezero_do_work(pg_data_t *pgdat)
{
	struct zone *zone = &pgdat->node_zones[ZONE_NORMAL];
	/* NOTE only MIGRATE_MOVABLE is supported currently */
	int mtype = MIGRATE_MOVABLE;
	unsigned int order;
	unsigned long nr_free, nr_zeroed;
	unsigned int nr_done;

	for (order = prezero_min_order; order < MAX_ORDER; order++) {
		/*
		 * Use data_race to avoid KCSAN warning since access
		 * to nr_free and nr_zeroed is lockless here.
		 *
		 * Since only MIGRATE_MOVABLE is supported at present,
		 * to set prezero_max_percent too high could prevent
		 * kprezerod from early bailing out.
		 */
		nr_free = data_race(zone->free_area[order].nr_free);
		/* Ditto. */
		nr_zeroed = data_race(zone->free_area[order].nr_zeroed);

		if (nr_zeroed >= nr_free * prezero_max_percent / 100)
			continue;

		nr_done = 0;
		while (nr_done < prezero_batch_pages) {
			if (prezero_one_page(zone, order, mtype) < 0)
				break;
			nr_done += 1 << order;
		}
	}
}

static bool kprezerod_should_wakeup(int nid)
{
	return kthread_should_stop() ||
	       time_after_eq(jiffies, kprezerod_sleep_expire[nid]);
}

static int prezero(void *data)
{
	pg_data_t *pgdat = (pg_data_t *)data;
	int nid = pgdat->node_id;

	set_freezable();

	while (!kthread_should_stop()) {
		unsigned long sleep_jiffies =
			msecs_to_jiffies(prezero_sleep_msecs);

		kprezerod_sleep_expire[nid] = jiffies + sleep_jiffies;
		if (wait_event_freezable_timeout(kprezerod_wait[nid],
						 kprezerod_should_wakeup(nid),
						 sleep_jiffies))
			prezero_do_work(pgdat);
	}

	return 0;
}

static void __start_stop_kprezerod(int nid)
{
	if (prezero_enabled()) {
		if (!prezero_kthread[nid])
			prezero_kthread[nid] = kthread_run(prezero,
					NODE_DATA(nid), "kprezerod%d", nid);
		if (IS_ERR(prezero_kthread[nid])) {
			pr_err("failed to run kprezerod on node %d\n", nid);
			prezero_kthread[nid] = NULL;
		}
	} else if (prezero_kthread[nid]) {
		kthread_stop(prezero_kthread[nid]);
		prezero_kthread[nid] = NULL;
	}
}

static void start_stop_kprezerod(void)
{
	int nid;

	for_each_node_state(nid, N_MEMORY)
		__start_stop_kprezerod(nid);
}

/*
 * Page clear engine support - hardware offloading for page clear.
 *
 * Page clear engine allows to use a DMA device through the dmaengine API
 * to clear (zero) page asynchronously.
 *
 * User may configure the DMA device on each NUMA node before enabling this
 * feature.
 */
#define DMA_TIMEOUT	5000
static DEFINE_MUTEX(nodedata_mutex);
static struct nodedata {
	struct dma_chan *dma_chan;
} *nodedata;

static void dma_completion_callback(void *arg)
{
	struct completion *done = arg;

	complete(done);
}

/*
 * DMA engine APIs are called to prepare and submit DMA descriptors, and to
 * check completion status. The dest_addr of descriptor is filled with the DMA
 * mapped address of the page to be cleared.
 */
static int clear_page_hw(struct page *page, int order, int node)
{
	struct dma_chan *dma_chan = NULL;
	struct device *dev;
	struct dma_async_tx_descriptor *tx = NULL;
	dma_addr_t dst_dma;
	dma_cookie_t cookie;
	enum dma_status status;
	unsigned long dma_flags = 0;
	bool hw_flag_cc = prezero_hw_flag_cc;
	bool hw_polling = prezero_hw_polling;
	int ret = 0;
	DECLARE_COMPLETION_ONSTACK(done);

	mutex_lock(&nodedata_mutex);
	/* Page clear engine is already disabled */
	if (!nodedata) {
		ret = -ENODEV;
		goto err_nodedata;
	}

	dma_chan = nodedata[node].dma_chan;
	dev = dma_chan->device->dev;

	/* DMA map page */
	dst_dma = dma_map_page(dev, page, 0, PAGE_SIZE << order,
			       DMA_FROM_DEVICE);
	ret = dma_mapping_error(dev, dst_dma);
	if (ret)
		goto err_nodedata;

	if (!hw_flag_cc)
		dma_flags |= DMA_PREP_NONTEMPORAL;

	if (!hw_polling)
		dma_flags |= DMA_PREP_INTERRUPT;

	/* Prep DMA memset */
	tx = dmaengine_prep_dma_memset(dma_chan, dst_dma, 0,
				       PAGE_SIZE << order, dma_flags);
	if (!tx) {
		pr_info("Failed to prep DMA memset on node %d\n", node);
		ret = -EIO;
		goto err_prep;
	}

	if (!hw_polling) {
		tx->callback = dma_completion_callback;
		tx->callback_param = &done;
	}

	/* Submit DMA descriptor */
	cookie = dmaengine_submit(tx);
	if (dma_submit_error(cookie)) {
		pr_info("Failed to submit DMA descriptor on node %d\n", node);
		ret = -EIO;
		goto err_prep;
	}

	if (hw_polling) {
		/* Check DMA completion status with polling */
		status = dma_sync_wait(dma_chan, cookie);
		if (status != DMA_COMPLETE) {
			pr_info("Failed to poll DMA completion status on node %d\n", node);
			ret = -EIO;
		}
	} else {
		dma_async_issue_pending(dma_chan);
		if (!wait_for_completion_timeout(&done,
					msecs_to_jiffies(DMA_TIMEOUT))) {
			ret = -EIO;
			goto err_prep;
		}
		status = dma_async_is_tx_complete(dma_chan, cookie,NULL,NULL);
		if (status != DMA_COMPLETE) {
			pr_info("Failed to check DMA completion status on node %d\n", node);
			ret = -EIO;
		}
	}

err_prep:
	dma_unmap_page(dev, dst_dma, PAGE_SIZE << order, DMA_FROM_DEVICE);
err_nodedata:
	mutex_unlock(&nodedata_mutex);
	return ret;
}

static bool engine_filter_fn(struct dma_chan *chan, void *node)
{
	return dev_to_node(&chan->dev->device) == (int)(unsigned long)node;
}

/*
 * It initially requests a DMA channel with DMA_MEMSET capability on each NUMA
 * node and uses the DMA device to clear high order pages.
 *
 * The preference is to request the DMA channel from local NUMA node. If it is
 * not available, try again to request the DMA channel from any NUMA node.
 */
static int get_dma_chan(int node)
{
	dma_cap_mask_t mask;

	/* Request DMA channel by mask */
	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMSET, mask);

	/* Prefer to request DMA channel from local NUMA node if available */
	nodedata[node].dma_chan = dma_request_channel(mask, engine_filter_fn,
						(void *)(unsigned long)node);
	if (!nodedata[node].dma_chan) {
		/* Try again to request the DMA channel from any NUMA node */
		nodedata[node].dma_chan = dma_request_chan_by_mask(&mask);
		if (IS_ERR(nodedata[node].dma_chan)) {
			pr_info("Failed to request DMA channel on node %d\n", node);
			nodedata[node].dma_chan = NULL;
			return -ENODEV;
		}
	}

	return 0;
}

static int init_page_clear_engine(void)
{
	int node, num_nodes;
	int ret;

	/* Page clear engine is already enabled */
	if (nodedata)
		return 0;

	num_nodes = num_online_nodes();
	nodedata = kcalloc(num_nodes, sizeof(*nodedata), GFP_KERNEL);
	if (!nodedata)
		return -ENOMEM;

	for_each_online_node(node) {
		ret = get_dma_chan(node);
		if (ret)
			goto fail;
	}

	pr_info("Hardware page clear engine is enabled\n");
	return 0;

fail:
	for (node = 0; node < num_nodes; node++) {
		if (nodedata[node].dma_chan)
			dma_release_channel(nodedata[node].dma_chan);
	}

	kfree(nodedata);
	nodedata = NULL;

	return ret;
}

static void exit_page_clear_engine(void)
{
	int node;

	/* Page clear engine is already disabled */
	if (!nodedata)
		return;

	mutex_lock(&nodedata_mutex);
	for_each_online_node(node) {
		dma_release_channel(nodedata[node].dma_chan);
	}

	kfree(nodedata);
	nodedata = NULL;
	mutex_unlock(&nodedata_mutex);

	pr_info("Hardware page clear engine is disabled\n");
}

static int __init setup_prezero(char *str)
{
	unsigned long val;
	int err;

	if (!str)
		return 0;

	err = kstrtoul(str, 0, &val);
	if (err < 0 || val > (1UL << PREZERO_MAX_FLAG) - 1)
		return 0;

	prezero_enabled_flag = val;

	return 1;
}
__setup("prezero=", setup_prezero);

#ifdef CONFIG_SYSFS
static ssize_t prezero_show_enabled(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", prezero_enabled_flag);
}
static ssize_t prezero_store_enabled(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	static DEFINE_MUTEX(mutex);
	unsigned long val;
	int err;
	ssize_t ret = count;

	mutex_lock(&mutex);

	err = kstrtoul(buf, 0, &val);
	if (err < 0 || val > (1UL << PREZERO_MAX_FLAG) - 1) {
		ret = -EINVAL;
		goto out;
	}

	prezero_enabled_flag = val;

	if (prezero_enabled_flag)
		static_branch_enable(&prezero_enabled_key);
	else
		static_branch_disable(&prezero_enabled_key);

	start_stop_kprezerod();

out:
	mutex_unlock(&mutex);
	return ret;
}
static struct kobj_attribute prezero_attr_enabled =
	__ATTR(enabled, 0644, prezero_show_enabled,
	       prezero_store_enabled);

#define PREZERO_SYSFS_ATTR(name, field, min_val, max_val, store_cb)	\
static ssize_t prezero_show_##name(struct kobject *kobj,		\
	struct kobj_attribute *attr, char *buf)				\
{									\
	return sprintf(buf, "%u\n", field);				\
}									\
static ssize_t prezero_store_##name(struct kobject *kobj,		\
	struct kobj_attribute *attr, const char *buf, size_t count)	\
{									\
	unsigned long val;						\
	int ret;							\
									\
	ret = kstrtoul(buf, 0, &val);					\
	if (ret || val < min_val || val > max_val)			\
		return -EINVAL;						\
									\
	field = val;							\
	store_cb();							\
	return count;							\
}									\
static struct kobj_attribute prezero_attr_##name =			\
	__ATTR(name, 0644, prezero_show_##name, prezero_store_##name)

static void dummy_store_cb(void)
{
}

static void prezero_sleep_msecs_store_cb(void)
{
	int nid;

	for_each_node_state(nid, N_MEMORY) {
		kprezerod_sleep_expire[nid] = 0;
		wake_up_interruptible(&kprezerod_wait[nid]);
	}
}

PREZERO_SYSFS_ATTR(min_order, prezero_min_order, 0, MAX_ORDER - 1,
		   dummy_store_cb);
PREZERO_SYSFS_ATTR(max_percent, prezero_max_percent, 0, 100,
		   dummy_store_cb);
PREZERO_SYSFS_ATTR(batch_pages, prezero_batch_pages, 0, UINT_MAX,
		   dummy_store_cb);
PREZERO_SYSFS_ATTR(sleep_msecs, prezero_sleep_msecs, 0, UINT_MAX,
		   prezero_sleep_msecs_store_cb);

static struct attribute *prezero_attrs[] = {
	&prezero_attr_enabled.attr,
	&prezero_attr_min_order.attr,
	&prezero_attr_max_percent.attr,
	&prezero_attr_batch_pages.attr,
	&prezero_attr_sleep_msecs.attr,
	NULL,
};

static struct attribute_group prezero_attr_group = {
	.attrs = prezero_attrs,
};

static ssize_t prezero_show_hw_enabled(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", prezero_hw_enabled());
}
static ssize_t prezero_store_hw_enabled(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	static DEFINE_MUTEX(mutex);
	unsigned long val;
	int err;
	ssize_t ret = count;

	mutex_lock(&mutex);

	err = kstrtoul(buf, 0, &val);
	if (err < 0 || val > 1) {
		ret = -EINVAL;
		goto out;
	}

	if (val) {
		if (!prezero_hw_enabled()) {
			err = init_page_clear_engine();
			if (!err)
				static_branch_enable(&prezero_hw_enabled_key);
			else
				ret = err;
		}
	} else {
		if (prezero_hw_enabled()) {
			static_branch_disable(&prezero_hw_enabled_key);
			exit_page_clear_engine();
		}
	}

out:
	mutex_unlock(&mutex);
	return ret;
}
static struct kobj_attribute prezero_attr_hw_enabled =
	__ATTR(hw_enabled, 0644, prezero_show_hw_enabled,
	       prezero_store_hw_enabled);

PREZERO_SYSFS_ATTR(hw_flag_cc, prezero_hw_flag_cc, 0, 1, dummy_store_cb);
PREZERO_SYSFS_ATTR(hw_polling, prezero_hw_polling, 0, 1, dummy_store_cb);

static struct attribute *page_clear_engine_attrs[] = {
	&prezero_attr_hw_enabled.attr,
	&prezero_attr_hw_flag_cc.attr,
	&prezero_attr_hw_polling.attr,
	NULL,
};

static struct attribute_group page_clear_engine_attr_group = {
	.attrs = page_clear_engine_attrs,
	.name = "page_clear_engine",
};

static int __init prezero_sysfs_init(void)
{
	struct kobject *prezero_kobj;
	int err;

	/*
	 * err = sysfs_create_group(mm_kobj, &prezero_attr_group);
	 * if (err)
	 *         pr_err("failed to register prezero group\n");
	 */


	prezero_kobj = kobject_create_and_add("prezero", mm_kobj);
	if (unlikely(!prezero_kobj)) {
		pr_err("failed to create prezero kobject\n");
		return -ENOMEM;
	}

	err = sysfs_create_group(prezero_kobj, &prezero_attr_group);
	if (err) {
		pr_err("failed to register prezero group\n");
		goto delete_obj;
	}

	err = sysfs_create_group(prezero_kobj, &page_clear_engine_attr_group);
	if (err) {
		pr_err("failed to register page_clear_engine group\n");
		goto remove_prezero_group;
	}

	return 0;

remove_prezero_group:
	sysfs_remove_group(prezero_kobj, &prezero_attr_group);
delete_obj:
	kobject_put(prezero_kobj);
	return err;
}
#else
static inline int __init prezero_sysfs_init(void)
{
	return 0;
}
#endif /* CONFIG_SYSFS */

static int __init prezero_init(void)
{
	int ret;
	int nid;

	ret = prezero_sysfs_init();
	if (ret < 0)
		return ret;

	for_each_node_state(nid, N_MEMORY) {
		init_waitqueue_head(&kprezerod_wait[nid]);
		__start_stop_kprezerod(nid);
	}

	return 0;
}
module_init(prezero_init);
