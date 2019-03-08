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
	return 0;
}
