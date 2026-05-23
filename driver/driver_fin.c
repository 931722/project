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

/* ========================================================================= */
/* 【修改 1】直接在頂部宣告 Batching 需要的結構體與全新的 IOCTL 控制碼       */
/* ========================================================================= */
#define MAX_BATCH     8
#define POLY_SIZE     256

struct ntt_batch_req {
    int32_t __user *coeffs;   // 指向 User space 的平坦一維陣列
    int num_polys;            // 這次批次包含幾個多項式 (1~8)
    int mode;                 // 0: NTT, 1: INTT
} __attribute__((packed));    // 強制包緊，防止核心與應用程式的記憶體對齊不一致

// 使用 _IOWR 代表我們會寫入參數給核心，硬體算完也會讀取結果回 User space
#define IOCTL_RUN_BATCH _IOWR('q', 3, struct ntt_batch_req)
/* ========================================================================= */

/* HLS AXI-Lite register map (need to verify with your exported IP header/component.xml) */
#define REG_AP_CTRL   0x00
#define REG_GIE       0x04
#define REG_IER       0x08
#define REG_ISR       0x0c
#define REG_A_LOW     0x10
#define REG_A_HIGH    0x14
#define REG_MODE      0x1c
#define REG_NUM_POLYS 0x24

#define AP_START      0x01
#define AP_DONE       0x02
#define AP_IDLE       0x04
#define AP_READY      0x08
#define AUTO_RESTART  0x80

#define NTT_SIZE      1024
//#define IOCTL_RUN_NTT _IO('q', 1)
//#define IOCTL_RUN_INTT _IO('q', 2)

static void __iomem *regs;
static dev_t dev_num;
static struct cdev ntt_cdev;
static struct class *ntt_class;
static struct device *ntt_dev;

static long ntt_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    void *buf_virt;
    dma_addr_t buf_phys;
    u32 low, high;
    unsigned long not_copied;
    unsigned long timeout;
    u32 ctrl;

    /* ========================================================================= */
    /* 【修改 3】區域變數調整：引入請求結構體與動態拷貝大小                      */
    /* ========================================================================= */
    struct ntt_batch_req req;
    size_t copy_size;

    // 檢查控制碼是否為我們新定義的 Batch 指令
    if (cmd != IOCTL_RUN_BATCH)
        return -EINVAL;
    /* ========================================================================= */

    if (!ntt_dev) {
        pr_err("ntt: ntt_dev is NULL\n");
        return -ENODEV;
    }

    if (!regs) {
        pr_err("ntt: regs is NULL\n");
        return -ENODEV;
    }
    /* ========================================================================= */
    /* 【修改 4】解包 User 傳來的信封 (Struct) 並動態計算這次需要多大的 DMA 空間 */
    /* ========================================================================= */
    // 先把結構體本身從小口袋 (arg) 拷貝出來
    if (copy_from_user(&req, (void __user *)arg, sizeof(struct ntt_batch_req))) {
        return -EFAULT;
    }

    // 防呆檢查：避免惡意輸入導致核心崩潰
    if (req.num_polys < 1 || req.num_polys > MAX_BATCH) {
        pr_err("ntt: invalid num_polys = %d\n", req.num_polys);
        return -EINVAL;
    }

    // 動態計算這次批次需要多少 Byte (例如：4 * 256 * 4 = 4096 Bytes)
    copy_size = req.num_polys * POLY_SIZE * sizeof(int32_t);

    // 根據實際需要的批次大小分配 DMA 一致性記憶體
    buf_virt = dma_alloc_coherent(ntt_dev, copy_size, &buf_phys, GFP_KERNEL);
    if (!buf_virt) {
        pr_err("ntt: dma_alloc_coherent failed for size %zu\n", copy_size);
        return -ENOMEM;
    }

    // 從結構體內指定的 coeffs 指標，將整批多項式倒進核心 DMA 緩衝區
    not_copied = copy_from_user(buf_virt, req.coeffs, copy_size);
    if (not_copied) {
        pr_err("ntt: copy_from_user failed, not_copied=%lu\n", not_copied);
        dma_free_coherent(ntt_dev, copy_size, buf_virt, buf_phys);
        return -EFAULT;
    }
    /* ========================================================================= */


    low  = lower_32_bits((u64)buf_phys);
    high = upper_32_bits((u64)buf_phys);

   
    iowrite32(0x00000000, regs + REG_AP_CTRL);

    iowrite32(low,  regs + REG_A_LOW);
    iowrite32(high, regs + REG_A_HIGH);

    /* ========================================================================= */
    /* 【修改 5】將結構體內的 mode 與 num_polys 寫入 FPGA 暫存器                */
    /* ========================================================================= */
    iowrite32(req.mode, regs + REG_MODE);           // 寫入模式 (0: NTT, 1: INTT)
    iowrite32(req.num_polys, regs + REG_NUM_POLYS); // 寫入批次數量
    /* ========================================================================= */


    /*
     * Optional: clear stale interrupt status if HLS IP uses it.
     * Safe for many HLS-generated blocks, but if your IP map differs, verify first.
     */
    iowrite32(0xffffffff, regs + REG_ISR);


    iowrite32(AP_START, regs + REG_AP_CTRL);


    timeout = jiffies + msecs_to_jiffies(1000);

    while (1) {
        // 1. 唯一的一次讀取！(在這裡打開信件)
        ctrl = ioread32(regs + REG_AP_CTRL);
        //pr_info("ntt: debug ap_ctrl value = 0x%08x\n", ctrl);
        // 3. 檢查是不是 Done 了？
        if (ctrl & AP_DONE) {
            break; // 抓到了，順利跳出迴圈往下交貨！
        }

        // 4. 檢查是不是等太久了？
        if (time_after(jiffies, timeout)) {
            pr_err("ntt: timeout waiting for AP_DONE\n");
            dma_free_coherent(ntt_dev, NTT_SIZE, buf_virt, buf_phys);
            return -ETIMEDOUT;
        }

    }

    /* ========================================================================= */
    /* 【修改 6】算完後，使用動態大小將結果寫回 User 指定的 coeffs 陣列          */
    /* ========================================================================= */
    not_copied = copy_to_user(req.coeffs, buf_virt, copy_size);

    dma_free_coherent(ntt_dev, copy_size, buf_virt, buf_phys);
    /* ========================================================================= */

    if (not_copied) {
        pr_err("ntt: copy_to_user failed, not_copied=%lu\n", not_copied);
        return -EFAULT;
    }

    return 0;
}

static int ntt_open(struct inode *i, struct file *f)
{
    //pr_info("ntt: device opened\n");
    return 0;
}

static int ntt_release(struct inode *i, struct file *f)
{
    //pr_info("ntt: device released\n");
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

    //pr_info("ntt_hw driver unloaded\n");
}

module_init(ntt_init);
module_exit(ntt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OpenAI");
MODULE_DESCRIPTION("NTT HW driver for Zynq/PYNQ (debug version)");
