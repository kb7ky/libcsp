FROM node:16

# Run as
RUN apt-get -y update
RUN groupadd -r logio && useradd -r -g logio logio

# Create app directory
WORKDIR /usr/src/app
RUN chown logio:logio /usr/src/app

RUN npm install log.io log.io-file-input

COPY server.json file.json run_logio.sh ./
RUN chmod +x run_logio.sh

USER logio

ENV LOGIO_SERVER_CONFIG_PATH=server.json
ENV LOGIO_FILE_INPUT_CONFIG_PATH=file.json

EXPOSE 6689
EXPOSE 6688

CMD [ "bash", "-c", "./run_logio.sh"]