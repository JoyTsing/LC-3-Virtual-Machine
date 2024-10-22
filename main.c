#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
#include <Windows.h>
#include <conio.h>

HANDLE hStdin= INVALID_HANDLE_VALUE;

uint16_t check_key(){
    return waitForSingleObject(hStdin,1000)==WAIT_OBJECT_0 && _kbhit();
}

DWORD fdwMode,fdwOldMode;

void disable_input_buffering(){
    hStdin=GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin,&fdwOldMode);
    fdwMode=fdwOldMode
        ^ ENABLE_ECHO_INPUT
        ^ ENABLE_LINE_INPUT;
    SetConsoleMode(hStdin,fdwMode);
    FlushConsoleInputBuffer(hStdin);
}

void restore_input_buffering(){
    SetConsoleMode(hStdin,fdwOldMode);
}
#else
#include <fcntl.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>
/** Platform Specifics **/

/* get keyboard status */
uint16_t check_key(){
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO,&readfds);

    struct timeval timeout;
    timeout.tv_sec=0;
    timeout.tv_usec=0;
    return select(1,&readfds,NULL,NULL,&timeout)!=0;
}

/* terminal input setup */
struct termios original_tio;

void disable_input_buffering(){
    tcgetattr(STDIN_FILENO,&original_tio);
    struct termios new_tio=original_tio;
    new_tio.c_cflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO,TCSANOW, &new_tio);
}

void restore_input_buffering(){
    tcsetattr(STDIN_FILENO,TCSANOW,&original_tio);
}

void handle_interrupt(int signal){
    restore_input_buffering();
    printf("\n");
    exit(-2);
}
#endif

/* end */
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
    OP_BR=0,//branch
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
    OP_STI, //store indirect
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

/* trap codes */
enum{
    TRAP_GETC = 0x20,  /* get character from keyboard */
    TRAP_OUT = 0x21,   /* output a character */
    TRAP_PUTS = 0x22,  /* output a word string */
    TRAP_IN = 0x23,    /* input a string */
    TRAP_PUTSP = 0x24, /* output a byte string */
    TRAP_HALT = 0x25   /* halt the program */
};

/* memory mapped registers */
enum{
    MR_KBSR=0xFE00, /* keyboard status */
    MR_KBDR=0xFE02 /* keyboard data */
};

uint16_t swap16(uint16_t x){
    return (x<<8)|(x>>8);
}

uint16_t sign_extend(uint16_t x,int bit_count){
    if((x>>(bit_count-1))&1){
        x|=(0xFFFF<<bit_count);
    }
    return x;
}

void mem_write(uint16_t address,uint16_t val){
    memory[address]=val;
}

uint16_t mem_read(uint16_t address){
    /* reading the memory mapped keyboard register triggers a key check */
    if(address==MR_KBSR){
        if(check_key()){
            memory[MR_KBSR]=(1<<15);
            memory[MR_KBDR]=getchar();
        }else{
            memory[MR_KBSR]=0;
        }
    }
    return memory[address];
}

void update_flags(uint16_t r){
    if(reg[r]==0){
        reg[R_COND]=FL_ZRO;
    }else if(reg[r]>>15){
        reg[R_COND]=FL_NEG;
    }else{
        reg[R_COND]=FL_POS;
    }
}

void read_image_file(FILE* file){
    /* origin tells us where in memory to place the image */
    uint16_t origin;
    fread(&origin,sizeof(origin),1,file);
    origin=swap16(origin);

    /* we know the maximum file size so we only need one fread */
    uint16_t max_read=UINT16_MAX-origin;
    uint16_t* p=memory+origin;
    size_t read=fread(p,sizeof(uint16_t),max_read,file);

    /* swap to little endian */
    while(read-- >0){
        *p=swap16(*p);
        ++p;
    }

}

int read_image(const char* image_path){
    FILE* file=fopen(image_path,"rb");
    if(!file){return 0;}
    read_image_file(file);
    fclose(file);
    return 1;
}

