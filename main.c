#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

/* 65536 locations */
uint16_t memory[UINT16_MAX];

enum{
    R_R0=0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC,   /* program counter */
    R_COND,
    R_COUNT
};

uint16_t reg[R_COUNT];

/* op codes */
enum{
    op_BR=0,//branch
    OP_ADD, //add
    OP_LD,  //load
    OP_ST,  //store
    OP_JSR, //jump register
    OP_AND, //bitwise and
    OP_LDR, //load register
    OP_STR, //store register
    OP_RTI, //unused
    OP_NOT, //bitwise not
    OP_LDI, //load indirect
    OP,STI, //store indirect
    OP_JMP, //jump
    OP_RES, //reserved(unused)
    OP_LEA, //load effective address
    OP_TRAP //execute trap
};

/* condition flags */
enum{
    FL_POS=1<<0,    /* P */
    FL_ZRO=1<<1,    /* Z */
    FL_NEG=1<<2,    /* N */
};

int main(int argc,char* argv[]){

    return 0;
}

