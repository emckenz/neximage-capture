#ifndef NEXIMAGE_UVC_ASSEMBLE_H
#define NEXIMAGE_UVC_ASSEMBLE_H

#include "uvc_types.h"

void uvc_assembler_init(UvcAssembler *a, unsigned char *storage, int capacity, int expected_size);
int uvc_assembler_push(UvcAssembler *a, const unsigned char *payload, int len);
void uvc_assembler_take(UvcAssembler *a);

#endif
