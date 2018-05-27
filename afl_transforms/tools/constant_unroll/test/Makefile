all: test_afl.unrolled

test_afl: test.c
	g++ test.c -o $@

test_afl.unrolled: test_afl
	$(PSZ) test_afl $@ --critical-step constant_unroll=on

clean:
	rm -fr peasoup_exec* *.o test_afl test_afl.unrolled
