#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>

#define DEVICE_NAME "ntt_hw"
#define CLASS_NAME  "ntt"

#define NTT_BASE_ADDR 0x40000000
#define NTT_RANGE     0x10000

/* HLS AXI-Lite register map (need to verify with your exported IP header/component.xml) */
#define REG_AP_CTRL   0x00
#define REG_GIE       0x04
#define REG_IER       0x08
#define REG_ISR       0x0c
#define REG_A_LOW     0x10
#define REG_A_HIGH    0x14

#define AP_START      0x01
#define AP_DONE       0x02
#define AP_IDLE       0x04
#define AP_READY      0x08
#define AUTO_RESTART  0x80

#define NTT_SIZE      1024
#define IOCTL_RUN_NTT _IO('q', 1)

static void __iomem *regs;
static dev_t dev_num;
static struct cdev ntt_cdev;
static struct class *ntt_class;
static struct device *ntt_dev;

static void dump_ap_ctrl(const char *tag);
static void dump_regs(const char *tag);
static void poll_ap_ctrl_debug(const char *tag, int count, unsigned int delay_us);
static void test_ap_ctrl_start(void);
static void test_axil_rw(void);
static void test_ap_ctrl_start(void)
{
    u32 ctrl;

    if (!regs) {
        pr_err("ntt: test_ap_ctrl_start regs is NULL\n");
        return;
    }

    pr_info("ntt: ===== AP_CTRL start test begin =====\n");

    dump_ap_ctrl("before_start_test");

    iowrite32(AP_START, regs + REG_AP_CTRL);

    ctrl = ioread32(regs + REG_AP_CTRL);
    pr_info("ntt: immediate AP_CTRL after start write = 0x%08x\n", ctrl);

    dump_ap_ctrl("after_start_test_immediate");
    poll_ap_ctrl_debug("after_start_test", 10, 1000);

    dump_regs("after_start_test");

    pr_info("ntt: ===== AP_CTRL start test end =====\n");
}
static void test_axil_rw(void)
{
    u32 v;

    pr_info("ntt: ===== AXI-Lite R/W test begin =====\n");

    v = ioread32(regs + REG_GIE);
    pr_info("ntt: GIE before = 0x%08x\n", v);
    iowrite32(0x00000001, regs + REG_GIE);
    v = ioread32(regs + REG_GIE);
    pr_info("ntt: GIE after write 1 = 0x%08x\n", v);
    iowrite32(0x00000000, regs + REG_GIE);
    v = ioread32(regs + REG_GIE);
    pr_info("ntt: GIE after write 0 = 0x%08x\n", v);

    v = ioread32(regs + REG_IER);
    pr_info("ntt: IER before = 0x%08x\n", v);
    iowrite32(0x00000003, regs + REG_IER);
    v = ioread32(regs + REG_IER);
    pr_info("ntt: IER after write 3 = 0x%08x\n", v);
    iowrite32(0x00000000, regs + REG_IER);
    v = ioread32(regs + REG_IER);
    pr_info("ntt: IER after write 0 = 0x%08x\n", v);

    v = ioread32(regs + REG_A_LOW);
    pr_info("ntt: A_LOW before = 0x%08x\n", v);
    iowrite32(0x12345678, regs + REG_A_LOW);
    v = ioread32(regs + REG_A_LOW);
    pr_info("ntt: A_LOW after write = 0x%08x\n", v);

    v = ioread32(regs + REG_A_HIGH);
    pr_info("ntt: A_HIGH before = 0x%08x\n", v);
    iowrite32(0xabcdef00, regs + REG_A_HIGH);
    v = ioread32(regs + REG_A_HIGH);
    pr_info("ntt: A_HIGH after write = 0x%08x\n", v);

    pr_info("ntt: ===== AXI-Lite R/W test end =====\n");
}
static void dump_ap_ctrl(const char *tag)
{
    u32 v;

    if (!regs) {
        pr_err("ntt: %s regs is NULL\n", tag);
        return;
    }

    v = ioread32(regs + REG_AP_CTRL);

    pr_info("ntt: %s AP_CTRL=0x%08x [start=%u done=%u idle=%u ready=%u auto_restart=%u]\n",
            tag,
            v,
            !!(v & BIT(0)),
            !!(v & BIT(1)),
            !!(v & BIT(2)),
            !!(v & BIT(3)),
            !!(v & BIT(7)));
}

