#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	int x = strtoul(argv[1], NULL, 16);

	printf("x = 0x%x\n", x);
	if (x >> 24 == 0x12)
		if (((x&0xff0000) >> 16) == 0x34)
			if (((x&0xff00) >> 8) == 0x56)
				if ((x & 0xff) == 0x78)
					return 1;

	return 0;
}
