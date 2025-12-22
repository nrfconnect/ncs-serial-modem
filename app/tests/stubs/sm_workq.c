#include <zephyr/kernel.h>

/* Work queue needed by sm_at_host and other modules */
struct k_work_q sm_work_q;

#define MY_STACK_SIZE 20480

K_THREAD_STACK_DEFINE(my_stack_area, MY_STACK_SIZE);

int start_my_work_queue(void)
{
	static const struct k_work_queue_config cfg = {
		.name = "sm_work_q",
		.essential = true,
	};

	k_work_queue_init(&sm_work_q);

	k_work_queue_start(&sm_work_q, my_stack_area, K_THREAD_STACK_SIZEOF(my_stack_area),
			   K_LOWEST_APPLICATION_THREAD_PRIO, &cfg);
	return 0;
}
SYS_INIT(start_my_work_queue, APPLICATION, 0);
