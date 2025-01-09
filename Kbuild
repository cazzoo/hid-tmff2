obj-m := hid-tmff-new.o test-tmt500rs.o

hid-tmff-new-objs := src/hid-tmff2.o \
                     src/tmt300rs/hid-tmt300rs.o \
                     src/tmt248/hid-tmt248.o \
                     src/tmtx/hid-tmtx.o \
                     src/tmtsxw/hid-tmtsxw.o \
                     src/tmt500rs/hid-tmt500rs.o \
                     src/tmt500rs/hid-tmt500rs-usb.o \
                     src/tmt500rs/hid-tmt500rs-ff.o \
                     src/tmt500rs/hid-tmt500rs-init.o

test-tmt500rs-objs := tests/tmt500rs/test-tmt500rs.o \
                      src/tmt500rs/hid-tmt500rs.o \
                      src/tmt500rs/hid-tmt500rs-usb.o \
                      src/tmt500rs/hid-tmt500rs-ff.o \
                      src/tmt500rs/hid-tmt500rs-init.o

ccflags-y := -I$(src)/src
