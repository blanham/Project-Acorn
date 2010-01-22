extern int PC;
extern void PrintFlags();
extern void PrintRegisters();
int DoOP(unsigned char OP);
#define PCnew (CS<<4)+IP
#define AH (AX>>8)
#define AL (AX & 0xFF)
#define CH (CX>>8) 
#define CL (CX & 0xFF)
#define IF (FLAGS>>9 & 0x1)
#define CF (FLAGS & 0x1)
#define PF (FLAGS>>2 & 0x1)
#define SF (FLAGS>>7 & 0x1)
#define ZF (FLAGS>>6 & 0x1)
#define OF(a) (FLAGS |= a<<11)