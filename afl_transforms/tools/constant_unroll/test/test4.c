#include <unistd.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	int x;

	read(0, &x, 4);
//	if (x == 33620225) // 0x02010101
	if (x == 3791716609) // 0xe2010101
//	if (x == 16843057) 
//	if (x == 16851249) 
            abort();
	return 0;
}
