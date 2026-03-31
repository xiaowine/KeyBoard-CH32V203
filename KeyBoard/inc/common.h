#ifndef KEYBOARD_COMMON_H
#define KEYBOARD_COMMON_H

/* 常用属性宏，供多个头文件共享，避免循环包含 */
#define INTF __attribute__((interrupt("WCH-Interrupt-fast")))
#define PACKED __attribute__((packed))
#define RAM __attribute__((section(".ramfunc")))
#define AL4 __attribute__((aligned(4)))
#define USED __attribute__((used))
#define LINK_AND_AL(x, y) __attribute__((section(x), used, aligned(y)))
#define CEIL_DIV(x, y) (((x) + (y) - 1) / (y))
#define CLAMP_INPLACE(var, lo, hi) \
  do                               \
  {                                \
    if ((var) > (hi))              \
      (var) = (hi);                \
    else if ((var) < (lo))         \
      (var) = (lo);                \
  } while (0)

#endif
