#ifndef XV6_IPI_H
#define XV6_IPI_H

#include "types.h"

void ipiinithart(void);
void ipisend(int cpu_id);
void ipiintr(void);

uint64 ipisent(int cpu_id);
uint64 ipireceived(int cpu_id);

#endif