# Protocol

## Transport

The node and the gateway each carry an RYLR998 LoRa module. The node uses
UART1 (GP8/GP9); the gateway uses the second module on the computer USB serial
port. The modules are configured with AT commands:

```
AT+ADDRESS=<node>
AT+NETWORKID=<net>
AT+BAND=<hz>
AT+PARAMETER=<sf>,<bw>,<cr>,<preamble>
AT+SEND=<dest>,<len>,<payload>
+RCV=<src>,<len>,<data>,<rssi>,<snr>
```

## Payload

The payload is the lowercase hex encoding of a sealed envelope. The plaintext
telemetry is JSON:

```
{"n":37,"s":12,"m":240}
```

where `n` is the node id, `s` is the monotonic sequence number, and `m` is the
last reaction time in milliseconds.

## Envelope

```
nonce(24) | ciphertext | tag(16)
```

The nonce is random per frame. The associated data is the single byte node id.
The envelope is hex encoded for the AT payload path.

## Key

The field key is 32 bytes derived with Argon2id:

- passphrase: `picokit field key v1`
- salt: `picokit-salt-0001` (16 bytes)
- time cost 3, parallelism 1, memory 64 blocks

In production the key is provisioned per device through OTP; the passphrase
here is a lab default.

## Game

The round arms with a random delay of 0.5 to 2.5 seconds, lights the onboard
LED, and opens a 3 second press window. A press records the reaction time; an
expired window records a miss.
