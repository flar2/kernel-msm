#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/slab.h>

#define FPS_ONENAV_TAP    616
#define FPS_ONENAV_HOLD   617
#define FPS_ONENAV_RIGHT  620
#define FPS_ONENAV_LEFT   621

static DEFINE_MUTEX(hb_lock);

struct homebutton_data {
	struct input_dev *hb_dev;
	struct workqueue_struct *hb_input_wq;
	struct work_struct hb_input_work;
	struct kobject *homebutton_kobj;
	int enable;
	bool enable_off;
	unsigned int haptic;
	unsigned int haptic_off;
	unsigned int key;
	unsigned int key_dbltap;
	unsigned int key_left;
	unsigned int key_right;
	unsigned int key_hold;
	unsigned int key_screenoff;
	unsigned int key_screenoff_dbltap;
	unsigned int key_screenoff_left;
	unsigned int key_screenoff_right;
	unsigned int key_screenoff_hold;
	unsigned int current_key;
} hb_data = {
	.enable = 0,
	.enable_off = false,
	.haptic = 0,
	.haptic_off = 0,
	.key = KEY_RESERVED,
	.key_dbltap = KEY_RESERVED,
	.key_left = KEY_RESERVED,
	.key_right = KEY_RESERVED,
	.key_hold = KEY_RESERVED,
	.key_screenoff = KEY_RESERVED,
	.key_screenoff_dbltap = KEY_RESERVED,
	.key_screenoff_left = KEY_RESERVED,
	.key_screenoff_right = KEY_RESERVED,
	.key_screenoff_hold = KEY_RESERVED,
	.current_key = KEY_RESERVED
};

static void hb_input_callback(struct work_struct *unused) {
	if (!hb_data.enable || !mutex_trylock(&hb_lock))
		return;

	input_report_key(hb_data.hb_dev, hb_data.current_key, 1);
	input_sync(hb_data.hb_dev);
	input_report_key(hb_data.hb_dev, hb_data.current_key, 0);
	input_sync(hb_data.hb_dev);

	mutex_unlock(&hb_lock);

	return;
}

static int input_dev_filter(struct input_dev *dev) {

	if (strstr(dev->name, "uinput-fpc")) {
		return 0;
	} else {
		return 1;
	}
}

static int hb_input_connect(struct input_handler *handler,
				struct input_dev *dev, const struct input_device_id *id) {
	int rc;
	struct input_handle *handle;

	if (input_dev_filter(dev)) {
		return -ENODEV;
	}

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "hb";

	rc = input_register_handle(handle);
	if (rc) {
		goto err_input_register_handle;
	}
	
	rc = input_open_device(handle);
	if (rc) {
		goto err_input_open_device;
	}

	return 0;

err_input_open_device:
	input_unregister_handle(handle);
err_input_register_handle:
	kfree(handle);
	return rc;
}

static bool hb_input_filter(struct input_handle *handle, unsigned int type, 
						unsigned int code, int value)
{
	if (type != EV_KEY) {
		return false;
	}
    
    if (value != 1){
        return false;
    }
		
	switch (code) {
        case FPS_ONENAV_TAP:
        case FPS_ONENAV_HOLD:
        case FPS_ONENAV_RIGHT:
		case FPS_ONENAV_LEFT:
            hb_data.current_key = code;
            break;
        case KEY_RESERVED:
        default:
            return false;
	}

	schedule_work(&hb_data.hb_input_work);

	return true;
}

static void hb_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id hb_ids[] = {
	{ .driver_info = 1 },
	{ },
};

static struct input_handler hb_input_handler = {
	.filter		= hb_input_filter,
	.connect	= hb_input_connect,
	.disconnect	= hb_input_disconnect,
	.name		= "hb_inputreq",
	.id_table	= hb_ids,
};

static ssize_t hb_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", hb_data.enable);

	return count;
}

static ssize_t hb_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d ", &hb_data.enable);
	if (hb_data.enable < 0 || hb_data.enable > 1)
		hb_data.enable = 0;
		
	return count;
}

static DEVICE_ATTR(enable, (S_IWUSR | S_IRUGO),
	hb_enable_show, hb_enable_store);

static ssize_t hb_haptic_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", hb_data.haptic);

	return count;
}

static ssize_t hb_haptic_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d ", &hb_data.haptic);
	if (hb_data.haptic < 0 || hb_data.haptic > 1)
		hb_data.haptic = 0;
		
	return count;
}

static DEVICE_ATTR(haptic, (S_IWUSR | S_IRUGO),
	hb_haptic_show, hb_haptic_store);

static ssize_t key_show(struct device *dev,
		 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hb_data.key);
}

