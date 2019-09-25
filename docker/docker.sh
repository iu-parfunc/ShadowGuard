pkey=`cat id_rsa`
docker build --build-arg SSH_PRIVATE_KEY="$pkey" -t `whoami`/shadow_guard .
