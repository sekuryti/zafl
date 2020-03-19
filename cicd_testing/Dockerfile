#FROM ubuntu:18.04
FROM git.zephyr-software.com:4567/opensrc/irdb-sdk/zipr-dev:latest
RUN DEBIAN_FRONTEND=noninteractive apt-get update &&   \
    DEBIAN_FRONTEND=noninteractive apt-get install -yq \
	postgresql  				\
	postgresql-client  			\
	libpqxx-dev				\
	sudo 					\
	nasm

COPY ./install /zafl
RUN cd /zafl && ./get-packages.sh
RUN cd /zafl/zipr_umbrella/ && service postgresql start && sleep 10 && env USER=root ./postgres_setup.sh
COPY run_zafl.sh /
RUN chmod +x /run_zafl.sh
ENTRYPOINT [ "/bin/bash", "-c", "/run_zafl.sh \"$@\"", "--" ]
CMD [ ] 
