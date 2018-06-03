all: test0.unrolled test_afl.unrolled

test_afl: test.c
	g++ test.c -o $@
test_afl.unrolled: test_afl
	$(PSZ) test_afl $@ --critical-step constant_unroll=on

test0.exe: test0.c
	g++ test0.c -o $@
test0.unrolled: test0.exe
	$(PSZ) test0.exe $@ --critical-step constant_unroll=on

clean:
	rm -fr peasoup_exec* *.o test_afl test_afl.unrolled test0.exe test0.unrolled a.out a.exe