static void dump_regs(const char *tag)
{
    u32 ap_ctrl, gie, ier, isr, a_low, a_high;

    if (!regs) {
        pr_err("ntt: %s regs is NULL\n", tag);
        return;
    }

    ap_ctrl = ioread32(regs + REG_AP_CTRL);
    gie     = ioread32(regs + REG_GIE);
    ier     = ioread32(regs + REG_IER);
    isr     = ioread32(regs + REG_ISR);
    a_low   = ioread32(regs + REG_A_LOW);
    a_high  = ioread32(regs + REG_A_HIGH);

    pr_info("ntt: %s register dump:\n", tag);
    pr_info("ntt:   AP_CTRL = 0x%08x [start=%u done=%u idle=%u ready=%u auto_restart=%u]\n",
            ap_ctrl,
            !!(ap_ctrl & BIT(0)),
            !!(ap_ctrl & BIT(1)),
            !!(ap_ctrl & BIT(2)),
            !!(ap_ctrl & BIT(3)),
            !!(ap_ctrl & BIT(7)));
    pr_info("ntt:   GIE     = 0x%08x\n", gie);
    pr_info("ntt:   IER     = 0x%08x\n", ier);
    pr_info("ntt:   ISR     = 0x%08x\n", isr);
    pr_info("ntt:   A_LOW   = 0x%08x\n", a_low);
    pr_info("ntt:   A_HIGH  = 0x%08x\n", a_high);
}
static void poll_ap_ctrl_debug(const char *tag, int count, unsigned int delay_us)
{
    int i;
    u32 v;

    if (!regs) {
        pr_err("ntt: %s regs is NULL\n", tag);
        return;
    }

    pr_info("ntt: ===== %s AP_CTRL poll begin =====\n", tag);

    for (i = 0; i < count; i++) {
        v = ioread32(regs + REG_AP_CTRL);
        pr_info("ntt: %s poll[%d] AP_CTRL=0x%08x [start=%u done=%u idle=%u ready=%u auto_restart=%u]\n",
                tag,
                i,
                v,
                !!(v & BIT(0)),
                !!(v & BIT(1)),
                !!(v & BIT(2)),
                !!(v & BIT(3)),
                !!(v & BIT(7)));
        if (delay_us)
            udelay(delay_us);
    }

    pr_info("ntt: ===== %s AP_CTRL poll end =====\n", tag);
}
static long ntt_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    void *buf_virt;
    dma_addr_t buf_phys;
    u32 low, high;
    unsigned long not_copied;
    unsigned long timeout;
    u32 ctrl;

    if (cmd != IOCTL_RUN_NTT)
        return -EINVAL;

    if (!ntt_dev) {
        pr_err("ntt: ntt_dev is NULL\n");
        return -ENODEV;
    }

    if (!regs) {
        pr_err("ntt: regs is NULL\n");
        return -ENODEV;
    }

    pr_info("ntt: ioctl enter\n");

    buf_virt = dma_alloc_coherent(ntt_dev, NTT_SIZE, &buf_phys, GFP_KERNEL);
    if (!buf_virt) {
        pr_err("ntt: dma_alloc_coherent failed\n");
        return -ENOMEM;
    }

    pr_info("ntt: dma buffer virt=%p phys=%pad size=%u\n",
            buf_virt, &buf_phys, NTT_SIZE);

    not_copied = copy_from_user(buf_virt, (void __user *)arg, NTT_SIZE);
    if (not_copied) {
        pr_err("ntt: copy_from_user failed, not_copied=%lu\n", not_copied);
        dma_free_coherent(ntt_dev, NTT_SIZE, buf_virt, buf_phys);
        return -EFAULT;
    }

    low  = lower_32_bits((u64)buf_phys);
    high = upper_32_bits((u64)buf_phys);

    pr_info("ntt: writing buffer address low=0x%08x high=0x%08x\n", low, high);
   
    iowrite32(0x00000000, regs + REG_AP_CTRL);
    dump_ap_ctrl("after_force_clear");
    
    dump_regs("before_program");

    iowrite32(low,  regs + REG_A_LOW);
    iowrite32(high, regs + REG_A_HIGH);

    dump_regs("after_addr_program");

    /*
     * Optional: clear stale interrupt status if HLS IP uses it.
     * Safe for many HLS-generated blocks, but if your IP map differs, verify first.
     */
    iowrite32(0xffffffff, regs + REG_ISR);

   dump_ap_ctrl("before_start");

