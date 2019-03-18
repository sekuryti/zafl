#include <stdio.h>
#include <stdlib.h>

volatile int compare_me(long x)
{
	if (x == -0x12345678L)
            abort();
}

int main(int argc, char **argv)
{
	long x;
	int y;
	FILE *fp = fopen(argv[1],"r");

	if (!fp) {
		fprintf(stderr, "Need input file\n");
		return 1;
	}

	printf("sizeof(x)=%lu\n", sizeof(x));

	fread(&x, 8, 1, fp);

	compare_me(x);

	fclose(fp);
	return 0;

}
