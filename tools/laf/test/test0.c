#include <unistd.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>

#define MAX_CHARS 1024

// #define K 1234567
#define K 2

int half(int i) {
	return i / 2;
}

int main(int argc, char **argv)
{
	int x;
	volatile int y;
	char buf[MAX_CHARS+1];

	fgets(buf, MAX_CHARS, stdin);
	x = atoi(buf);
	if (x == K)  {
		fprintf(stdout, "x(%d) is equal to K(%d)\n", x, K);
	}

	if (x != K) {
		fprintf(stdout, "x(%d) is not equal to K(%d)\n", x, K);
	}

	y = half(x);
	
	if (y == K)  {
		fprintf(stdout, "y(%d) is equal to K(%d)\n", y, K);
	}

	if (y != K) {
		fprintf(stdout, "y(%d) is not equal to K(%d)\n", y, K);
	}

	y += y + K;

	int z = 233353 / (y);
	int m = 233353 % y;
	fprintf(stdout, "z=%d m=%d\n", z, m);
	z = -3444 / y;
	fprintf(stdout, "z=%d\n", z);

	long xx = x;
	if (xx == 12345)
		printf("xx == 12345\n");
	else
		printf("xx != 12345\n");
	if (xx == (z+5)/12345)
		printf("xx == z/12345\n");
	else
		printf("xx != z/12345\n");

	if (xx >= -5)
		printf("xx >= -5\n");
	else
		printf("xx < -5\n");
	return 0;
}