iowrite32(AP_START, regs + REG_AP_CTRL);

/* 先立刻讀一次 */
ctrl = ioread32(regs + REG_AP_CTRL);
pr_info("ntt: start written, immediate AP_CTRL readback=0x%08x\n", ctrl);

dump_ap_ctrl("after_start_immediate");

/* 再連續 poll 幾次，看狀態有沒有變化 */
poll_ap_ctrl_debug("after_start", 10, 1000);

dump_regs("after_start");
    timeout = jiffies + msecs_to_jiffies(1000);
    while (!(ioread32(regs + REG_AP_CTRL) & AP_DONE)) {
        if (time_after(jiffies, timeout)) {
            dump_regs("timeout");
            pr_err("ntt: timeout waiting for AP_DONE\n");
            dma_free_coherent(ntt_dev, NTT_SIZE, buf_virt, buf_phys);
            return -ETIMEDOUT;
        }
        cpu_relax();
    }

    dump_regs("after_done");

    not_copied = copy_to_user((void __user *)arg, buf_virt, NTT_SIZE);

    dma_free_coherent(ntt_dev, NTT_SIZE, buf_virt, buf_phys);

    if (not_copied) {
        pr_err("ntt: copy_to_user failed, not_copied=%lu\n", not_copied);
        return -EFAULT;
    }

    pr_info("ntt: ioctl exit success\n");
    return 0;
}

static int ntt_open(struct inode *i, struct file *f)
{
    pr_info("ntt: device opened\n");
    return 0;
}

static int ntt_release(struct inode *i, struct file *f)
{
    pr_info("ntt: device released\n");
    return 0;
}

static const struct file_operations fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = ntt_ioctl,
    .open           = ntt_open,
    .release        = ntt_release,
};

static int __init ntt_init(void)
{
    int rc;

    regs = ioremap(NTT_BASE_ADDR, NTT_RANGE);
    if (!regs) {
        pr_err("ntt: ioremap failed\n");
        return -ENOMEM;
    }

    rc = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (rc) {
        pr_err("ntt: alloc_chrdev_region failed\n");
        goto err_unmap;
    }

    cdev_init(&ntt_cdev, &fops);
    rc = cdev_add(&ntt_cdev, dev_num, 1);
    if (rc) {
        pr_err("ntt: cdev_add failed\n");
        goto err_unreg;
    }

    ntt_class = class_create(CLASS_NAME);
    if (IS_ERR(ntt_class)) {
        rc = PTR_ERR(ntt_class);
        pr_err("ntt: class_create failed\n");
        goto err_cdev;
    }

    ntt_dev = device_create(ntt_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(ntt_dev)) {
        rc = PTR_ERR(ntt_dev);
        pr_err("ntt: device_create failed\n");
        ntt_dev = NULL;
        goto err_class;
    }

    pr_info("ntt_hw driver loaded\n");
    pr_info("ntt: regs mapped at virt=%p phys_base=0x%08x range=0x%x\n",
            regs, NTT_BASE_ADDR, NTT_RANGE);

    dump_regs("init");
    test_axil_rw();
    

    return 0;

err_class:
    class_destroy(ntt_class);
err_cdev:
    cdev_del(&ntt_cdev);
err_unreg:
    unregister_chrdev_region(dev_num, 1);
err_unmap:
    iounmap(regs);
    regs = NULL;
    return rc;
}

static void __exit ntt_exit(void)
{
    if (ntt_dev) {
        device_destroy(ntt_class, dev_num);
        ntt_dev = NULL;
    }

    if (ntt_class)
        class_destroy(ntt_class);

    cdev_del(&ntt_cdev);
    unregister_chrdev_region(dev_num, 1);

    if (regs) {
        iounmap(regs);
        regs = NULL;
    }

    pr_info("ntt_hw driver unloaded\n");
}

module_init(ntt_init);
module_exit(ntt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OpenAI");
MODULE_DESCRIPTION("NTT HW driver for Zynq/PYNQ (debug version)");