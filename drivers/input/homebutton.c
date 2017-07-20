#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/fb.h>

#define FPS_ONENAV_DOWN   614
#define FPS_ONENAV_UP     615
#define FPS_ONENAV_TAP    616
#define FPS_ONENAV_HOLD   617
#define FPS_ONENAV_YPLUS  618 // LEFT
#define FPS_ONENAV_YMINUS 619 // RIGHT
#define FPS_ONENAV_XPLUS  620 // DOWN
#define FPS_ONENAV_XMINUS 621 // UP
#define FPS_ONENAV_DBLTAP 622
#define FPS_ONENAV_DBLTAP_BIS 112

#define VIB_STRENGTH	30

static DEFINE_MUTEX(hb_lock);
extern void set_vibrate(int value);

struct homebutton_data {
	struct input_dev *hb_dev;
	struct workqueue_struct *hb_input_wq;
	struct work_struct hb_input_work;
	struct notifier_block notif;
	struct kobject *homebutton_kobj;
	bool key_press;
	bool scr_suspended;
	bool enable;
	bool enable_off;
	bool haptic;
	unsigned int key;
	unsigned int key_left;
	unsigned int key_right;
	unsigned int key_hold;
	unsigned int key_down;
	unsigned int key_up;
	unsigned int key_dbltap;
	unsigned int current_key;
} hb_data = {
	.enable = true,
	.enable_off = false,
	.haptic = false,
	.key = KEY_RESERVED,
	.key_hold = KEY_RESERVED,
	.key_left = KEY_RESERVED,
	.key_right = KEY_RESERVED,
	.key_down = KEY_RESERVED,
	.key_up = KEY_RESERVED,
	.key_dbltap = KEY_RESERVED,
	.current_key = KEY_HOME
};

static void hb_input_callback(struct work_struct *unused) {
	if (!mutex_trylock(&hb_lock))
		return;

	if (hb_data.haptic)
		set_vibrate(VIB_STRENGTH);

	input_report_key(hb_data.hb_dev, hb_data.current_key, hb_data.key_press);
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

	if (!hb_data.enable) {
		return false;
	}
	
	if (hb_data.scr_suspended && !hb_data.enable_off)
		return false;
	
	if (value == 1)
		hb_data.key_press = true;
	else
		hb_data.key_press = false;
		
	switch (code) {
		case FPS_ONENAV_TAP:
			hb_data.current_key = hb_data.key;
			break;
		case FPS_ONENAV_HOLD:
			hb_data.current_key = hb_data.key_hold;
			break;
		case FPS_ONENAV_YPLUS:
			hb_data.current_key = hb_data.key_up;
			break;
		case FPS_ONENAV_YMINUS:
			hb_data.current_key = hb_data.key_down;
			break;
		case FPS_ONENAV_XPLUS:
			hb_data.current_key = hb_data.key_right;
			break;
		case FPS_ONENAV_XMINUS:
			hb_data.current_key = hb_data.key_left;
			break;
		case FPS_ONENAV_DBLTAP_BIS:
		case FPS_ONENAV_DBLTAP:
			hb_data.current_key = hb_data.key_dbltap;
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

static int fb_notifier_callback(struct notifier_block *this,
				unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;

	if (evdata && evdata->data && event == FB_EVENT_BLANK) {
		blank = evdata->data;
		switch (*blank) {
			case FB_BLANK_UNBLANK:
				//display on
				hb_data.scr_suspended = false;
				break;
			case FB_BLANK_POWERDOWN:
			case FB_BLANK_HSYNC_SUSPEND:
			case FB_BLANK_VSYNC_SUSPEND:
			case FB_BLANK_NORMAL:
				//display off
				hb_data.scr_suspended = true;
				break;
		}
	}

	return NOTIFY_OK;
}

static ssize_t hb_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hb_data.enable);
}

static ssize_t hb_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int rc;
	unsigned long input;

	rc = kstrtoul(buf, 0, &input);
	if (rc < 0)
		return -EINVAL;

	if (input < 0 || input > 1)
		input = 0;

	hb_data.enable = input;

	return count;
}

