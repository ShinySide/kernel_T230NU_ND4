/* include/linux/sec-debug.h
 *
 * Copyright (C) 2013 Samsung Electronics Co, Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#ifndef SEC_DEBUG_H
#define SEC_DEBUG_H

#include <linux/sched.h>

#ifdef CONFIG_SEC_DEBUG

union sec_debug_level_t {
	struct {
		u16 kernel_fault;
		u16 user_fault;
	} en;
	u32 uint_val;
};

struct sec_log_buffer {
	u32     sig;
	u32     start;
	u32     size;
	u8      data[0];
};

extern int sec_crash_key_panic;
extern union sec_debug_level_t sec_debug_level;

extern int sec_debug_init(void);
extern void sec_debug_check_crash_key(unsigned int code, int value);
extern int sec_debug_panic_dump(char *buf);
extern void sec_getlog_supply_loggerinfo(unsigned char *buffer, 
		const char *name);
extern void sec_getlog_supply_fbinfo(void *p_fb, u32 xres, u32 yres, u32 bpp,
		u32 frames);
extern void sec_gaf_supply_rqinfo(unsigned short curr_offset,
		unsigned short rq_offset);

#endif

#ifdef CONFIG_SEC_LOG
extern void sec_getlog_supply_kloginfo(void *klog_buf);
extern void register_log_char_hook();
#endif

struct worker;
struct work_struct;

#ifdef CONFIG_SEC_DEBUG_SCHED_LOG
extern void __sec_debug_task_log(int cpu, struct task_struct *task);
extern void __sec_debug_irq_log(unsigned int irq, void *fn, int en);
extern void __sec_debug_work_log(struct worker *worker,
			struct work_struct *work, work_func_t f, int en);
#ifdef CONFIG_SEC_DEBUG_TIMER_LOG
extern void __sec_debug_timer_log(unsigned int type, void *fn);
#endif

static inline void sec_debug_task_log(int cpu, struct task_struct *task)
{
	if (unlikely(sec_debug_level.en.kernel_fault))
		__sec_debug_task_log(cpu, task);
}

static inline void sec_debug_irq_log(unsigned int irq, void *fn, int en)
{
	if (unlikely(sec_debug_level.en.kernel_fault))
		__sec_debug_irq_log(irq, fn, en);
}

static inline void sec_debug_work_log(struct worker *worker,
			struct work_struct *work, work_func_t f, int en)
{
	if (unlikely(sec_debug_level.en.kernel_fault))
		__sec_debug_work_log(worker, work, f, en);
}

#ifdef CONFIG_SEC_DEBUG_TIMER_LOG
static inline void sec_debug_timer_log(unsigned int type, void *fn)
{
	if (unlikely(sec_debug_level.en.kernel_fault))
		__sec_debug_timer_log(type, fn);
}
#else
static inline void sec_debug_timer_log(unsigned int type, void *fn)
{
}
#endif

#ifdef CONFIG_SEC_DEBUG_SOFTIRQ_LOG
static inline void sec_debug_softirq_log(unsigned int irq, void *fn, int en)
{
	if (unlikely(sec_debug_level.en.kernel_fault))
		__sec_debug_irq_log(irq, fn, en);
}
#else
static inline void sec_debug_softirq_log(unsigned int irq, void *fn, int en)
{
}
#endif
#endif
#endif				/* SEC_DEBUG_H */
