/*
 * drivers/input/touchscreen/screen_off_gestures.c
 *
 *
 * Copyright (c) 2013, Dennis Rassmann <showp1984@gmail.com>
 * Copyright (c) 2013-16 Aaron Segaert <asegaert@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/screen_off_gestures.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/hrtimer.h>
#include <asm-generic/cputime.h>
/*
#include <linux/wakelock.h>
*/

/* Tuneables */
#define WG_DEBUG		0
#define WG_DEFAULT		0
#define DT2W_DEFAULT		0
#define S2W_DEFAULT		0
#define S2S_DEFAULT		0
#define WG_PWRKEY_DUR           60

/* Redmi Note 3 */
#define SWEEP_Y_MAX             1920
#define SWEEP_X_MAX             1080
#define SWEEP_EDGE		90
#define SWEEP_Y_LIMIT           SWEEP_Y_MAX-SWEEP_EDGE
#define SWEEP_X_LIMIT           SWEEP_X_MAX-SWEEP_EDGE
#define SWEEP_X_B1              340
#define SWEEP_X_B2              620
#define SWEEP_Y_START		800
#define SWEEP_X_START		540
#define SWEEP_X_FINAL           270
#define SWEEP_Y_NEXT            135
#define DT2W_FEATHER		150
#define DT2W_TIME 		250

/* Wake Gestures */
#define SWEEP_TIMEOUT		300
#define TRIGGER_TIMEOUT		500
#define SCREEN_OFF_GESTURE		0x0b
#define SWEEP_RIGHT		0x01
#define SWEEP_LEFT		0x02
#define SWEEP_UP		0x04
#define SWEEP_DOWN		0x08

#define KEY_GESTURE_SWIPE_RIGHT       622
#define KEY_GESTURE_SWIPE_LEFT        623
#define KEY_GESTURE_SWIPE_DOWN        624
#define KEY_GESTURE_SWIPE_UP          625
#define KEY_GESTURE_DOUBLE_TAP        626

#define LOGTAG			"WG"

/* Resources */
int gesture_swipe_right = 0, gesture_swipe_left = 0, gesture_swipe_down = 0, gesture_swipe_up = 0;
int dt2w_switch = 0;
static int touch_x = 0, touch_y = 0;
static bool touch_x_called = false, touch_y_called = false;
static bool exec_countx = true, exec_county = true, exec_count = true;
static bool barrierx[2] = {false, false}, barriery[2] = {false, false};
static int firstx = 0, firsty = 0;
static int sweep_y_limit = SWEEP_Y_LIMIT;
static int sweep_x_limit = SWEEP_X_LIMIT;
static unsigned long firstx_time = 0, firsty_time = 0;
static unsigned long long tap_time_pre = 0;
static int touch_nr = 0, x_pre = 0, y_pre = 0;
static bool touch_cnt = true;

static struct input_dev * wake_dev;
static struct workqueue_struct *screen_off_gestures_input_wq;
static struct workqueue_struct *dt2w_input_wq;
static struct work_struct screen_off_gestures_input_work;
static struct work_struct dt2w_input_work;

static bool is_suspended(void)
{
	return scr_suspended();
}

/* Doubletap2wake */

static void doubletap2wake_reset(void) {
	exec_count = true;
	touch_nr = 0;
	tap_time_pre = 0;
	x_pre = 0;
	y_pre = 0;
}

static unsigned int calc_feather(int coord, int prev_coord) {
	int calc_coord = 0;
	calc_coord = coord-prev_coord;
	if (calc_coord < 0)
		calc_coord = calc_coord * (-1);
	return calc_coord;
}

/* init a new touch */
static void new_touch(int x, int y) {
	tap_time_pre = ktime_to_ms(ktime_get());
	x_pre = x;
	y_pre = y;
	touch_nr++;
}

