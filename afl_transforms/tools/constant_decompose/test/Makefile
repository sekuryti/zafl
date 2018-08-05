all: test0.decomposed test_afl.decomposed

test_afl: test4.c
	g++ test4.c -o $@
test_afl.decomposed: test_afl
	$(PSZ) test_afl $@ --critical-step constant_decompose=on

test0.exe: test0.c
	g++ test0.c -o $@
test0.decomposed: test0.exe
	$(PSZ) test0.exe $@ --critical-step constant_decompose=on

clean:
	rm -fr peasoup_exec* *.o test_afl test_afl.decomposed test0.exe test0.decomposed a.out a.exe
