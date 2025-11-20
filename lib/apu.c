#include "apu.h"

#include <stdlib.h>
#include <string.h>

#include <ulog.h>

fgb_apu* fgb_apu_init(void) {
	fgb_apu* apu = malloc(sizeof(fgb_apu));
	if (!apu) {
		log_error("Failed to allocate APU");
		return NULL;
	}

	memset(apu, 0, sizeof(fgb_apu));

	return apu;
}
