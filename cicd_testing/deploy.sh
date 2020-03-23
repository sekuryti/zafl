#/bin/bash

export ZAFL_PATH=git.zephyr-software.com:4567/opensrc/libzafl/
export ZAFL_TAG=zafl:latest
export DOCKER_ZAFL=${ZAFL_PATH}${ZAFL_TAG}

do_docker_clean()
{
	if [[ $CICD_WEEKLY == 1 ]]; then
		docker system prune -a -f
	fi
}


do_login()
{

	# login to gitlab's docker registry as gitlab-user
	docker login $ZAFL_PATH -u gitlab-runner -p 84MyuSuDo4kQat4GZ_Zs  2> /dev/null
}

do_build_image()
{
	mv ../install .
	# if we fail here, continue on so we put "install" back in the right place.
	# the test should stop this
	docker build -t $DOCKER_ZAFL . || true	
	mv install ..
}


do_test()
{
	# use the container to xform /bin/ls
	cp $(which cat) /tmp

	if [ -x /tmp/cat.zafl ]; then
		sudo rm /tmp/cat.zafl
	fi

	# map /io inside of containter to /tmp locally
	docker run -v /tmp:/io -t $DOCKER_ZAFL /io/cat /io/cat.zafl
	ldd /tmp/cat.zafl 
	ldd /tmp/cat.zafl | grep -v "not found"

	# verify functional correctness
	echo "Verify functional correctness of cat"
	echo "hello" > /tmp/hello
	/tmp/cat.zafl /tmp/hello > /tmp/hello2
	diff /tmp/hello /tmp/hello2
	rm /tmp/hello /tmp/hello2
}

do_push()
{
	if [[ $CICD_WEEKLY == 1 ]]; then
		docker push ${DOCKER_ZAFL}
	fi
}

do_logout()
{
	docker logout $ZAFL_PATH
}

main()
{
	set -e 
	set -x 
	if [[ -z $PEASOUP_HOME ]] ; then
		cd $CICD_MODULE_WORK_DIR/zafl_test
		source set_env_vars
		cd /tmp/zafl_tmp
		source set_env_vars
		cd cicd_testing
	fi

	do_docker_clean
	do_login
	do_build_image
	do_test
	do_push
	do_logout
}

main "$@"