static ssize_t key_store(struct device *dev,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long input;

	ret = kstrtoul(buf, 0, &input);
	if (ret < 0)
		return -EINVAL;

	set_bit(input, hb_data.hb_dev->keybit);
	hb_data.key = input;

	return count;
}

static DEVICE_ATTR(key, (S_IWUSR | S_IRUGO),
	key_show, key_store);

static ssize_t key_hold_show(struct device *dev,
		 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hb_data.key_hold);
}

static ssize_t key_hold_store(struct device *dev,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long input;

	ret = kstrtoul(buf, 0, &input);
	if (ret < 0)
		return -EINVAL;

	set_bit(input, hb_data.hb_dev->keybit);
	hb_data.key_hold = input;

	return count;
}

static DEVICE_ATTR(key_hold, (S_IWUSR | S_IRUGO),
	key_hold_show, key_hold_store);

static ssize_t key_left_show(struct device *dev,
		 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hb_data.key_left);
}

static ssize_t key_left_store(struct device *dev,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long input;

	ret = kstrtoul(buf, 0, &input);
	if (ret < 0)
		return -EINVAL;

	set_bit(input, hb_data.hb_dev->keybit);
	hb_data.key_left = input;

	return count;
}

static DEVICE_ATTR(key_left, (S_IWUSR | S_IRUGO),
	key_left_show, key_left_store);

static ssize_t key_right_show(struct device *dev,
		 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hb_data.key_right);
}

static ssize_t key_right_store(struct device *dev,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long input;

	ret = kstrtoul(buf, 0, &input);
	if (ret < 0)
		return -EINVAL;

	set_bit(input, hb_data.hb_dev->keybit);
	hb_data.key_right = input;

	return count;
}

static DEVICE_ATTR(key_right, (S_IWUSR | S_IRUGO),
	key_right_show, key_right_store);

static ssize_t key_dbltap_show(struct device *dev,
		 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hb_data.key_dbltap);
}

static ssize_t key_dbltap_store(struct device *dev,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long input;

	ret = kstrtoul(buf, 0, &input);
	if (ret < 0)
		return -EINVAL;

	set_bit(input, hb_data.hb_dev->keybit);
	hb_data.key_dbltap = input;

	return count;
}

static DEVICE_ATTR(key_dbltap, (S_IWUSR | S_IRUGO),
	key_dbltap_show, key_dbltap_store);

static ssize_t hb_enable_off_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hb_data.enable_off);
}

static ssize_t hb_enable_off_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int rc;
	unsigned long input;

	rc = kstrtoul(buf, 0, &input);
	if (rc < 0)
		return -EINVAL;

	if (input < 0 || input > 1)
		input = 0;

	hb_data.enable_off = input;

	return count;
}

static DEVICE_ATTR(enable_off, (S_IWUSR | S_IRUGO),
	hb_enable_off_show, hb_enable_off_store);

static ssize_t hb_haptic_off_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", hb_data.haptic_off);

	return count;
}

static ssize_t hb_haptic_off_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d ", &hb_data.haptic_off);
	if (hb_data.haptic_off < 0 || hb_data.haptic_off > 1)
		hb_data.haptic_off = 0;
		
	return count;
}

static DEVICE_ATTR(haptic_off, (S_IWUSR | S_IRUGO),
	hb_haptic_off_show, hb_haptic_off_store);

static ssize_t key_screenoff_show(struct device *dev,
		 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hb_data.key_screenoff);
}

static ssize_t key_screenoff_store(struct device *dev,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long input;

	ret = kstrtoul(buf, 0, &input);
	if (ret < 0)
		return -EINVAL;

	set_bit(input, hb_data.hb_dev->keybit);
	hb_data.key_screenoff = input;

	return count;
}

static DEVICE_ATTR(key_screenoff, (S_IWUSR | S_IRUGO),
	key_screenoff_show, key_screenoff_store);

static ssize_t key_screenoff_hold_show(struct device *dev,
		 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hb_data.key_screenoff_hold);
}

static ssize_t key_screenoff_hold_store(struct device *dev,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long input;

	ret = kstrtoul(buf, 0, &input);
	if (ret < 0)
		return -EINVAL;

	set_bit(input, hb_data.hb_dev->keybit);
	hb_data.key_screenoff_hold = input;

	return count;
}

static DEVICE_ATTR(key_screenoff_hold, (S_IWUSR | S_IRUGO),
	key_screenoff_hold_show, key_screenoff_hold_store);

static ssize_t key_screenoff_left_show(struct device *dev,
		 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hb_data.key_screenoff_left);
}

