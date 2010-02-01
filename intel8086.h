extern int PC;
extern void PrintFlags();
extern void PrintRegisters();
int DoOP(unsigned char OP);
#define PCnew (CS<<4)+IP
#define AH (AX>>8 & 0xFF)
#define AL (AX & 0xFF)
#define CH (CX>>8 & 0xFF) 
#define CL (CX & 0xFF)
#define IF (FLAGS>>9 & 0x1)
#define CF (FLAGS & 0x1)
#define PF (FLAGS>>2 & 0x1)
#define SF (FLAGS>>7 & 0x1)
#define ZF (FLAGS>>6 & 0x1)
#define OF (FLAGS>>11 & 0x1)


//macros
#define JMP1(condition) if(condition) PC += ram[PC+1] + 2; else PC +=2; 
#define CHKZF(arg) if (arg == 0) FLAGS |= 0x40; else FLAGS &= 0xFFBF;
#define RDRAM(addr) RAM[CS]