/* execute trap routine */
int execute_trap(uint16_t instr,FILE* in,FILE* out){
    int running=1;
    switch(instr&0xFF){
        case TRAP_GETC:
            {
                uint16_t c=getc(in);
                reg[R_R0]=c;
            }
            break;
        case TRAP_OUT:
            {
                char c=(char)reg[R_R0&0xff];
                putc(c,out);
            }
            break;
        case TRAP_PUTS:
            {
                uint16_t* word=memory+reg[R_R0];
                while(*word){
                    putc((char)(*word&0xff),out);
                    word++;
                }
                fflush(out);
            }
            break;
        case TRAP_IN:
            {
                fprintf(out,"Enter a character: ");
                fflush(out);
                uint16_t c=getc(in);
                putc((char)c,out);
                fflush(out);
                reg[R_R0]=c;
            }
            break;
        case TRAP_PUTSP:
            {
                /* one char per byte(two bytes per word) */
                uint16_t* word=memory+reg[R_R0];
                while(*word){
                    putc((char)(*word&0xff),out);
                    char c=*word>>8;
                    if(c){
                        putc(c,out);
                    }
                    word++;
                }
                fflush(out);
            }
            break;
        case TRAP_HALT:
            {
                fputs("HALT\n",out);
                fflush(out);
                running=0;
            }
            break;
    }

    return running;
}

int read_and_execute_instruction(){
    int running=1;
    int is_max=R_PC==UINT16_MAX;

    /* FETCH */
    uint16_t instr=mem_read(reg[R_PC]++);
    uint16_t op=instr>>12;

    switch(op){
        case OP_ADD:
            {
                uint16_t dr=(instr>>9)&0x7;
                uint16_t sr1=(instr>>6)&0x7;
                uint16_t imm_flag=(instr>>5)&0x1;
                if(imm_flag){
                    uint16_t imm5=sign_extend(instr&0x1f,5);
                    reg[dr]=reg[sr1]+imm5;
                }else{
                    uint16_t sr2=instr&0x7;
                    reg[dr]=reg[sr1]+reg[sr2];
                }
                update_flags(dr);
            }
            break;
        case OP_AND:
            {
                uint16_t dr=(instr>>9)&0x7;
                uint16_t sr1=(instr>>6)&0x7;
                uint16_t imm_flag=(instr>>5)&0x1;
                if(imm_flag){
                    uint16_t imm5=sign_extend(instr&0x1f,5);
                    reg[dr]=reg[sr1]&imm5;
                }else{
                    uint16_t sr2=instr&0x7;
                    reg[dr]=reg[sr1]&reg[sr2];
                }
                update_flags(dr);
            }
            break;
        case OP_NOT:
            {
                uint16_t dr=(instr>>9)&0x7;
                uint16_t sr=(instr>>6)&0x7;
                reg[dr]=~reg[sr];
                update_flags(dr);
            }
            break;
        case OP_BR:
            {
                uint16_t n_flag=(instr>>11)&0x1;
                uint16_t z_flag=(instr>>10)&0x1;
                uint16_t p_flag=(instr>>9) &0x1;

                /* PCoffset 9 */
                uint16_t pc_offset=sign_extend(instr&0x1ff,9);
                /*
                 * n_flag is set and negatiev condition flag is set
                 * z_flag is set and zero condition flag is set
                 * p_flag is set and positive condition flag is set
                 * */
                if((n_flag && (reg[R_COND]& FL_NEG))||
                   (z_flag && (reg[R_COND]& FL_ZRO))||
                   (p_flag && (reg[R_COND]& FL_POS))){
                    reg[R_PC]+=pc_offset;
                }
            }
            break;
        case OP_JMP:
            {
                uint16_t base_r=(instr>>6)&0x7;
                reg[R_PC]=reg[base_r];
            }
            break;
        case OP_JSR:
            {
                /* save pc in R7 to jump back to later */
                reg[R_R7]=reg[R_PC];
                uint16_t imm_flag=(instr>>11)&0x1;
                if(imm_flag){
                    uint16_t pc_offset=sign_extend(instr&0x7ff,11);
                    reg[R_PC]+=pc_offset;
                }else{
                    uint16_t base_r=(instr>>6)&0x7;
                    reg[R_PC]=reg[base_r];
                }
            }
            break;
        case OP_LD:
            {
                uint16_t dr=(instr>>9)&0x7;
                uint16_t pc_offset=sign_extend(instr&0x1ff,9);
                /* add pc_offset to the current pc and load that memory location */
                reg[dr]=mem_read(reg[R_PC]+pc_offset);
                update_flags(dr);
            }
            break;
        case OP_LDI:
            {
                uint16_t dr=(instr>>9)&0x7;
                uint16_t pc_offset=sign_extend(instr&0x1ff,9);
                /* add pc_offset to the current PC, look at that memory
                 * location to get the final address */
                reg[dr]=mem_read(mem_read(reg[R_PC]+pc_offset));
                update_flags(dr);
            }
            break;
        case OP_LDR:
            {
                uint16_t dr=(instr>>9)&0x7;
                uint16_t base_r=(instr>>6)&0x7;
                uint16_t offset=sign_extend(instr&0x3f,6);
                reg[dr]=mem_read(reg[base_r]+offset);
                update_flags(dr);
            }
            break;
        case OP_LEA:
            {
                uint16_t dr=(instr>>9)&0x7;
                uint16_t pc_offset=sign_extend(instr&0x1ff,9);
                reg[dr]=reg[R_PC]+pc_offset;
                update_flags(dr);
            }
            break;
        case OP_ST:
            {
                uint16_t sr=(instr>>9)&0x7;
                uint16_t pc_offset=sign_extend(instr&0x1ff,9);
                mem_write(reg[R_PC]+pc_offset,reg[sr]);
            }
            break;
        case OP_STI:
            {
                uint16_t sr=(instr>>9)&0x7;
                uint16_t pc_offset=sign_extend(instr&0x1ff,9);
                mem_write(mem_read(reg[R_PC]+pc_offset),reg[sr]);
            }
            break;
        case OP_STR:
            {
                uint16_t sr=(instr>>9)&0x7;
                uint16_t base_r=(instr>>6)&0x7;
                uint16_t offset=sign_extend(instr& 0x3f,6);
                mem_write(reg[base_r]+offset,reg[sr]);
            }
            break;
        case OP_TRAP:
            running=execute_trap(instr,stdin,stdout);
            break;
        case OP_RES:
        case OP_RTI:
        default:
            abort();
            break;
    }

    if(running&&is_max){
        printf("Program counter overflow!\n");
        return 0;
    }

    return running;
}


