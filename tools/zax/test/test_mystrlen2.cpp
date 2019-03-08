#include <iostream>

using namespace std;

int x = 0;

volatile int identity(int x)
{
	return x;
}

size_t my_strlen(char *arg)
{
	int count = 0;
	while (*arg!='\0')
	{
		if (count % 99)
			x *= count;
		else if (count % 98)
			x += count;

		count++;
		arg++;
	}

	return count;
}

int main(int argc, char **argv)
{
	if (argc > 1)
		cout << "length: " << my_strlen(argv[1]) << endl;
	cout << "x: " << x <<endl;
}
