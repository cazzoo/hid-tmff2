#include <linux/kernel.h>
#include <linux/hid.h>
#include <linux/device.h>
#include <linux/stdarg.h>
#include <linux/module.h>
#include "hid-tmt500rs.h"
#include "hid-tmt500rs-utils.h"

void t500rs_dbg(struct t500rs_device_entry *t500rs, const char *fmt, ...)
{
    if ((t500rs) && (t500rs)->data && (t500rs)->data->hdev) {
        struct va_format vaf;
        va_list args;

        va_start(args, fmt);
        vaf.fmt = fmt;
        vaf.va = &args;
        dev_dbg(&(t500rs)->data->hdev->dev, "T500RS: %pV\n", &vaf);
        va_end(args);
    }
}
EXPORT_SYMBOL_GPL(t500rs_dbg);

void t500rs_info(struct t500rs_device_entry *t500rs, const char *fmt, ...)
{
    if ((t500rs) && (t500rs)->data && (t500rs)->data->hdev) {
        struct va_format vaf;
        va_list args;

        va_start(args, fmt);
        vaf.fmt = fmt;
        vaf.va = &args;
        dev_info(&(t500rs)->data->hdev->dev, "T500RS: %pV\n", &vaf);
        va_end(args);
    }
}
EXPORT_SYMBOL_GPL(t500rs_info);

void t500rs_err(struct t500rs_device_entry *t500rs, const char *fmt, ...)
{
    if ((t500rs) && (t500rs)->data && (t500rs)->data->hdev) {
        struct va_format vaf;
        va_list args;

        va_start(args, fmt);
        vaf.fmt = fmt;
        vaf.va = &args;
        dev_err(&(t500rs)->data->hdev->dev, "T500RS: %pV\n", &vaf);
        va_end(args);
    }
}
EXPORT_SYMBOL_GPL(t500rs_err); 