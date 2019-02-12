#include <iostream>

using namespace std;

int x = 0;

volatile int identity(int y)
{
	if (y % 39 == 0)
		x = 1;
	else
		x = 2;
	return y;
}

size_t my_strlen(char *arg)
{
	int count = 0;
	while (*arg!='\0')
	{
		count++;
		arg++;
		count = identity(count);
	}

	return count;
}

int main(int argc, char **argv)
{
	if (argc > 1)
		cout << "length= " << my_strlen(argv[1]) << endl;
	cout << "x= " << x << endl;
}