/** Tests **/

int test_add_instr_1() {
  int pass = 1;

  uint16_t add_instr =
    ((OP_ADD & 0xf) << 12) |
    ((R_R0 & 0x7) << 9)    |
    ((R_R1 & 0x7) << 6)    |
    (R_R2 & 0x7);

  memory[0x3000] = add_instr;
  reg[R_R1] = 1;
  reg[R_R2] = 2;

  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (reg[R_R0] != 3) {
    printf("Expected register 0 to contain 3, got %d\n", reg[R_R0]);
    pass = 0;
  }

  if (reg[R_COND] != FL_POS) {
    printf("Expected condition flags to be %d, got %d\n", FL_POS, reg[R_COND]);
    pass = 0;
  }

  return pass;
}

int test_add_instr_2() {
  int pass = 1;

  uint16_t add_instr =
    ((OP_ADD & 0xf) << 12) |
    ((R_R0 & 0x7) << 9)    |
    ((R_R1 & 0x7) << 6)    |
    (1 << 5) |
    0x2;

  memory[0x3000] = add_instr;
  reg[R_R1] = 1;

  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (reg[R_R0] != 3) {
    printf("Expected register 0 to contain 3, got %d\n", reg[R_R0]);
    pass = 0;
  }

  if (reg[R_COND] != FL_POS) {
    printf("Expected condition flags to be %d, got %d\n", FL_POS, reg[R_COND]);
    pass = 0;
  }

  return pass;
}

int test_and_instr_1() {
  int pass = 1;

  uint16_t and_instr =
    ((OP_AND & 0xf) << 12) |
    ((R_R0 & 0x7) << 9)    |
    ((R_R1 & 0x7) << 6)    |
    (R_R2 & 0x7);

  memory[0x3000] = and_instr;
  reg[R_R1] = 0xff;
  reg[R_R2] = 0xf0;

  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (reg[R_R0] != 0xf0) {
    printf("Expected register 0 to contain %d, got %d\n", 0xf0, reg[R_R0]);
    pass = 0;
  }

  if (reg[R_COND] != FL_POS) {
    printf("Expected condition flags to be %d, got %d\n", FL_POS, reg[R_COND]);
    pass = 0;
  }

  return pass;
}

