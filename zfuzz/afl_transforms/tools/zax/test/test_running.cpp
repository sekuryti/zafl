#include <iostream>
#include <string.h>

using namespace std;

int num_running_a = 0;

size_t my_strlen(char *arg)
{
	int i = 0;
	bool a_detected = false;

	while(arg[i])
	{
		if (arg[i]=='a') {
				num_running_a++;
				a_detected = true;
		}
		else{
			if (a_detected)
				goto out;
		}
		i++;
	}
out:
	return i;
}

int main(int argc, char **argv)
{
	if (argc > 1)
		cout << "length: " << my_strlen(argv[1]) << endl;
	cout << "num running a: " << num_running_a <<endl;
}
