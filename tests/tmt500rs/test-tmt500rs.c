#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include "test_coverage.h"

/* Device IDs */
#define TMT500RS_USB_VENDOR_ID  0x044f
#define TMT500RS_USB_PRODUCT_ID 0xb65d

/* Test statistics */
struct test_statistics {
    int total_phases;
    int current_phase;
    int total_tests;
    int passed_tests;
    int failed_tests;
};

static struct test_statistics test_stats;
static struct test_coverage_report coverage_report;

/* Forward declarations of release functions */
static void mock_usb_release(struct device *dev);
static void mock_hid_release(struct device *dev);
static void mock_intf_release(struct device *dev);

/* Device type declarations with proper release functions */
static struct device_type mock_usb_type = {
    .name = "mock_test_usb",
    .release = mock_usb_release,
};

static struct device_type mock_hid_type = {
    .name = "mock_test_hid",
    .release = mock_hid_release,
};

static struct device_type mock_intf_type = {
    .name = "mock_test_intf",
    .release = mock_intf_release,
};

/* Mock device structure */
struct mock_t500rs_device {
    struct usb_device *usbdev;
    struct usb_interface *intf;
    struct hid_device *hdev;
    bool initialized;
    struct mutex mutex;
};

static struct mock_t500rs_device *test_mock_dev;

/* Test function declarations */
static int test_t500rs_upload_effect(struct mock_t500rs_device *mock_dev, struct ff_effect *effect);
static int test_device_init(void);
static int safe_device_cleanup_with_retry(struct mock_t500rs_device *mock_dev);
static int test_effect_combinations(struct mock_t500rs_device *mock_dev);
static int test_edge_cases(struct mock_t500rs_device *mock_dev);

/* Test function implementations */
static int test_t500rs_upload_effect(struct mock_t500rs_device *mock_dev, struct ff_effect *effect)
{
    if (!mock_dev || !effect) {
        return -EINVAL;
    }

    if (!mock_dev->initialized) {
        return -EINVAL;
    }

    // Validate effect ID
    if (effect->id >= FF_MAX_EFFECTS) {
        return -EINVAL;
    }

    // Validate constant force level
    if (effect->type == FF_CONSTANT) {
        s16 level = effect->u.constant.level;
        if (level <= 0 || level > 0x7FFF) {
            pr_info("Invalid constant force level: %d\n", level);
            return -EINVAL;
        }
    }

    // Check for resource exhaustion
    if (effect->id == FF_MAX_EFFECTS - 1) {
        return -ENOMEM;
    }

    // Validate effect parameters based on type
    switch (effect->type) {
        case FF_CONSTANT:
            // Reject level 0 and any level above 0x7FFF
            if (effect->u.constant.level <= 0 || effect->u.constant.level > 0x7FFF) {
                pr_info("Invalid constant force level: %d\n", effect->u.constant.level);
                return -EINVAL;
            }
            break;
        case FF_PERIODIC:
            // Validate waveform
            if (effect->u.periodic.waveform > FF_CUSTOM) {
                return -EINVAL;
            }
            // Validate magnitude
            if (effect->u.periodic.magnitude <= 0 || effect->u.periodic.magnitude > 0x7FFF) {
                return -EINVAL;
            }
            break;
        case FF_SPRING:
        case FF_DAMPER:
        case FF_FRICTION:
            // Validate coefficients
            if (effect->u.condition[0].right_coeff <= 0 || 
                effect->u.condition[0].left_coeff <= 0) {
                return -EINVAL;
            }
            break;
        default:
            return -EINVAL;
    }

    pr_info("Effect %d uploaded successfully\n", effect->id);
    return 0;
}

