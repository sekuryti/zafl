#include <unistd.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	int x;

	read(0, &x, 4);
	if (x == 29535)
            abort();
	return 0;
}
