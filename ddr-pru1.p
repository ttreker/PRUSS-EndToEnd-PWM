#include "prussints.hp"

.origin 0
.entrypoint START

START:

// Enable OCP master port
LBCO  r0, CONST_PRUCFG, 4, 4
CLR   r0, r0, 4         // Clear SYSCFG[STANDBY_INIT] to enable OCP master port
SBCO  r0, CONST_PRUCFG, 4, 4

// Configure the programmable pointer register for PRU0 by setting c28_pointer[15:0]
// field to 0x0100.  This will make C28 point to 0x00010000 (PRU shared RAM).
MOV   r0, 0x00000100
MOV   r1, CTPPR_0_P1
ST32  r0, r1

// Load 2 values from shared RAM into Registers R10/R11
// DDR physical address is in R10; Sample count is in R11
LBCO  r10, CONST_PRUSHAREDRAM, 0, 8

POLL:
// Poll for receipt of interrupt on host 0
WBS       eventStatus, 31

// DEBUG toggle P8-46 (uncomment to scope action)
// MOV   r5, r30
// XOR   r5, r5, 2
// MOV   r30, r5

// Get the data from the other PRU then store it.
XIN   10, &r0, 8
SBBO  r0, r10, 0, 8

// Update DDR pointer, and remaining sample count
ADD   r10, r10, 8
SUB   r11, r11, 1

// Clear the status of the interrupt
MOV   r0, PRU0_PRU1_INTERRUPT
SBCO	r0,	CONST_PRUSSINTC,	SICR_OFFSET,        4

// If all samples taken, cleanup and halt, otherwise go wait again
QBNE POLL, r11, 0

MOV       r31.b0, PRU0_ARM_INTERRUPT+16
HALT