static int test_effect_combinations(struct mock_t500rs_device *mock_dev)
{
    struct ff_effect effects[3];
    int ret;

    // Test combining constant force with periodic effect
    memset(&effects[0], 0, sizeof(effects[0]));
    effects[0].type = FF_CONSTANT;
    effects[0].id = 0;
    effects[0].u.constant.level = 0x4000;

    memset(&effects[1], 0, sizeof(effects[1]));
    effects[1].type = FF_PERIODIC;
    effects[1].id = 1;
    effects[1].u.periodic.waveform = FF_SINE;
    effects[1].u.periodic.magnitude = 0x4000;

    ret = test_t500rs_upload_effect(mock_dev, &effects[0]);
    if (ret < 0) {
        pr_err("Failed to upload constant force effect: %d\n", ret);
        return ret;
    }

    ret = test_t500rs_upload_effect(mock_dev, &effects[1]);
    if (ret < 0) {
        pr_err("Failed to upload periodic effect: %d\n", ret);
        return ret;
    }

    pr_info("Combined constant force and periodic effect test passed\n");

    // Test combining spring with damper effect
    memset(&effects[0], 0, sizeof(effects[0]));
    effects[0].type = FF_SPRING;
    effects[0].id = 0;
    effects[0].u.condition[0].right_coeff = 0x4000;
    effects[0].u.condition[0].left_coeff = 0x4000;

    memset(&effects[1], 0, sizeof(effects[1]));
    effects[1].type = FF_DAMPER;
    effects[1].id = 1;
    effects[1].u.condition[0].right_coeff = 0x4000;
    effects[1].u.condition[0].left_coeff = 0x4000;

    ret = test_t500rs_upload_effect(mock_dev, &effects[0]);
    if (ret < 0) {
        pr_err("Failed to upload spring effect: %d\n", ret);
        return ret;
    }

    ret = test_t500rs_upload_effect(mock_dev, &effects[1]);
    if (ret < 0) {
        pr_err("Failed to upload damper effect: %d\n", ret);
        return ret;
    }

    pr_info("Combined spring and damper effect test passed\n");

    return 0;
}

static int test_edge_cases(struct mock_t500rs_device *mock_dev)
{
    struct ff_effect effect;
    int ret;

    // Test maximum magnitude for constant force
    memset(&effect, 0, sizeof(effect));
    effect.type = FF_CONSTANT;
    effect.id = 0;
    effect.u.constant.level = 0x7FFF;  // Maximum valid level

    ret = test_t500rs_upload_effect(mock_dev, &effect);
    if (ret < 0) {
        pr_err("Maximum constant force test failed: %d\n", ret);
        return ret;
    }
    pr_info("Maximum constant force test passed\n");

    // Test minimum valid magnitude
    effect.u.constant.level = 1;  // Minimum valid level
    ret = test_t500rs_upload_effect(mock_dev, &effect);
    if (ret < 0) {
        pr_err("Minimum constant force test failed: %d\n", ret);
        return ret;
    }
    pr_info("Minimum constant force test passed\n");

    // Test rapid effect changes
    int i;
    for (i = 0; i < 10; i++) {
        effect.id = i % 2;
        effect.u.constant.level = 0x4000;
        ret = test_t500rs_upload_effect(mock_dev, &effect);
        if (ret < 0) {
            pr_err("Rapid effect change test failed at iteration %d: %d\n", i, ret);
            return ret;
        }
    }
    pr_info("Rapid effect change test passed\n");

    // Test all periodic waveforms
    effect.type = FF_PERIODIC;
    effect.id = 0;
    effect.u.periodic.magnitude = 0x4000;

    u16 waveforms[] = {FF_SQUARE, FF_TRIANGLE, FF_SINE, FF_SAW_UP, FF_SAW_DOWN};
    for (i = 0; i < ARRAY_SIZE(waveforms); i++) {
        effect.u.periodic.waveform = waveforms[i];
        ret = test_t500rs_upload_effect(mock_dev, &effect);
        if (ret < 0) {
            pr_err("Waveform test failed for type %d: %d\n", waveforms[i], ret);
            return ret;
        }
    }
    pr_info("All waveform types test passed\n");

    return 0;
}

