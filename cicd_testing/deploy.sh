#/bin/bash

export ZAFL_PATH=git.zephyr-software.com:4567/allnp/zafl_umbrella/
export ZAFL_TAG=zafl:latest
export DOCKER_ZAFL=${ZAFL_PATH}${ZAFL_TAG}


do_clean()
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
	cp $(which ls) /tmp
	docker run  -v /tmp:/io -t $DOCKER_ZAFL /io/ls /io/ls.zafl
	ldd /tmp/ls.zafl
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
	cd /tmp/zafl_test/cicd_testing
	do_clean
	do_login
	do_build_image
	do_test
	do_push
	do_logout
}

main "$@"
