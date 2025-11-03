obj-m := hid_tmff_new.o

hid_tmff_new-objs := src/hid-tmff2.o \
                     src/tmt300rs/hid-tmt300rs.o \
                     src/tmt248/hid-tmt248.o \
                     src/tmtx/hid-tmtx.o \
                     src/tmtsxw/hid-tmtsxw.o \
                     src/tmt500rs/hid-tmt500rs-usb.o

ccflags-y := -I$(src)/src
ccflags-y += $(T500RS_VERSION_DEF)

