/*
 * ksm - a really simple and fast x64 hypervisor
 * Copyright (C) 2016, 2017 Ahmed Samy <asamy@protonmail.com>
 *
 * Public domain
 */
#ifdef __linux__
#include <linux/cpu.h>
#else
#include <ntddk.h>
#endif

#include "ksm.h"
#include "compiler.h"

#ifdef __linux__
static inline void do_cpu(void *v)
{
	int(*f) (struct ksm *) = v;
	int ret = f(ksm);

	KSM_DEBUG("On CPU calling %d\n", ret);
}

static int ksm_hotplug_cpu_online(unsigned int cpu)
{
	get_online_cpus();
	if(cpu_online(cpu))
		smp_call_function_single(cpu, do_cpu, __ksm_init_cpu, 1);
	put_online_cpus();

	return 0;
}

static int ksm_hotplug_cpu_teardown(unsigned int cpu)
{
	smp_call_function_single(cpu, do_cpu, __ksm_exit_cpu, 1);

	return 0;
}

static enum cpuhp_state hotplug_cpu_state = CPUHP_OFFLINE;

int register_cpu_callback(void)
{
	hotplug_cpu_state = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
		"ksm/hotplug:online", ksm_hotplug_cpu_online,
		ksm_hotplug_cpu_teardown);

	return 0;
}

void unregister_cpu_callback(void)
{
	cpuhp_remove_state(hotplug_cpu_state);
}
#else
static void *hotplug_cpu;

static void ksm_hotplug_cpu(void *ctx, PKE_PROCESSOR_CHANGE_NOTIFY_CONTEXT change_ctx, PNTSTATUS op_status)
{
	/* CPU Hotplug callback, a CPU just came online.  */
	GROUP_AFFINITY affinity;
	GROUP_AFFINITY prev;
	PPROCESSOR_NUMBER pnr;
	int status;

	if (change_ctx->State == KeProcessorAddCompleteNotify) {
		pnr = &change_ctx->ProcNumber;
		affinity.Group = pnr->Group;
		affinity.Mask = 1ULL << pnr->Number;
		KeSetSystemGroupAffinityThread(&affinity, &prev);

		KSM_DEBUG_RAW("New processor\n");
		status = __ksm_init_cpu(ksm);
		if (!NT_SUCCESS(status))
			*op_status = status;

		KeRevertToUserGroupAffinityThread(&prev);
	}
}

int register_cpu_callback(void)
{
	hotplug_cpu = KeRegisterProcessorChangeCallback(ksm_hotplug_cpu, NULL, 0);
	if (!hotplug_cpu)
		return STATUS_UNSUCCESSFUL;

	return 0;
}

void unregister_cpu_callback(void)
{
	KeDeregisterProcessorChangeCallback(hotplug_cpu);
}

#endif