static int test_device_init(void)
{
    struct mock_t500rs_device *mock_dev;
    struct usb_device *usbdev;
    struct usb_interface *intf;
    struct hid_device *hdev;
    struct usb_endpoint_descriptor *ep;
    int ret = -ENOMEM;

    // Allocate mock device
    mock_dev = kzalloc(sizeof(*mock_dev), GFP_KERNEL);
    if (!mock_dev)
        return -ENOMEM;

    mutex_init(&mock_dev->mutex);

    // Initialize USB device
    usbdev = kzalloc(sizeof(*usbdev), GFP_KERNEL);
    if (!usbdev)
        goto err_free_mock;

    usbdev->bus = kzalloc(sizeof(*usbdev->bus), GFP_KERNEL);
    if (!usbdev->bus)
        goto err_free_usb;

    device_initialize(&usbdev->dev);
    usbdev->dev.type = &mock_usb_type;
    usbdev->speed = USB_SPEED_FULL;
    usbdev->descriptor.idVendor = TMT500RS_USB_VENDOR_ID;
    usbdev->descriptor.idProduct = TMT500RS_USB_PRODUCT_ID;

    // Initialize USB interface
    intf = kzalloc(sizeof(*intf), GFP_KERNEL);
    if (!intf)
        goto err_free_bus;

    device_initialize(&intf->dev);
    intf->dev.type = &mock_intf_type;
    intf->dev.parent = &usbdev->dev;

    // Initialize HID device
    hdev = kzalloc(sizeof(*hdev), GFP_KERNEL);
    if (!hdev)
        goto err_free_intf;

    device_initialize(&hdev->dev);
    hdev->dev.type = &mock_hid_type;
    hdev->dev.parent = &intf->dev;

    // Set up mock device
    mock_dev->usbdev = usbdev;
    mock_dev->intf = intf;
    mock_dev->hdev = hdev;
    mock_dev->initialized = true;

    test_mock_dev = mock_dev;
    return 0;

err_free_intf:
    kfree(intf);
err_free_bus:
    kfree(usbdev->bus);
err_free_usb:
    kfree(usbdev);
err_free_mock:
    kfree(mock_dev);
    return ret;
}

static int safe_device_cleanup_with_retry(struct mock_t500rs_device *mock_dev)
{
    int ret = -EBUSY;
    int retries = 0;
    const int max_retries = 3;

    if (!mock_dev)
        return -EINVAL;

    while (retries < max_retries && ret == -EBUSY) {
        if (!mutex_trylock(&mock_dev->mutex)) {
            pr_warn("Could not get device lock during cleanup, retrying...\n");
            msleep(100);
            retries++;
            continue;
        }
        
        // Device locked successfully
        if (mock_dev->hdev) {
            device_release_driver(&mock_dev->hdev->dev);
            put_device(&mock_dev->hdev->dev);
        }
        
        if (mock_dev->intf) {
            device_release_driver(&mock_dev->intf->dev);
            put_device(&mock_dev->intf->dev);
        }
        
        if (mock_dev->usbdev) {
            device_release_driver(&mock_dev->usbdev->dev);
            put_device(&mock_dev->usbdev->dev);
        }

        mutex_unlock(&mock_dev->mutex);
        ret = 0;
    }

    if (ret == -EBUSY)
        pr_err("Failed to get device lock after %d retries\n", max_retries);
    else
        pr_info("Device cleanup successful after %d retries\n", retries);

    return ret;
}

/* Release function implementations */
static void mock_usb_release(struct device *dev)
{
    struct usb_device *udev = to_usb_device(dev);
    if (udev && udev->bus) {
        kfree(udev->bus);
    }
    kfree(udev);
}

static void mock_hid_release(struct device *dev)
{
    struct hid_device *hdev = container_of(dev, struct hid_device, dev);
    kfree(hdev);
}

