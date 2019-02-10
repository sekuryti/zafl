#include <iostream>

using namespace std;

size_t my_strlen(char *arg)
{
	int count = 0;
	while (*arg!='\0')
	{
		count++;
		arg++;
	}

	return count;
}

int main(int argc, char **argv)
{
	if (argc > 1)
		cout << "length: " << my_strlen(argv[1]) << endl;
}