int test_and_instr_2() {
  int pass = 1;

  uint16_t and_instr =
    ((OP_AND & 0xf) << 12) |
    ((R_R0 & 0x7) << 9)    |
    ((R_R1 & 0x7) << 6)    |
    (1 << 5) |
    0x0f;

  memory[0x3000] = and_instr;
  reg[R_R1] = 0xff;

  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (reg[R_R0] != 0x0f) {
    printf("Expected register 0 to contain %d, got %d\n", 0x0f, reg[R_R0]);
    pass = 0;
  }

  if (reg[R_COND] != FL_POS) {
    printf("Expected condition flags to be %d, got %d\n", FL_POS, reg[R_COND]);
    pass = 0;
  }

  return pass;
}

int test_not_instr() {
  int pass = 1;

  uint16_t not_instr =
    ((OP_NOT & 0xf) << 12) |
    ((R_R0 & 0x7) << 9)    |
    ((R_R1 & 0x7) << 6)    |
    0x3f;

  memory[0x3000] = not_instr;
  reg[R_R1] = 0xf;

  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (reg[R_R0] != 0xfff0) {
    printf("Expected register 0 to contain %d, got %d\n", 0xfff0, reg[R_R0]);
    pass = 0;
  }

  if (reg[R_COND] != FL_NEG) {
    printf("Expected condition flags to be %d, got %d\n", FL_NEG, reg[R_COND]);
    pass = 0;
  }

  return pass;
}

int test_br_instr_1() {
  int pass = 1;

  uint16_t br_instr =
    ((OP_BR & 0xf) << 12) |
    (1 << 11) |
    0x123;

  memory[0x3000] = br_instr;

  /* nothing should happen */
  reg[R_COND] = 0;
  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (reg[R_PC] != 0x3001) {
    printf("Expected program counter to contain %d, got %d\n", 0x3001, reg[R_PC]);
    pass = 0;
  }

  return pass;
}

int test_br_instr_2() {
  int pass = 1;

  uint16_t br_instr =
    ((OP_BR & 0xf) << 12) |
    (1 << 11) |
    0x0ff;

  memory[0x3000] = br_instr;

  reg[R_COND] = FL_NEG;
  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (reg[R_PC] != 0x3100) {
    printf("Expected program counter to contain %d, got %d\n", 0x3100, reg[R_PC]);
    pass = 0;
  }

  return pass;
}

int test_br_instr_3() {
  int pass = 1;

  uint16_t br_instr =
    ((OP_BR & 0xf) << 12) |
    (1 << 10) |
    0x0ff;

  memory[0x3000] = br_instr;

  reg[R_COND] = FL_ZRO;
  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (reg[R_PC] != 0x3100) {
    printf("Expected program counter to contain %d, got %d\n", 0x3100, reg[R_PC]);
    pass = 0;
  }

  return pass;
}

int test_br_instr_4() {
  int pass = 1;

  uint16_t br_instr =
    ((OP_BR & 0xf) << 12) |
    (1 << 9) |
    0x0ff;

  memory[0x3000] = br_instr;

  reg[R_COND] = FL_POS;
  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (reg[R_PC] != 0x3100) {
    printf("Expected program counter to contain %d, got %d\n", 0x3100, reg[R_PC]);
    pass = 0;
  }

  return pass;
}

int test_jmp_instr() {
  int pass = 1;

  uint16_t jmp_instr =
    ((OP_JMP & 0xf) << 12) |
    ((R_R0 & 0x7) << 6);

  memory[0x3000] = jmp_instr;
  reg[R_R0] = 0x1234;

  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (reg[R_PC] != 0x1234) {
    printf("Expected program counter to contain %d, got %d\n", 0x1234, reg[R_PC]);
    pass = 0;
  }

  return pass;
}

