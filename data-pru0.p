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
MOV   r1, CTPPR_0_P0
ST32  r0, r1         // (Note: STXX instructions are macros from the header)

// Load 2 values from shared RAM into Registers R10/R11
// Only the R11, the sample count, is used by PRU 0
LBCO  r10, CONST_PRUSHAREDRAM, 0, 8

// Low count is in R0, High count is in R1. Zero them.
ZERO  &r0, 8

// Sync up on a clean low-to-high transition
WBC   r31,3
WBS   r31,3

// Count High
CNTLOOPHIGH:

ADD   r1, r1, 1
QBBS  CNTLOOPHIGH, r31, 3

// Count Low
CNTLOOPLOW:

ADD   r0, r0, 1
QBBC  CNTLOOPLOW, r31, 3

// Pass the data to the other PRU
XOUT  10, &r0, 8
MOV   r31.b0, PRU0_PRU1_INTERRUPT + 16

// Low count is in R0, High count is in R1. Zero them.
ZERO  &r0, 8

// Decrement sample count and HALT if count is 0
SUB r11, r11, 1

QBNE CNTLOOPHIGH, r11, 0
HALT
