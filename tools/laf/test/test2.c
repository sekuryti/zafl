#include <iostream>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	int x = 0;
	std::cin >> std::hex >> x;
	if (x == 0x12345678)
            abort();
	return 0;
}
