#include <stdio.h>
#include <stdlib.h>

volatile int fib(int a)
{
	if (a == 0) return 0;
	if (a == 1) return 1;
	if (a == 2) return 1;
	return fib(a-1) + fib(a-2);
}

int main(int argc, char **argv)
{
	if (argc <= 1) return 1;
	int x = atoi(argv[1]);
	if (x < 0)
		return 2;
	if (x > 50)
		return 3;
	int f = fib(x);
	printf("fibonacci(%d) = %d\n", x, f);
}
