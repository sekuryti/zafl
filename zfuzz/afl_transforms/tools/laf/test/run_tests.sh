make clean test0.laf.zafl

cleanup() {
	rm test0.*.out
	make clean
}

report_failure() {
	echo "Test $1: FAIL"
	cleanup
	exit 1
}

report_success() {
	echo "Test $1: PASS"

}

echo "1" | ./test0.exe > test0.exe.out
echo "1" | ./test0.laf.zafl > test0.laf.out
diff test0.exe.out test0.laf.out
if [ ! $? -eq 0 ]; then
	report_failure "equality"
else
	report_success "equality"
fi

echo "2" | ./test0.exe > test0.exe.out
echo "2" | ./test0.laf.zafl > test0.laf.out
if [ ! $? -eq 0 ]; then
	report_failure "inequality"
else
	report_success "inequality"
fi

echo "4" | ./test0.exe > test0.exe.out
echo "4" | ./test0.laf.zafl > test0.laf.out
if [ ! $? -eq 0 ]; then
	report_failure "inequality"
else
	report_success "inequality"
fi

echo Sanity tests passed
cleanup
exit 0