int test_jsr_instr_1() {
  int pass = 1;

  uint16_t jsr_instr =
    ((OP_JSR & 0xf) << 12) |
    (1 << 11) |
    0xff;

  memory[0x3000] = jsr_instr;

  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (reg[R_PC] != 0x3100) {
    printf("Expected program counter to contain %d, got %d\n", 0x3100, reg[R_PC]);
    pass = 0;
  }

  if (reg[R_R7] != 0x3001) {
    printf("Expected register 7 to contain %d, got %d\n", 0x3001, reg[R_R7]);
    pass = 0;
  }

  return pass;
}

int test_jsr_instr_2() {
  int pass = 1;

  uint16_t jsr_instr =
    ((OP_JSR & 0xf) << 12) |
    ((R_R0 & 0x7) << 6);

  memory[0x3000] = jsr_instr;
  reg[R_R0] = 0x1234;

  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (reg[R_PC] != 0x1234) {
    printf("Expected program counter to contain %d, got %d\n", 0x1234, reg[R_PC]);
    pass = 0;
  }

  if (reg[R_R7] != 0x3001) {
    printf("Expected register 7 to contain %d, got %d\n", 0x3001, reg[R_R7]);
    pass = 0;
  }

  return pass;
}

int test_ld_instr() {
  int pass = 1;

  uint16_t ld_instr =
    ((OP_LD & 0xf) << 12) |
    ((R_R0 & 0x7) << 9)   |
    0xff;

  memory[0x3000] = ld_instr;
  memory[0x3100] = 0x123;

  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (reg[R_R0] != 0x123) {
    printf("Expected register 0 to contain %d, got %d\n", 0x123, reg[R_R0]);
    pass = 0;
  }

  if (reg[R_COND] != FL_POS) {
    printf("Expected condition flags to be %d, got %d\n", FL_POS, reg[R_COND]);
    pass = 0;
  }

  return pass;
}

int test_ldi_instr() {
  int pass = 1;

  uint16_t ldi_instr =
    ((OP_LDI & 0xf) << 12) |
    ((R_R0 & 0x7) << 9)   |
    0xff;

  memory[0x3000] = ldi_instr;
  memory[0x3100] = 0x3200;
  memory[0x3200] = 0x123;

  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (reg[R_R0] != 0x123) {
    printf("Expected register 0 to contain %d, got %d\n", 0x123, reg[R_R0]);
    pass = 0;
  }

  if (reg[R_COND] != FL_POS) {
    printf("Expected condition flags to be %d, got %d\n", FL_POS, reg[R_COND]);
    pass = 0;
  }

  return pass;
}

int test_ldr_instr() {
  int pass = 1;

  uint16_t ldr_instr =
    ((OP_LDR & 0xf) << 12) |
    ((R_R0 & 0x7) << 9)    |
    ((R_R1 & 0x7) << 6)    |
    0xf;

  memory[0x3000] = ldr_instr;
  reg[R_R1] = 0x31f1;
  memory[0x3200] = 0x123;

  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (reg[R_R0] != 0x123) {
    printf("Expected register 0 to contain %d, got %d\n", 0x123, reg[R_R0]);
    pass = 0;
  }

  if (reg[R_COND] != FL_POS) {
    printf("Expected condition flags to be %d, got %d\n", FL_POS, reg[R_COND]);
    pass = 0;
  }

  return pass;
}

int test_lea_instr() {
  int pass = 1;

  uint16_t lea_instr =
    ((OP_LEA & 0xf) << 12) |
    ((R_R0 & 0x7) << 9)    |
    0xff;

  memory[0x3000] = lea_instr;

  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (reg[R_R0] != 0x3100) {
    printf("Expected register 0 to contain %d, got %d\n", 0x3100, reg[R_R0]);
    pass = 0;
  }

  if (reg[R_COND] != FL_POS) {
    printf("Expected condition flags to be %d, got %d\n", FL_POS, reg[R_COND]);
    pass = 0;
  }

  return pass;
}

int test_st_instr() {
  int pass = 1;

  uint16_t st_instr =
    ((OP_ST & 0xf) << 12) |
    ((R_R0 & 0x7) << 9)    |
    0xff;

  memory[0x3000] = st_instr;
  reg[R_R0] = 0x123;

  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (memory[0x3100] != 0x123) {
    printf("Expected memory location %d to contain %d, got %d\n", 0x3100, 0x123, reg[R_R0]);
    pass = 0;
  }

  return pass;
}

