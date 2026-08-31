# test/fixtures

`fake_device.hmac` is the PUBLIC testbed golden key (000102...1f) from the
protocol golden vectors — NOT a secret. It is used exclusively by
tools/fake_device.py and the integration tests. Production keys are NEVER
kept in the repository.
