# Using a multi-stage build since we use the private deploy key to fetch from github.
# See https://vsupalov.com/build-docker-image-clone-private-repo-ssh-key/
FROM gcc:8.3.0 as intermediate

ENV BENCH "intspeed"
ENV SIZE "test"

# Change the shell to bash.
SHELL ["/bin/bash", "-c"]

RUN apt-get update && apt-get install -y git

# Add credentials on build.
ARG SSH_PRIVATE_KEY 
RUN mkdir /root/.ssh/
RUN echo "${SSH_PRIVATE_KEY}" > /root/.ssh/id_rsa
RUN chmod 600 /root/.ssh/id_rsa

# Make sure the domain is accepted.
RUN touch /root/.ssh/known_hosts
RUN ssh-keyscan github.com >> /root/.ssh/known_hosts

# Clone shadow_guard repository.
RUN mkdir /root/shadow_guard \
    && cd /root/shadow_guard \
    && git clone git@github.com:iu-parfunc/liteCFI.git . \
    && git checkout retguard && git pull

# Now create the final image.
FROM gcc:8.3.0
RUN apt-get update && apt-get install -y vim gdb git
# Copy the repository from the intermediate image.
COPY --from=intermediate /root/shadow_guard /home/shadow_guard 

# Install cmake.
RUN wget -qO- "https://cmake.org/files/v3.15/cmake-3.15.3-Linux-x86_64.tar.gz" | tar --strip-components=1 -xz -C /usr/local

# Install bazel.
RUN apt-get update && apt-get install -y openjdk-11-jdk
RUN echo "deb [arch=amd64] https://storage.googleapis.com/bazel-apt stable jdk1.8" | tee /etc/apt/sources.list.d/bazel.list
RUN curl https://bazel.build/bazel-release.pub.gpg | apt-key add -
RUN apt-get update && apt-get install -y bazel
RUN apt-get install --only-upgrade bazel

# Download and extract spec benchmarks.
RUN cd /home/ \
    && curl -L -o benchmark.tar.gz https://www.dropbox.com/s/19xcn3rcai3cp5r/benchmark.tar.gz?dl=1 \
    && tar -zxvf benchmark.tar.gz \
    && cp shadow_guard/docker/gcc-config.cfg spec_cpu2017/config 

RUN mkdir /home/results

# Run the spec benchmarks.
CMD /bin/bash -c "cd /home/shadow_guard/ \
    && ./bazel.sh deps \
    && ./bazel.sh build --debug \
    && cd ../spec_cpu2017 \
    && source shrc \
    && runcpu --config=gcc-config.cfg --size=$SIZE $BENCH \
    && cp result/* ../results/"
