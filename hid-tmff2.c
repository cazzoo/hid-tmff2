#include <linux/delay.h>

static int tmff2_init(struct hid_device *hdev)
{
    struct tmff2_device *tmff2;
    int error;

    tmff2 = kzalloc(sizeof(*tmff2), GFP_KERNEL);
    if (!tmff2)
        return -ENOMEM;

    tmff2->hdev = hdev;
    hid_set_drvdata(hdev, tmff2);

    // Add delay before initialization to allow device to stabilize
    msleep(100);

    error = hid_parse(hdev);
    if (error) {
        hid_err(hdev, "parse failed\n");
        goto err_free;
    }

    error = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
    if (error) {
        hid_err(hdev, "hw start failed\n");
        goto err_free;
    }

    // Add delay after HID initialization
    msleep(100);

    error = tmff2_setup_ff(tmff2);
    if (error) {
        hid_err(hdev, "force feedback setup failed\n");
        goto err_stop;
    }

    // Add delay after force feedback setup
    msleep(100);

    return 0;

err_stop:
    hid_hw_stop(hdev);
err_free:
    kfree(tmff2);
    return error;
} 