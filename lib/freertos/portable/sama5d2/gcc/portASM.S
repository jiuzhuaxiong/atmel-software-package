;/*
;    FreeRTOS V9.0.0 - Copyright (C) 2016 Real Time Engineers Ltd.
;    All rights reserved
;
;
;    ***************************************************************************
;     *                                                                       *
;     *    FreeRTOS tutorial books are available in pdf and paperback.        *
;     *    Complete, revised, and edited pdf reference manuals are also       *
;     *    available.                                                         *
;     *                                                                       *
;     *    Purchasing FreeRTOS documentation will not only help you, by       *
;     *    ensuring you get running as quickly as possible and with an        *
;     *    in-depth knowledge of how to use FreeRTOS, it will also help       *
;     *    the FreeRTOS project to continue with its mission of providing     *
;     *    professional grade, cross platform, de facto standard solutions    *
;     *    for microcontrollers - completely free of charge!                  *
;     *                                                                       *
;     *    >>> See http://www.FreeRTOS.org/Documentation for details. <<<     *
;     *                                                                       *
;     *    Thank you for using FreeRTOS, and thank you for your support!      *
;     *                                                                       *
;    ***************************************************************************
;
;
;    This file is part of the FreeRTOS distribution.
;
;    FreeRTOS is free software; you can redistribute it and/or modify it under
;    the terms of the GNU General Public License (version 2) as published by the
;    Free Software Foundation AND MODIFIED BY the FreeRTOS exception.
;    >>>NOTE<<< The modification to the GPL is included to allow you to
;    distribute a combined work that includes FreeRTOS without being obliged to
;    provide the source code for proprietary components outside of the FreeRTOS
;    kernel.  FreeRTOS is distributed in the hope that it will be useful, but
;    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;    or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
;    more details. You should have received a copy of the GNU General Public
;    License and the FreeRTOS license exception along with FreeRTOS; if not it
;    can be viewed here: http://www.freertos.org/a00114.html and also obtained
;    by writing to Richard Barry, contact details for whom are available on the
;    FreeRTOS WEB site.
;
;    1 tab == 4 spaces!
;
;    http://www.FreeRTOS.org - Documentation, latest information, license and
;    contact details.
;
;    http://www.SafeRTOS.com - A version that is certified for use in safety
;    critical systems.
;
;    http://www.OpenRTOS.com - Commercial support, development, porting,
;    licensing and training services.
;*/

#define AIC         0xFC020000
#define AIC_EOICR   0x38

	.text
	.arm

	.set SYS_MODE,	0x1f
	.set SVC_MODE,	0x13
	.set IRQ_MODE,	0x12


	/* Variables and functions. */
	.extern resetVector
	.extern vTaskSwitchContext
	.extern ulPortYieldRequired
	.extern ulPortInterruptNesting
	.extern vApplicationIRQHandler
	.extern	vTaskSwitchContext
	.extern ulCriticalNesting
	.extern	pxCurrentTCB
	.extern	ulPortTaskHasFPUContext

	.global FreeRTOS_SWI_Handler
	.global FreeRTOS_IRQ_Handler
	.global vPortRestoreTaskContext


.macro portSAVE_CONTEXT

	/* Save the LR and SPSR onto the system mode stack before switching to
	 system mode to save the remaining system mode registers. */
	SRSDB	sp!, #SYS_MODE
	CPS		#SYS_MODE
	PUSH	{R0-R12, R14}

	/* Push the critical nesting count */
	LDR		R2, ulCriticalNestingConst
	LDR		R1, [R2]
	PUSH	{R1}

	/* Does the task have a floating point context that needs saving?  If
	 ulPortTaskHasFPUContext is 0 then no. */
	LDR		R2, ulPortTaskHasFPUContextConst
	LDR		R3, [R2]
	CMP		R3, #0

	/* Save the floating point context, if any */
	FMRXNE  R1,  FPSCR
	VPUSHNE {D0-D15}
#if configFPU_D32 == 1
	VPUSHNE	{D16-D31}
#endif /* configFPU_D32 */
	PUSHNE	{R1}

	/* Save ulPortTaskHasFPUContext itself */
	PUSH	{R3}

	/* Save the stack pointer in the TCB */
	LDR		R0, pxCurrentTCBConst
	LDR		R1, [R0]
	STR		SP, [R1]

	.endm

/**********************************************************************/

.macro portRESTORE_CONTEXT

	/* Set the SP to point to the stack of the task being restored. */
	LDR		R0, pxCurrentTCBConst
	LDR		R1, [R0]
	LDR		SP, [R1]

	/* Is there a floating point context to restore?  If the restored
	ulPortTaskHasFPUContext is zero then no. */
	LDR		R0, ulPortTaskHasFPUContextConst
	POP		{R1}
	STR		R1, [R0]
	CMP		R1, #0

	/* Restore the floating point context, if any */
	POPNE 	{R0}
#if configFPU_D32 == 1
	VPOPNE	{D16-D31}
