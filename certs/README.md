# certs/

This folder contains **no** real certificates — in Docker operation the
testbed PKI is mounted read-only at `/certs`
(`../DO_NOT_COMMIT/CA`, see docker-compose.yml):

```
/certs/root/certs/ca.crt.pem            root CA (server validation)
/certs/clients/service-laptop-01.crt    client identity of this tool
/certs/clients/service-laptop-01.key    private key (0600, never in git)
```

Additional client identities are issued by the CA script:
`DO_NOT_COMMIT/CA/issue_device_cert.sh <identity> <out> v3_client <basename>`

For a native installation the layout from the design concept (§17) applies:

```
/etc/fountainer/certs/root_ca.crt
/etc/fountainer/certs/client.crt
/etc/fountainer/private/client.key      (owner fountainer, mode 0600)
```

Rules (design concept §14/§16): keys never go into git, logs or the binary;
the configuration contains file paths only. Certificate rotation is done by
swapping files + restarting, without recompilation.