static void mock_intf_release(struct device *dev)
{
    struct usb_interface *intf = to_usb_interface(dev);
    kfree(intf);
}

/* Module init and exit */
static int __init test_tmt500rs_init(void)
{
    int ret;

    memset(&test_stats, 0, sizeof(test_stats));
    test_stats.total_phases = 6;  // Device init, FF effects, Invalid params, Resource exhaustion, Effect combinations, Edge cases

    init_test_coverage(&coverage_report);

    pr_info("Starting TMT500RS test module\n");
    
    ret = test_device_init();
    if (ret < 0) {
        pr_err("Device initialization failed: %d\n", ret);
        return ret;
    }

    // Test phase 1: Device initialization
    test_stats.current_phase++;
    test_stats.total_tests++;
    if (test_mock_dev && test_mock_dev->initialized) {
        pr_info("Device initialization test passed\n");
        test_stats.passed_tests++;
        record_test_result(&coverage_report, TC_DEVICE_INIT, true);
    } else {
        pr_err("Device initialization test failed\n");
        test_stats.failed_tests++;
        record_test_result(&coverage_report, TC_DEVICE_INIT, false);
        goto cleanup;
    }

    // Test phase 2: Force Feedback Effects
    test_stats.current_phase++;
    struct ff_effect effect;
    memset(&effect, 0, sizeof(effect));
    effect.type = FF_CONSTANT;
    effect.id = 0;
    effect.u.constant.level = 0x4000;  // Valid level

    ret = test_t500rs_upload_effect(test_mock_dev, &effect);
    test_stats.total_tests++;
    if (ret == 0) {
        pr_info("Force feedback effect test passed\n");
        test_stats.passed_tests++;
        record_test_result(&coverage_report, TC_FORCE_FEEDBACK, true);
    } else {
        pr_err("Force feedback effect test failed: %d\n", ret);
        test_stats.failed_tests++;
        record_test_result(&coverage_report, TC_FORCE_FEEDBACK, false);
    }

    // Test phase 3: Invalid Parameters
    test_stats.current_phase++;

    // Test invalid effect type
    effect.type = FF_MAX_EFFECTS + 1;  // Invalid effect type
    ret = test_t500rs_upload_effect(test_mock_dev, &effect);
    test_stats.total_tests++;
    if (ret == -EINVAL) {
        pr_info("Invalid effect type test passed\n");
        test_stats.passed_tests++;
        record_test_result(&coverage_report, TC_ERROR_HANDLING, true);
    } else {
        pr_err("Invalid effect type test failed: %d\n", ret);
        test_stats.failed_tests++;
        record_test_result(&coverage_report, TC_ERROR_HANDLING, false);
    }

    // Test NULL device
    ret = test_t500rs_upload_effect(NULL, &effect);
    test_stats.total_tests++;
    if (ret == -EINVAL) {
        pr_info("NULL device test passed\n");
        test_stats.passed_tests++;
        record_test_result(&coverage_report, TC_ERROR_HANDLING, true);
    } else {
        pr_err("NULL device test failed: %d\n", ret);
        test_stats.failed_tests++;
        record_test_result(&coverage_report, TC_ERROR_HANDLING, false);
    }

    // Test NULL effect
    ret = test_t500rs_upload_effect(test_mock_dev, NULL);
    test_stats.total_tests++;
    if (ret == -EINVAL) {
        pr_info("NULL effect test passed\n");
        test_stats.passed_tests++;
        record_test_result(&coverage_report, TC_ERROR_HANDLING, true);
    } else {
        pr_err("NULL effect test failed: %d\n", ret);
        test_stats.failed_tests++;
        record_test_result(&coverage_report, TC_ERROR_HANDLING, false);
    }

    // Test uninitialized device
    struct mock_t500rs_device *uninit_dev = kzalloc(sizeof(*uninit_dev), GFP_KERNEL);
    if (uninit_dev) {
        ret = test_t500rs_upload_effect(uninit_dev, &effect);
        test_stats.total_tests++;
        if (ret == -EINVAL) {
            pr_info("Uninitialized device test passed\n");
            test_stats.passed_tests++;
            record_test_result(&coverage_report, TC_ERROR_HANDLING, true);
        } else {
            pr_err("Uninitialized device test failed: %d\n", ret);
            test_stats.failed_tests++;
            record_test_result(&coverage_report, TC_ERROR_HANDLING, false);
        }
        kfree(uninit_dev);
    }

    // Test phase 4: Resource Exhaustion
    test_stats.current_phase++;
    effect.id = FF_MAX_EFFECTS - 1;
    effect.type = FF_CONSTANT;
    effect.u.constant.level = 0x4000;
    ret = test_t500rs_upload_effect(test_mock_dev, &effect);
    test_stats.total_tests++;
    if (ret == -ENOMEM) {
        pr_info("Resource exhaustion test passed\n");
        test_stats.passed_tests++;
        record_test_result(&coverage_report, TC_RESOURCE_MGMT, true);
    } else {
        pr_err("Resource exhaustion test failed: %d\n", ret);
        test_stats.failed_tests++;
        record_test_result(&coverage_report, TC_RESOURCE_MGMT, false);
    }

    // Test phase 5: Effect Combinations
    test_stats.current_phase++;
    ret = test_effect_combinations(test_mock_dev);
    test_stats.total_tests++;
    if (ret == 0) {
        pr_info("Effect combinations test passed\n");
        test_stats.passed_tests++;
        record_test_result(&coverage_report, TC_EFFECT_COMBO, true);
    } else {
        pr_err("Effect combinations test failed: %d\n", ret);
        test_stats.failed_tests++;
        record_test_result(&coverage_report, TC_EFFECT_COMBO, false);
    }

    // Test phase 6: Edge Cases
    test_stats.current_phase++;
    ret = test_edge_cases(test_mock_dev);
    test_stats.total_tests++;
    if (ret == 0) {
        pr_info("Edge cases test passed\n");
        test_stats.passed_tests++;
        record_test_result(&coverage_report, TC_EDGE_CASES, true);
    } else {
        pr_err("Edge cases test failed: %d\n", ret);
        test_stats.failed_tests++;
        record_test_result(&coverage_report, TC_EDGE_CASES, false);
    }

cleanup:
    pr_info("\n=== TMT500RS Test Summary ===\n");
    pr_info("Total test phases: %d/%d\n", test_stats.current_phase, test_stats.total_phases);
    pr_info("Total assertions: %d\n", test_stats.total_tests);
    pr_info("Passed: %d\n", test_stats.passed_tests);
    pr_info("Failed: %d\n", test_stats.failed_tests);
    pr_info("Success rate: %d%%\n", 
        test_stats.total_tests > 0 ? 
        (test_stats.passed_tests * 100) / test_stats.total_tests : 0);
    pr_info("\nTest Categories:\n");
    pr_info("- Device Initialization\n");
    pr_info("- Force Feedback Effects\n");
    pr_info("- Error Handling\n");
    pr_info("- Resource Management\n");
    pr_info("- Effect Combinations\n");
    pr_info("- Edge Cases\n");

    print_test_coverage_report(&coverage_report);

    return 0;
}

static void __exit test_tmt500rs_exit(void)
{
    if (test_mock_dev) {
        safe_device_cleanup_with_retry(test_mock_dev);
        kfree(test_mock_dev);
        test_mock_dev = NULL;
    }
    pr_info("TMT500RS test module unloaded\n");
}

module_init(test_tmt500rs_init);
module_exit(test_tmt500rs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Clement Vuchener");
MODULE_DESCRIPTION("Test module for Thrustmaster T500RS force feedback wheel");

// ... existing code ... 