int test_sti_instr() {
  int pass = 1;

  uint16_t sti_instr =
    ((OP_STI & 0xf) << 12) |
    ((R_R0 & 0x7) << 9)    |
    0xff;

  memory[0x3000] = sti_instr;
  memory[0x3100] = 0x3200;
  reg[R_R0] = 0x123;

  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (memory[0x3200] != 0x123) {
    printf("Expected memory location %d to contain %d, got %d\n", 0x3200, 0x123, reg[R_R0]);
    pass = 0;
  }

  return pass;
}

int test_str_instr() {
  int pass = 1;

  uint16_t str_instr =
    ((OP_STR & 0xf) << 12) |
    ((R_R0 & 0x7) << 9)    |
    ((R_R1 & 0x7) << 6)    |
    0xf;

  memory[0x3000] = str_instr;
  reg[R_R0] = 0x123;
  reg[R_R1] = 0x31f1;

  int result = read_and_execute_instruction();
  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (memory[0x3200] != 0x123) {
    printf("Expected memory location %d to contain %d, got %d\n", 0x3200, 0x123, reg[R_R0]);
    pass = 0;
  }

  return pass;
}

int test_trap_getc() {
  int pass = 1;

  uint16_t trap_getc_instr =
    ((OP_TRAP & 0xf) << 12) |
    (TRAP_GETC & 0xff);

  char in_buf[] = {'x'};
  char out_buf[256];
  FILE *in = fmemopen(in_buf, sizeof(in_buf), "r");
  FILE *out = fmemopen(out_buf, sizeof(out_buf), "w");

  int result = execute_trap(trap_getc_instr, in, out);
  fclose(in);
  fclose(out);

  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (reg[R_R0] != 'x') {
    printf("Expected register 0 to contain %d, got %d\n", 'x', reg[R_R0]);
    pass = 0;
  }

  if (out_buf[0] != 0) {
    printf("Expected output buffer to contain %d, got %d\n", 0, out_buf[0]);
    pass = 0;
  }

  return pass;
}

int test_trap_out() {
  int pass = 1;

  uint16_t trap_out_instr =
    ((OP_TRAP & 0xf) << 12) |
    (TRAP_OUT & 0xff);

  char in_buf[] = {0};
  char out_buf[256];
  FILE *in = fmemopen(in_buf, sizeof(in_buf), "r");
  FILE *out = fmemopen(out_buf, sizeof(out_buf), "w");

  reg[R_R0] = 'x';

  int result = execute_trap(trap_out_instr, in, out);
  fclose(in);
  fclose(out);

  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (out_buf[0] != 'x') {
    printf("Expected output buffer to contain %d, got %d\n", 'x', out_buf[0]);
    pass = 0;
  }

  return pass;
}

int test_trap_puts() {
  int pass = 1;

  uint16_t trap_puts_instr =
    ((OP_TRAP & 0xf) << 12) |
    (TRAP_PUTS & 0xff);

  char in_buf[] = {0};
  char out_buf[256];
  FILE *in = fmemopen(in_buf, sizeof(in_buf), "r");
  FILE *out = fmemopen(out_buf, sizeof(out_buf), "w");

  reg[R_R0] = 0x3100;
  memory[0x3100] = 'h';
  memory[0x3101] = 'e';
  memory[0x3102] = 'y';
  memory[0x3103] = 0;

  int result = execute_trap(trap_puts_instr, in, out);
  fclose(in);
  fclose(out);

  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (strncmp(out_buf, "hey", 3) != 0) {
    printf("Expected output buffer to contain %s, got %s\n", "hey", out_buf);
    pass = 0;
  }

  return pass;
}

int test_trap_in() {
  int pass = 1;

  uint16_t trap_in_instr =
    ((OP_TRAP & 0xf) << 12) |
    (TRAP_IN & 0xff);

  char in_buf[] = {'x'};
  char out_buf[256];
  FILE *in = fmemopen(in_buf, sizeof(in_buf), "r");
  FILE *out = fmemopen(out_buf, sizeof(out_buf), "w");

  int result = execute_trap(trap_in_instr, in, out);
  fclose(in);
  fclose(out);

  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (reg[R_R0] != 'x') {
    printf("Expected register 0 to contain %d, got %d\n", 'x', reg[R_R0]);
  }

  if (strncmp(out_buf, "Enter a character: x", 27) != 0) {
    printf("Expected output buffer to contain %s, got %s\n", "Enter a character: x", out_buf);
    pass = 0;
  }

  return pass;
}