/* Doubletap2wake main function */
static void detect_doubletap2wake(int x, int y, bool st)
{
        bool single_touch = st;
#if WG_DEBUG
        pr_info(LOGTAG"x,y(%4d,%4d) tap_time_pre:%llu\n",
                x, y, tap_time_pre);
#endif
	if (x < SWEEP_EDGE || x > sweep_x_limit)
		return;
	if (y < SWEEP_EDGE || y > sweep_y_limit)
		return;

	if ((single_touch) && (dt2w_switch) && (exec_count) && (touch_cnt)) {
		touch_cnt = false;
		if (touch_nr == 0) {
			new_touch(x, y);
		} else if (touch_nr == 1) {
			if ((calc_feather(x, x_pre) < DT2W_FEATHER) &&
			    (calc_feather(y, y_pre) < DT2W_FEATHER) &&
			    ((ktime_to_ms(ktime_get())-tap_time_pre) < DT2W_TIME))
				touch_nr++;
			else {
				doubletap2wake_reset();
				new_touch(x, y);
			}
		} else {
			doubletap2wake_reset();
			new_touch(x, y);
		}
		if ((touch_nr > 1)) {
			exec_count = false;
            input_report_key(wake_dev, KEY_GESTURE_DOUBLE_TAP, 1);
            input_sync(wake_dev);
            input_report_key(wake_dev, KEY_GESTURE_DOUBLE_TAP, 0);
            input_sync(wake_dev);
			doubletap2wake_reset();
		}
	}
}

/* Screen off gestures */
static void screen_off_gestures_reset(void) {

	exec_countx = true;
	barrierx[0] = false;
	barrierx[1] = false;
	firstx = 0;
	firstx_time = 0;

	exec_county = true;
	barriery[0] = false;
	barriery[1] = false;
	firsty = 0;
	firsty_time = 0;
}

/* Screen off gestures functions*/
static void detect_screen_off_gestures_v(int x, int y, bool st)
{
	int prevy = 0, nexty = 0;
    bool single_touch = st;

	if (firsty == 0) {
		firsty = y;
		firsty_time = ktime_to_ms(ktime_get());
	}

#if WG_DEBUG
        pr_info(LOGTAG"screen_off_gestures vert  x,y(%4d,%4d) single:%s\n",
                x, y, (single_touch) ? "true" : "false");
#endif

	//sweep up
	if (firsty > SWEEP_Y_START && single_touch && s2w_switch & SWEEP_UP) {
		prevy = firsty;
		nexty = prevy - SWEEP_Y_NEXT;
		if (barriery[0] == true || (y < prevy && y > nexty)) {
			prevy = nexty;
			nexty -= SWEEP_Y_NEXT;
			barriery[0] = true;
			if (barriery[1] == true || (y < prevy && y > nexty)) {
				prevy = nexty;
				barriery[1] = true;
				if (y < prevy) {
					if (y < (nexty - SWEEP_Y_NEXT)) {
						if (exec_county && (ktime_to_ms(ktime_get()) - firsty_time < SWEEP_TIMEOUT)) {
                            input_report_key(wake_dev, KEY_GESTURE_SWIPE_UP, 1);
                            input_sync(wake_dev);
                            input_report_key(wake_dev, KEY_GESTURE_SWIPE_UP, 0);
                            input_sync(wake_dev);		
							exec_county = false;
						}
					}
				}
			}
		}
	//sweep down
	} else if (firsty <= SWEEP_Y_START && single_touch && s2w_switch & SWEEP_DOWN) {
		prevy = firsty;
		nexty = prevy + SWEEP_Y_NEXT;
		if (barriery[0] == true || (y > prevy && y < nexty)) {
			prevy = nexty;
			nexty += SWEEP_Y_NEXT;
			barriery[0] = true;
			if (barriery[1] == true || (y > prevy && y < nexty)) {
				prevy = nexty;
				barriery[1] = true;
				if (y > prevy) {
					if (y > (nexty + SWEEP_Y_NEXT)) {
						if (exec_county && (ktime_to_ms(ktime_get()) - firsty_time < SWEEP_TIMEOUT)) {
                            input_report_key(wake_dev, KEY_GESTURE_SWIPE_DOWN, 1);
                            input_sync(wake_dev);
                            input_report_key(wake_dev, KEY_GESTURE_SWIPE_DOWN, 0);
                            input_sync(wake_dev);	
							exec_county = false;
						}
					}
				}
			}
		}
	}
	
}

