#include <stdio.h>
volatile int bar(int a)
{
	if (a == 2)
		return 5;
	else
		return 3;
}

volatile int foo(int a)
{
	if (a==3)
		return bar(a);
	else
		return bar(2);

}

volatile int bob(int a)
{
	a = bar(7);
	if (a==2)
		return 1;
	else return 2;
	
}

int main(int argc, char **argv)
{
	int x = foo(argc);
	int y = bar(argc);
	int z = bob(argc);
	printf("out=%d\n", x+y+z);
}
