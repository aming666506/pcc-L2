

#ifndef COMMON_DEVICE_UTILS_H_
#define COMMON_DEVICE_UTILS_H_

#ifndef likely
#define likely(x) __builtin_expect((x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif

#endif /* COMMON_DEVICE_UTILS_H_ */
