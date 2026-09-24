#ifndef LIBUSB_CONFIG_H
#define LIBUSB_CONFIG_H

#define PACKAGE "libusb"
#define PACKAGE_VERSION "1.0.27"
#define VERSION "1.0.27"

#define HAVE_GETTIMEOFDAY 1
#define HAVE_POLL_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_NANOSLEEP 1
#define HAVE_PTHREAD_CONDATTR_SETCLOCK 1
#define HAVE_PTHREAD_SETNAME_NP 1
#define HAVE_ASM_TYPES_H 1
#define HAVE_LINUX_FILTER_H 1
#define HAVE_LINUX_NETLINK_H 1

#define ENABLE_LOGGING 1
#define USE_USBDK 0

#define DEFAULT_VISIBILITY __attribute__((visibility("default")))

#endif
