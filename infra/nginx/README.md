Self-signed TLS placeholder for the gateway.

To enable HTTPS in `nginx.conf`, mount these files into the container:

- `/etc/nginx/certs/selfsigned.crt`
- `/etc/nginx/certs/selfsigned.key`

Example:

```sh
openssl req -x509 -nodes -days 365 \
  -newkey rsa:2048 \
  -keyout selfsigned.key \
  -out selfsigned.crt \
  -subj "/CN=localhost"
```