static void detect_screen_off_gestures_h(int x, int y, bool st, bool scr_suspended)
{
        int prevx = 0, nextx = 0;
        bool single_touch = st;

	if (!scr_suspended && y < sweep_y_limit) {
		screen_off_gestures_reset();
		return;
	}

	if (firstx == 0) {
		firstx = x;
		firstx_time = ktime_to_ms(ktime_get());
	}

#if WG_DEBUG
        pr_info(LOGTAG"screen_off_gestures Horz x,y(%4d,%4d) wake:%s\n",
                x, y, (scr_suspended) ? "true" : "false");
#endif
	//left->right
	if (firstx < SWEEP_X_START && single_touch &&
			((scr_suspended && (s2w_switch & SWEEP_RIGHT)) ||
			(!scr_suspended && (s2s_switch & SWEEP_RIGHT)))) {
		prevx = 0;
		nextx = SWEEP_X_B1;
		if ((barrierx[0] == true) ||
		   ((x > prevx) && (x < nextx))) {
			prevx = nextx;
			nextx = SWEEP_X_B2;
			barrierx[0] = true;
			if ((barrierx[1] == true) ||
			   ((x > prevx) && (x < nextx))) {
				prevx = nextx;
				barrierx[1] = true;
				if (x > prevx) {
					if (x > (SWEEP_X_MAX - SWEEP_X_FINAL)) {
						if (exec_countx && (ktime_to_ms(ktime_get()) - firstx_time < SWEEP_TIMEOUT)) {
                            input_report_key(wake_dev, KEY_GESTURE_SWIPE_RIGHT, 1);
                            input_sync(wake_dev);
                            input_report_key(wake_dev, KEY_GESTURE_SWIPE_RIGHT, 0);
                            input_sync(wake_dev);
							exec_countx = false;
						}
					}
				}
			}
		}
	//right->left
	} else if (firstx >= SWEEP_X_START && single_touch &&
			((scr_suspended && (s2w_switch & SWEEP_LEFT)) ||
			(!scr_suspended && (s2s_switch & SWEEP_LEFT)))) {
		prevx = (SWEEP_X_MAX - SWEEP_X_FINAL);
		nextx = SWEEP_X_B2;
		if ((barrierx[0] == true) ||
		   ((x < prevx) && (x > nextx))) {
			prevx = nextx;
			nextx = SWEEP_X_B1;
			barrierx[0] = true;
			if ((barrierx[1] == true) ||
			   ((x < prevx) && (x > nextx))) {
				prevx = nextx;
				barrierx[1] = true;
				if (x < prevx) {
					if (x < SWEEP_X_FINAL) {
						if (exec_countx) {
                            input_report_key(wake_dev, KEY_GESTURE_SWIPE_LEFT, 1);
                            input_sync(wake_dev);
                            input_report_key(wake_dev, KEY_GESTURE_SWIPE_LEFT, 0);
                            input_sync(wake_dev);
						}
					}
				}
			}
		}
	}
}

static void screen_off_gestures_input_callback(struct work_struct *unused)
{
	detect_screen_off_gestures_h(touch_x, touch_y, true, is_suspended());
	if (is_suspended())
		detect_screen_off_gestures_v(touch_x, touch_y, true);

	return;
}

static void dt2w_input_callback(struct work_struct *unused)
{

	if (is_suspended() && dt2w_switch)
		detect_doubletap2wake(touch_x, touch_y, true);
	return;
}

