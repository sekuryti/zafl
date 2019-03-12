#include <iostream>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	int x;
	FILE *fp = fopen(argv[1],"r");

	fread(&x, 4, 1, fp);

	if (x == 0x12345678)
            abort();

	fclose(fp);
	return 0;

}