static ssize_t key_screenoff_left_store(struct device *dev,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long input;

	ret = kstrtoul(buf, 0, &input);
	if (ret < 0)
		return -EINVAL;

	set_bit(input, hb_data.hb_dev->keybit);
	hb_data.key_screenoff_left = input;

	return count;
}

static DEVICE_ATTR(key_screenoff_left, (S_IWUSR | S_IRUGO),
	key_screenoff_left_show, key_screenoff_left_store);

static ssize_t key_screenoff_right_show(struct device *dev,
		 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hb_data.key_screenoff_right);
}

static ssize_t key_screenoff_right_store(struct device *dev,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long input;

	ret = kstrtoul(buf, 0, &input);
	if (ret < 0)
		return -EINVAL;

	set_bit(input, hb_data.hb_dev->keybit);
	hb_data.key_screenoff_right = input;

	return count;
}

static DEVICE_ATTR(key_screenoff_right, (S_IWUSR | S_IRUGO),
	key_screenoff_right_show, key_screenoff_right_store);

static ssize_t key_screenoff_dbltap_show(struct device *dev,
		 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hb_data.key_screenoff_dbltap);
}

static ssize_t key_screenoff_dbltap_store(struct device *dev,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long input;

	ret = kstrtoul(buf, 0, &input);
	if (ret < 0)
		return -EINVAL;

	set_bit(input, hb_data.hb_dev->keybit);
	hb_data.key_screenoff_dbltap = input;

	return count;
}

static DEVICE_ATTR(key_screenoff_dbltap, (S_IWUSR | S_IRUGO),
	key_screenoff_dbltap_show, key_screenoff_dbltap_store);

static int __init hb_init(void)
{
	int rc = 0;

	hb_data.hb_dev = input_allocate_device();
	if (!hb_data.hb_dev) {
		pr_err("Failed to allocate hb_dev\n");
		goto err_alloc_dev;
	}

	input_set_capability(hb_data.hb_dev, EV_KEY, FPS_ONENAV_TAP);
	input_set_capability(hb_data.hb_dev, EV_KEY, FPS_ONENAV_HOLD);
	input_set_capability(hb_data.hb_dev, EV_KEY, FPS_ONENAV_RIGHT);
	input_set_capability(hb_data.hb_dev, EV_KEY, FPS_ONENAV_LEFT);
	set_bit(EV_KEY, hb_data.hb_dev->evbit);
	hb_data.hb_dev->name = "qwerty";
	hb_data.hb_dev->phys = "qwerty/input0";

	rc = input_register_device(hb_data.hb_dev);
	if (rc) {
		pr_err("%s: input_register_device err=%d\n", __func__, rc);
		goto err_input_dev;
	}

	rc = input_register_handler(&hb_input_handler);
	if (rc)
		pr_err("%s: Failed to register hb_input_handler\n", __func__);

	hb_data.hb_input_wq = create_workqueue("hb_wq");
	if (!hb_data.hb_input_wq) {
		pr_err("%s: Failed to create workqueue\n", __func__);
		return -EFAULT;
	}
	INIT_WORK(&hb_data.hb_input_work, hb_input_callback);

	hb_data.homebutton_kobj = kobject_create_and_add("homebutton", NULL) ;
	if (hb_data.homebutton_kobj == NULL) {
		pr_warn("%s: homebutton_kobj failed\n", __func__);
	}

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_enable.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton enable\n", __func__);

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_haptic.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton haptic\n", __func__);

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_key.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton key\n", __func__);

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_key_hold.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton key\n", __func__);

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_key_left.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton key\n", __func__);

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_key_right.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton key\n", __func__);

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_key_dbltap.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton key\n", __func__);

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_enable_off.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton screen off key\n", __func__);

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_haptic_off.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton haptic screen off key\n", __func__);

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_key_screenoff.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton key\n", __func__);

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_key_screenoff_hold.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton key\n", __func__);

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_key_screenoff_left.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton key\n", __func__);

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_key_screenoff_right.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton key\n", __func__);

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_key_screenoff_dbltap.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton key\n", __func__);

err_input_dev:
	input_free_device(hb_data.hb_dev);

err_alloc_dev:
	pr_info("%s hb done\n", __func__);

	return 0;
}

static void __exit hb_exit(void)
{
	kobject_del(hb_data.homebutton_kobj);
	destroy_workqueue(hb_data.hb_input_wq);
	input_unregister_handler(&hb_input_handler);
	input_unregister_device(hb_data.hb_dev);
	input_free_device(hb_data.hb_dev);

	return;
}

module_init(hb_init);
module_exit(hb_exit);