static void wg_input_event(struct input_handle *handle, unsigned int type,
				unsigned int code, int value)
{
	if (is_suspended() && code == ABS_MT_POSITION_X) {
		value -= 5000;
	}
	
#if WG_DEBUG
	pr_info("wg: code: %s|%u, val: %i\n",
		((code==ABS_MT_POSITION_X) ? "X" :
		(code==ABS_MT_POSITION_Y) ? "Y" :
		(code==ABS_MT_TRACKING_ID) ? "ID" :
		"undef"), code, value);
#endif

	if (code == ABS_MT_SLOT) {
		screen_off_gestures_reset();
		doubletap2wake_reset();
		return;
	}

	if (code == ABS_MT_TRACKING_ID && value == -1) {
		screen_off_gestures_reset();
		touch_cnt = true;
		queue_work_on(0, dt2w_input_wq, &dt2w_input_work);
		return;
	}

	if (code == ABS_MT_POSITION_X) {
		touch_x = value;
		touch_x_called = true;
	}

	if (code == ABS_MT_POSITION_Y) {
		touch_y = value;
		touch_y_called = true;
	}

	if (touch_x_called && touch_y_called) {
		touch_x_called = false;
		touch_y_called = false;
		queue_work_on(0, screen_off_gestures_input_wq, &screen_off_gestures_input_work);
	} else if (!is_suspended() && touch_x_called && !touch_y_called) {
		touch_x_called = false;
		touch_y_called = false;
		queue_work_on(0, screen_off_gestures_input_wq, &screen_off_gestures_input_work);
	}
}

static int input_dev_filter(struct input_dev *dev) {
	if (strstr(dev->name, "synaptics_dsx_i2c")) {
		return 0;
	} else {
		return 1;
	}
}

static int wg_input_connect(struct input_handler *handler,
				struct input_dev *dev, const struct input_device_id *id) {
	struct input_handle *handle;
	int error;

	if (input_dev_filter(dev))
		return -ENODEV;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "wg";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void wg_input_disconnect(struct input_handle *handle) {
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id wg_ids[] = {
	{ .driver_info = 1 },
	{ },
};

static struct input_handler wg_input_handler = {
	.event		= wg_input_event,
	.connect	= wg_input_connect,
	.disconnect	= wg_input_disconnect,
	.name		= "wg_inputreq",
	.id_table	= wg_ids,
};


/*
 * SYSFS stuff below here
 */

static ssize_t doubletap2wake_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", dt2w_switch);

	return count;
}

static ssize_t doubletap2wake_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d ", &dt2w_switch);
	if (dt2w_switch < 0 || dt2w_switch > 1)
		dt2w_switch = 0;
		
	return count;
}

static DEVICE_ATTR(doubletap2wake, (S_IWUSR|S_IRUGO),
	doubletap2wake_show, doubletap2wake_dump);

static ssize_t gesture_swipe_right_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", gesture_swipe_right);

	return count;
}

static ssize_t gesture_swipe_right_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d ", &gesture_swipe_right);
	if (gesture_swipe_right < 0 || gesture_swipe_right > 200)
		gesture_swipe_right = 0;

	return count;
}

static DEVICE_ATTR(gesture_swipe_right, (S_IWUSR|S_IRUGO),
	gesture_swipe_right_show, gesture_swipe_right_dump);

static ssize_t gesture_swipe_left_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", gesture_swipe_left);

	return count;
}

static ssize_t gesture_swipe_left_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d ", &gesture_swipe_left);
	if (gesture_swipe_left < 0 || gesture_swipe_left > 200)
		gesture_swipe_left = 0;

	return count;
}

static DEVICE_ATTR(gesture_swipe_left, (S_IWUSR|S_IRUGO),
	gesture_swipe_left_show, gesture_swipe_left_dump);

static ssize_t gesture_swipe_down_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", gesture_swipe_down);

	return count;
}

