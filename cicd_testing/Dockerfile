FROM git.zephyr-software.com:4567/opensrc/irdb-cookbook-examples/zipr-dev:latest

# /home/zuser was created in the base image
COPY ./install /home/zuser/zafl
RUN sudo chown zuser:zuser -R /home/zuser/zafl

# install Zafl-specific packages
USER root
RUN cd /home/zuser/zafl && ./get-packages.sh

COPY run_zafl.sh /
RUN chmod +x /run_zafl.sh
RUN sudo chown zuser:zuser /run_zafl.sh

USER zuser

ENV PEASOUP_HOME=/opt/ps_zipr
ENV IRDB_LIBS=/opt/ps_zipr/irdb-libs/lib
ENV IRDB_SDK=/home/zuser/irdb-sdk
ENV ZAFL_HOME=/home/zuser/zafl
ENV PSPATH=/opt/ps_zipr/irdb-libs/plugins_install:/home/zuser/zafl/plugins_install
ENV PATH="/opt/ps_zipr/tools:/home/zuser/zafl/bin:${PATH}"

#RUN cd /zafl/zipr_umbrella/ && service postgresql start && sleep 10 && env USER=root ./postgres_setup.sh
ENTRYPOINT [ "/bin/bash", "-c", "/run_zafl.sh \"$@\"", "--" ]
CMD [ ] 
