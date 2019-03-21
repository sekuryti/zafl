#include <stdio.h>
#include <stdlib.h>


volatile void compare_me_2(long x)
{
	if (x == 0x12345678L)
            abort();
}

volatile int compare_me(long x)
{
	if (x == 0x12345678L)
            abort();

	return x;
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

	x = compare_me(x);
	printf("x = %ld\n", x);

	fclose(fp);
	return 0;

}