static ssize_t gesture_swipe_down_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d ", &gesture_swipe_down);
	if (gesture_swipe_down < 0 || gesture_swipe_down > 200)
		gesture_swipe_down = 0;

	return count;
}

static DEVICE_ATTR(gesture_swipe_down, (S_IWUSR|S_IRUGO),
	gesture_swipe_down_show, gesture_swipe_down_dump);

static ssize_t gesture_swipe_up_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", gesture_swipe_up);

	return count;
}

static ssize_t gesture_swipe_up_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d ", &gesture_swipe_up);
	if (gesture_swipe_up < 0 || gesture_swipe_up > 200)
		gesture_swipe_up = 0;

	return count;
}

static DEVICE_ATTR(gesture_swipe_up, (S_IWUSR|S_IRUGO),
	gesture_swipe_up_show, gesture_swipe_up_dump);

/*
 * INIT / EXIT stuff below here
 */

struct kobject *android_touch_kobj;
EXPORT_SYMBOL_GPL(android_touch_kobj);

static int __init screen_off_gestures_init(void)
{
	int rc = 0;

	wake_dev = input_allocate_device();
	if (!wake_dev) {
		pr_err("Failed to allocate wake_dev\n");
		goto err_alloc_dev;
	}

	input_set_capability(wake_dev, EV_KEY, KEY_GESTURE_SWIPE_RIGHT);
	input_set_capability(wake_dev, EV_KEY, KEY_GESTURE_SWIPE_LEFT);
	input_set_capability(wake_dev, EV_KEY, KEY_GESTURE_SWIPE_DOWN);
	input_set_capability(wake_dev, EV_KEY, KEY_GESTURE_SWIPE_UP);
	input_set_capability(wake_dev, EV_KEY, KEY_GESTURE_DOUBLE_TAP);
    wake_dev->name = "wg_pwrkey";
    wake_dev->phys = "wg_pwrkey/input0";

	rc = input_register_device(wake_dev);
	if (rc) {
		pr_err("%s: input_register_device err=%d\n", __func__, rc);
		goto err_input_dev;
	}

	rc = input_register_handler(&wg_input_handler);
	if (rc)
		pr_err("%s: Failed to register wg_input_handler\n", __func__);

	screen_off_gestures_input_wq = create_workqueue("screen_off_gesturesiwq");
	if (!screen_off_gestures_input_wq) {
		pr_err("%s: Failed to create workqueue\n", __func__);
		return -EFAULT;
	}
	INIT_WORK(&screen_off_gestures_input_work, screen_off_gestures_input_callback);
		
	dt2w_input_wq = create_workqueue("dt2wiwq");
	if (!dt2w_input_wq) {
		pr_err("%s: Failed to create dt2wiwq workqueue\n", __func__);
		return -EFAULT;
	}
	INIT_WORK(&dt2w_input_work, dt2w_input_callback);
		

	android_touch_kobj = kobject_create_and_add("android_touch", NULL) ;
	if (android_touch_kobj == NULL) {
		pr_warn("%s: android_touch_kobj create_and_add failed\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_gesture_swipe_right.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for gesture_swipe_right\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_gesture_swipe_left.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for gesture_swipe_left\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_gesture_swipe_down.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for gesture_swipe_down\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_gesture_swipe_up.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for gesture_swipe_up\n", __func__);
	}
    rc = sysfs_create_file(android_touch_kobj, &dev_attr_doubletap2wake.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for doubletap2wake\n", __func__);
	}

	return 0;

err_input_dev:
	input_free_device(wake_dev);
err_alloc_dev:
	return 0;
}

static void __exit screen_off_gestures_exit(void)
{
	kobject_del(android_touch_kobj);
	input_unregister_handler(&wg_input_handler);
	destroy_workqueue(screen_off_gestures_input_wq);
	destroy_workqueue(dt2w_input_wq);
	input_unregister_device(wake_dev);
	input_free_device(wake_dev);
	return;
}

module_init(screen_off_gestures_init);
module_exit(screen_off_gestures_exit);