int test_trap_putsp() {
  int pass = 1;

  uint16_t trap_putsp_instr =
    ((OP_TRAP & 0xf) << 12) |
    (TRAP_PUTSP & 0xff);

  char in_buf[] = {0};
  char out_buf[256];
  FILE *in = fmemopen(in_buf, sizeof(in_buf), "r");
  FILE *out = fmemopen(out_buf, sizeof(out_buf), "w");

  reg[R_R0] = 0x3100;
  memory[0x3100] = 'h' | ('e' << 8);
  memory[0x3101] = 'y' | (' ' << 8);
  memory[0x3102] = 'd' | ('u' << 8);
  memory[0x3103] = 'd' | ('e' << 8);
  memory[0x3104] = 0;

  int result = execute_trap(trap_putsp_instr, in, out);
  fclose(in);
  fclose(out);

  if (result != 1) {
    printf("Expected return value to be 1, got %d\n", result);
    pass = 0;
  }

  if (strncmp(out_buf, "hey dude", 8) != 0) {
    printf("Expected output buffer to contain %s, got %s\n", "hey dude", out_buf);
    pass = 0;
  }

  return pass;
}

int test_trap_halt() {
  int pass = 1;

  uint16_t trap_halt_instr =
    ((OP_TRAP & 0xf) << 12) |
    (TRAP_HALT & 0xff);

  char in_buf[] = {0};
  char out_buf[256];
  FILE *in = fmemopen(in_buf, sizeof(in_buf), "r");
  FILE *out = fmemopen(out_buf, sizeof(out_buf), "w");

  int result = execute_trap(trap_halt_instr, in, out);
  fclose(in);
  fclose(out);

  if (result != 0) {
    printf("Expected return value to be 0, got %d\n", result);
    pass = 0;
  }

  if (strncmp(out_buf, "HALT", 8) != 0) {
    printf("Expected output buffer to contain %s, got %s\n", "hey dude", out_buf);
    pass = 0;
  }

  return pass;
}

int run_tests() {
  int (*tests[])(void) = {
    test_add_instr_1,
    test_add_instr_2,
    test_and_instr_1,
    test_and_instr_2,
    test_not_instr,
    test_br_instr_1,
    test_br_instr_2,
    test_br_instr_3,
    test_br_instr_4,
    test_jmp_instr,
    test_jsr_instr_1,
    test_jsr_instr_2,
    test_ld_instr,
    test_ldi_instr,
    test_ldr_instr,
    test_lea_instr,
    test_st_instr,
    test_sti_instr,
    test_str_instr,
    test_trap_getc,
    test_trap_out,
    test_trap_puts,
    test_trap_in,
    test_trap_putsp,
    NULL
  };

  int i, result, ok = 1;
  for (i = 0; tests[i] != NULL; i++) {
    /* clear memory */
    memset(reg, 0, sizeof(reg));
    memset(memory, 0, sizeof(memory));

    /* set the PC to starting position */
    /* 0x3000 is the default */
    enum { PC_START = 0x3000 };
    reg[R_PC] = PC_START;


    result = tests[i]();
    if (!result) {
      printf("Test %d failed!\n", i);
      ok = 0;
    }
  }

  if (ok) {
    printf("All tests passed!\n");
    return 0;
  }
  return 1;
}
int main(int argc,char* argv[]){
    if(argc<2){
        printf("lc3 --test | [image-file1] ...\n");
        exit(2);
    }

    if(strcmp(argv[1],"--test")==0){
        exit(run_tests());
    }

    for(int j=1;j<argc;++j){
        if(!read_image(argv[j])){
            printf("failed to load image: %s\n",argv[j]);
            exit(1);
        }
    }

    signal(SIGINT,handle_interrupt);
    disable_input_buffering();

    /* set the PC to starting position */
    /* 0x3000 is the default */
    enum{PC_START=0x3000};
    reg[R_PC]=PC_START;

    int running=1;
    while(running){
        running=read_and_execute_instruction();
    }
    restore_input_buffering();
    return 0;
}

