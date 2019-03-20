#include <stdio.h>
#include <stdlib.h>

volatile int compare_me(short x)
{
	if (x == -0x1234)
            abort();
}

int main(int argc, char **argv)
{
	short x;
	FILE *fp = fopen(argv[1],"r");

	if (!fp) {
		fprintf(stderr, "Need input file\n");
		return 1;
	}

	fread(&x, 4, 1, fp);

	compare_me(x);

	fclose(fp);
	return 0;

}
