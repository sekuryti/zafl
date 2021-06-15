#/bin/bash

export ZAFL_PATH=git.zephyr-software.com:4567/opensrc/zafl/
export ZAFL_TAG=zafl:latest
export DOCKER_ZAFL=${ZAFL_PATH}${ZAFL_TAG}

do_docker_clean()
{
	if [[ $CICD_WEEKLY == 1 ]]; then
		docker system prune -a -f
	fi
}



do_build_image()
{
	rm -rf install
	mkdir install
	cp -r ../get-packages.sh install
	cp -r ../set_env_vars install
	cp -r ../zafl_plugins/ install
	cp -r ../tools/ install
	cp -r ../bin/ install
	# if we fail here, continue on so we put "install" back in the right place.
	# the test should stop this
	docker build -t $DOCKER_ZAFL . || true	
}


do_test()
{
	local tmppath=/tmp/$(whoami)
	mkdir -p $tmppath
	chmod 777 $tmppath
	# use the container to xform /bin/ls
	cp $(which cat) $tmppath

	rm -f $tmppath/cat.zafl

	# map /io inside of containter to /tmp locally
	docker run -v $tmppath:/io -t $DOCKER_ZAFL /io/cat /io/cat.zafl
	ldd $tmppath/cat.zafl 
	ldd $tmppath/cat.zafl | grep -v "not found"

	# verify functional correctness
	echo "Verify functional correctness of cat"
	echo "hello" > $tmppath/hello
	$tmppath/cat.zafl $tmppath/hello > $tmppath/hello2
	diff $tmppath/hello $tmppath/hello2
	rm $tmppath/hello $tmppath/hello2
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
        if [[ $CICD_WEEKLY == 1 ]]; then
                docker login -u $CI_DEPLOY_USER -p $CI_DEPLOY_PASSWORD $DOCKER_ZAFL
        fi


	do_docker_clean
	do_build_image
	do_test
	do_push
	do_logout
}

main "$@"
