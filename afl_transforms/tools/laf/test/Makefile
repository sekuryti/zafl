all: test0.laf test_afl.laf

test_afl: test4.c
	g++ test4.c -o $@
test_afl.laf: test_afl
	$(PSZ) test_afl $@ --critical-step laf=on

test0.exe: test0.c
	g++ test0.c -o $@
test0.laf: test0.exe
	$(PSZ) test0.exe $@ --critical-step laf=on

clean:
	rm -fr peasoup_exec* *.o test_afl test_afl.laf test0.exe test0.laf a.out a.exe