static DEVICE_ATTR(enable, (S_IWUSR | S_IRUGO),
	hb_enable_show, hb_enable_store);

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

static ssize_t key_down_show(struct device *dev,
		 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hb_data.key_down);
}

static ssize_t key_down_store(struct device *dev,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long input;

	ret = kstrtoul(buf, 0, &input);
	if (ret < 0)
		return -EINVAL;

	set_bit(input, hb_data.hb_dev->keybit);
	hb_data.key_down = input;

	return count;
}

static DEVICE_ATTR(key_down, (S_IWUSR | S_IRUGO),
	key_down_show, key_down_store);

static ssize_t key_up_show(struct device *dev,
		 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hb_data.key_up);
}

static ssize_t key_up_store(struct device *dev,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned long input;

	ret = kstrtoul(buf, 0, &input);
	if (ret < 0)
		return -EINVAL;

	set_bit(input, hb_data.hb_dev->keybit);
	hb_data.key_up = input;

	return count;
}

static DEVICE_ATTR(key_up, (S_IWUSR | S_IRUGO),
	key_up_show, key_up_store);

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

static ssize_t hb_haptic_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hb_data.haptic);
}

static ssize_t hb_haptic_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int rc;
	unsigned long input;

	rc = kstrtoul(buf, 0, &input);
	if (rc < 0)
		return -EINVAL;

	if (input < 0 || input > 1)
		input = 0;

	hb_data.haptic = input;

	return count;
}

static DEVICE_ATTR(haptic, (S_IWUSR | S_IRUGO),
	hb_haptic_show, hb_haptic_store);

static DEVICE_ATTR(key_dbltap, (S_IWUSR | S_IRUGO),
	key_dbltap_show, key_dbltap_store);

static int __init hb_init(void)
{
	int rc = 0;

	hb_data.hb_dev = input_allocate_device();
	if (!hb_data.hb_dev) {
		pr_err("Failed to allocate hb_dev\n");
		goto err_alloc_dev;
	}

	input_set_capability(hb_data.hb_dev, EV_KEY, KEY_HOME);
	input_set_capability(hb_data.hb_dev, EV_KEY, KEY_POWER);
	input_set_capability(hb_data.hb_dev, EV_KEY, 580); // APP_SWITCH
	input_set_capability(hb_data.hb_dev, EV_KEY, 582); // VOICE_ASSIST
	input_set_capability(hb_data.hb_dev, EV_KEY, KEY_BACK);
	input_set_capability(hb_data.hb_dev, EV_KEY, KEY_VOLUMEDOWN);
	input_set_capability(hb_data.hb_dev, EV_KEY, KEY_VOLUMEUP);
	input_set_capability(hb_data.hb_dev, EV_KEY, KEY_PLAYPAUSE);
	input_set_capability(hb_data.hb_dev, EV_KEY, KEY_PREVIOUSSONG);
	input_set_capability(hb_data.hb_dev, EV_KEY, KEY_NEXTSONG);
	set_bit(EV_KEY, hb_data.hb_dev->evbit);
	set_bit(KEY_HOME, hb_data.hb_dev->keybit);
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

	hb_data.notif.notifier_call = fb_notifier_callback;
	if (fb_register_client(&hb_data.notif)) {
		rc = -EINVAL;
		goto err_alloc_dev;
	}

	hb_data.homebutton_kobj = kobject_create_and_add("homebutton", NULL) ;
	if (hb_data.homebutton_kobj == NULL) {
		pr_warn("%s: homebutton_kobj failed\n", __func__);
	}

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_enable.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton enable\n", __func__);

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_key.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton key\n", __func__);

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_key_hold.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton key\n", __func__);

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_key_down.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton key\n", __func__);

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_key_up.attr);
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

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_haptic.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton haptic key\n", __func__);

	rc = sysfs_create_file(hb_data.homebutton_kobj, &dev_attr_enable_off.attr);
	if (rc)
		pr_err("%s: sysfs_create_file failed for homebutton screen off key\n", __func__);

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
