# DogeGB

Game Boy's First Cold Storage Wallet for Dogecoin!


https://github.com/user-attachments/assets/fb31d778-f8cc-49e6-b118-f0ea7d067f48



## Building

Requires GBDK (Game Boy Development Kit):
- Download from: [https://gbdk-2020.github.io/gbdk-binaries/](https://github.com/gbdk-2020/gbdk-2020)
- Extract to project root as `gbdk/` folder

Then:
```bash
make
```

*Build may need some adjustments to run on Windows/Linux*

## Testing

```bash
make .test
```

Runs crypto validation tests to help ensure wallet generation is correct.


```bash
python3 tools/corrupt_rom.py ./build/DogeGB.gb
```

Creates a corrupted rom (flips random bit) to verify ROM integrity checks work.



## Features

- Generate BIP39 mnemonic phrases
- Create Dogecoin wallet addresses
- Built-in entropy generation via "Bonk Time" game
- QR address display
- Air-gapped operation
- ROM integrity verification


## Security Considerations

### Platform Verification

__IMPORTANT: Verify the platform yourself.__

Before using DogeGB to store crypto, generate a dummy test address and verify the backup words generate the same address on a known working wallet.

You can use MyDoge, Nintondo Wallet, and a few others to do so.

### Entropy

Entropy is the elephant in the room on these old devices. Modern hardware has great sources of randomness, but those aren't available on these primitive devices.

> Random numbers should not be generated with a method chosen at random. -Donald Knuth

The consequences of bad run-time entropy are catastrophic. A good example is the Milk Sad/Libbitcoin bug, where wallets were only seeded with system time (32 bits of entropy). This made address generation predictable and easy to brute force leading to over 2,600 wallets being pwned with losses estimated around $1 million.

To attempt to generate a decent source of run-time entropy the 'Bonk Time!' mini game requires 72 button presses where it polls input keys, hardware registers, and measures input deltas.

### Defense in Depth

In the case where 'Bonk Time!' generates catastrophically bad entropy I wanted to avoid the worst case scenario of "Contagion": if one devices gets pwned, all devices get pwned.

Out of an abundance of caution, during the build process random data from the build computer is injected into the app. This ensures each build (each user) of the app has it's own baseline, requiring a hacker to not only exploit low-entropy but also compromise the build computer (or the cartridge itself).

Unfortunately, this means users will be required to build the software themselves. In a cartridge release scenario, each cartridge would have its own unique build of the software.

ROM integrity verification is intended to detect bit rot from hardware failure, which prevents the app from (worst case) silently generating unspendable addresses due to corruption. It is not intended to prevent motivated attackers from malicious edits of the ROM. 

### Healthy Practices

Treat this like a paper wallet. Backup words are not encrypted on device and deleting the backup words won't necessarily prevent forensic recovery.

That means:

- Keep it physically secure
- Keep cameras away from the screen
- For Flash Cartridges, use a dedicated SD Card that never gets plugged into a computer again!

### Hardware Specific Issues

Security is not a Yes/No question, it's a measure of risk. And the risks varies depending on the device you are running DogeGB on.

Generally speaking, original GameBoy/Color hardware are going to be less risky than other devices.

The GameBoy Advance (depending on cartridge type) and the DS emulate DogeGB which could lead to reduced entropy generation during "Bonk Time!".

The DS itself has WiFi, so it's not air-gapped.

Modern remakes of the GameBoy may have their own risks, like wifi chips or video out via USB port. So be extra cautious when using those.

Using the wallet on desktop emulation is about the worst you can do. You can do it for fun but don't use those addresses to store crypto.


## Preview

![gb](https://github.com/user-attachments/assets/044451bf-7f35-4be1-bdfa-6c1b8b7aa620)
![ds](https://github.com/user-attachments/assets/410771f3-0140-4c07-885f-b7ed9e8cb8a1)


*Disclaimer: DogeGB is an independent, open-source project made using GBDK and is not affiliated with, endorsed by, or licensed by Nintendo. No Nintendo assets were used in its creation. "Game Boy" and "Game Boy Color" are registered trademarks of Nintendo. All trademarks are the property of their respective owners.*
