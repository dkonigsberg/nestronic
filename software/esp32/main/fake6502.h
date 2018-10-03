#ifndef FAKE6502_H
#define FAKE6502_H

#include <stdint.h>

void reset6502();

void exec6502(uint32_t tickcount);

void step6502();

void irq6502();

void nmi6502();

void hookexternal(void *funcptr);

uint32_t get6502_ticks();
uint32_t get6502_pc();

#endif /* FAKE6502_H */