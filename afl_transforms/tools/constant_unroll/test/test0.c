#include <unistd.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>

#define MAX_CHARS 1024

#define K 1234567

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
		fprintf(stdout, "x is equal to K=%d\n", K);
	}

	if (x != K) {
		fprintf(stdout, "x is not equal to K=%d\n", K);
	}

	y = half(x*2);
	
	if (y == K)  {
		fprintf(stdout, "y is equal to K=%d\n", K);
	}

	if (y != K) {
		fprintf(stdout, "y is not equal to K=%d\n", K);
	}
	return 0;
}
