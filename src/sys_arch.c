#include "FreeRTOS.h"
#include "lwip/mem.h"
#include "lwip/arch.h"

void *malloc(size_t size)
{
    return pvPortMalloc(size);
}

void free(void *ptr)
{
    vPortFree(ptr);
}