#endif /* configFPU_D32 */
	VPOPNE	{D0-D15}
	VMSRNE  FPSCR, R0

	/* Restore the critical section nesting depth */
	LDR		R0, ulCriticalNestingConst
	POP		{R1}
	STR		R1, [R0]

	/* Restore all system mode registers other than the SP (which is already
	 being used) */
	POP		{R0-R12, R14}

	/* Return to the task code, loading CPSR on the way.  CPSR has the interrupt
	 enable bit set appropriately for the task about to execute. */
	RFEIA	sp!

	.endm

/******************************************************************************
 * SVC handler is used to yield a task.
 *****************************************************************************/
.align 4
.type FreeRTOS_SWI_Handler, %function
FreeRTOS_SWI_Handler:

	/* Save the context of the current task and select a new task to run. */
	portSAVE_CONTEXT
	LDR R0, vTaskSwitchContextConst
	BLX	R0
	portRESTORE_CONTEXT


/******************************************************************************
 * vPortRestoreTaskContext is used to start the scheduler.
 *****************************************************************************/
.align 4
.type vPortRestoreTaskContext, %function
vPortRestoreTaskContext:

	/* Switch to system mode */
	CPS		#SYS_MODE
	portRESTORE_CONTEXT

/******************************************************************************
 * IRQ interrupt handler used when individual priorities cannot be masked
 *****************************************************************************/
.align 4
.type FreeRTOS_IRQ_Handler, %function
FreeRTOS_IRQ_Handler:


	/* Return to the interrupted instruction. */
	SUB		lr, lr, #4

	/* Push the return address and SPSR. */
	PUSH	{lr}
	MRS		lr, SPSR
	PUSH	{lr}

	/* Change to supervisor mode to allow reentry. */
	CPS		#SVC_MODE

	/* Push used registers. */
	PUSH	{r0-r4, r12}

	/* Increment nesting count.  r3 holds the address of ulPortInterruptNesting
	 for future use.  r1 holds the original ulPortInterruptNesting value for
	 future use. */
	LDR		r3, ulPortInterruptNestingConst
	LDR		r1, [r3]
	ADD		r4, r1, #1
	STR		r4, [r3]

	/* Ensure bit 2 of the stack pointer is clear.  r2 holds the bit 2 value for
	 future use. */
	MOV		r2, sp
	AND		r2, r2, #4
	SUB		sp, sp, r2

	PUSH	{r0-r4, lr}

	/* Call the port part specific handler. */
	LDR		r0, vApplicationIRQHandlerConst
	BLX		r0
	POP		{r0-r4, lr}
	ADD		sp, sp, r2

	CPSID	i


	/* Write to the EOI register. */
	ldr     r4, =AIC
	str     r0, [r4, #AIC_EOICR]

	/* Restore the old nesting count. */
	STR		r1, [r3]

	/* A context switch is never performed if the nesting count is not 0. */
	CMP		r1, #0
	BNE		exit_without_switch

	/* Did the interrupt request a context switch?  r1 holds the address of
	 ulPortYieldRequired and r0 the value of ulPortYieldRequired for future
	 use. */
	LDR		r1, ulPortYieldRequiredConst
	LDR		r0, [r1]
	CMP		r0, #0
	BNE		switch_before_exit

exit_without_switch:
	/* No context switch.  Restore used registers, LR_irq and SPSR before
	 returning. */
	POP		{r0-r4, r12}
	CPS		#IRQ_MODE
	POP		{LR}
	MSR		SPSR_cxsf, LR
	POP		{LR}
	MOVS	PC, LR

switch_before_exit:
	/* A context switch is to be performed.  Clear the context switch pending
	 flag. */
	MOV		r0, #0
	STR		r0, [r1]

	/* Restore used registers, LR-irq and SPSR before saving the context
	 to the task stack. */
	POP		{r0-r4, r12}
	CPS		#IRQ_MODE
	POP		{LR}
	MSR		SPSR_cxsf, LR
	POP		{LR}
	portSAVE_CONTEXT

	/* Call the function that selects the new task to execute.
	 vTaskSwitchContext() if vTaskSwitchContext() uses LDRD or STRD
	 instructions, or 8 byte aligned stack allocated data.  LR does not need
	 saving as a new LR will be loaded by portRESTORE_CONTEXT anyway. */
	LDR		r0, vTaskSwitchContextConst
	BLX		r0

	/* Restore the context of, and branch to, the task selected to execute next. */
	portRESTORE_CONTEXT


pxCurrentTCBConst: .word pxCurrentTCB
ulCriticalNestingConst: .word ulCriticalNesting
ulPortTaskHasFPUContextConst: .word ulPortTaskHasFPUContext
vTaskSwitchContextConst: .word vTaskSwitchContext
vApplicationIRQHandlerConst: .word vApplicationIRQHandler
ulPortInterruptNestingConst: .word ulPortInterruptNesting
ulPortYieldRequiredConst: .word ulPortYieldRequired

	.end


