FROM francipvb/dgd

LABEL author.name="Francisco R. Del Roio"
LABEL author.email="francipvb@hotmail.com"

# We must create an unprivileged user
RUN adduser -DH mudserver
RUN mkdir /app
RUN chown mudserver /app
USER mudserver
WORKDIR /app
RUN mkdir mud state
VOLUME state/
COPY --chown=mudserver cloud-server/src/ mud/
COPY --chown=mudserver entrypoint.sh .
COPY --chown=mudserver cloud.dgd .
RUN chmod +x entrypoint.sh

ENTRYPOINT ["/app/entrypoint.sh"]