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




int read_and_execute_instruction(){
    int running=1;
    int is_max=R_PC==UINT16_MAX;

    /* FETCH */
    uint16_t instr=mem_read(reg[R_PC]++);
    uint16_t op=instr>>12;

    switch(op){
        case OP_ADD:break;
        case OP_AND:break;
        case OP_NOT:break;
        case OP_BR:break;
        case OP_JMP:break;
        case OP_JSR:break;
        case OP_LD:break;
        case OP_LDI:break;
        case OP_LDR:break;
        case OP_LEA:break;
        case OP_ST:break;
        case OP_STI:break;
        case OP_STR:break;
        case OP_TRAP:break;
        case OP_RES:break;
        case OP_RTI:break;
        default:
            break;
    }

    if(running&&is_max){
        printf("Program counter overflow!\n");
        return 0;
    }

    return running;
}

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

/* end */

/** Tests **/
int test_add_instr_1(){
    int pass=1;
    uint16_t add_instr=
        ((OP_ADD & 0xf)<<12)|
        ((R_R0 & 0x7)<<9)   |
        ((R_R1 & 0x7)<<6)   |
        (R_R2 & 0x7);
    memory[0x3000]=add_instr;
    reg[R_R1]=1;
    reg[R_R2]=2;

    int result=read_and_execute_instruction();
    if(result!=-1){
        printf("Expected return value to be 1, got %d\n",result);
        pass=0;
    }
    if(reg[R_R0]!=3){
        printf("Expected register 0 to contaion 3, got %d\n",reg[R_R0]);
    }

    if(reg[R_COND]!=FL_POS){
        printf("Expected condition flags to be %d, got %d\n",FL_POS,reg[R_COND]);
        pass=0;
    }
    return pass;
}

int test_add_instr_2(){
    int pass=1;
    return pass;
}



int run_tests(){
    int (*tests[])()={
        test_add_instr_1,
        test_add_instr_2,
        NULL
    };

    int i,result,ok=1;
    for(i=0;tests[i]!=NULL;i++){
        /* clear memory */
        memset(reg,0,sizeof(reg));
        memset(memory,0,sizeof(memory));

        enum{PC_START=0x3000};
        reg[R_PC]=PC_START;

        result=tests[i]();
        if(!result){
            printf("Test %d failed\n",i);
            ok=0;
        }
    }

    if(ok){
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

