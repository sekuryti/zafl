#include <iostream>

using namespace std;

extern "C" void zafl_bbInstrument(unsigned short);
extern "C" void zafl_initAflForkServer();

int main(int argc, char **argv)
{
	int x = 0;

//	zafl_initAflForkServer();

	zafl_bbInstrument(0);

	cout << "Enter a number: ";

	cin >> x;

	cout << "Number is: " << dec << x << endl;

	if (x % 2 == 0)
	{
		zafl_bbInstrument(2);
		cout << "Divisible by 2" << endl;
	}

	if (x % 3 == 0)
	{
		zafl_bbInstrument(3);
		cout << "Divisible by 3" << endl;
	}

	if (x % 5 == 0)
	{
		zafl_bbInstrument(5);
		cout << "Divisible by 5" << endl;
	}
}
