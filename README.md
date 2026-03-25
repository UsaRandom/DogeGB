# DogeGB

Game Boy's Cold Storage Wallet for Dogecoin, Pepecoin, and Bellscoin!


https://github.com/user-attachments/assets/fb31d778-f8cc-49e6-b118-f0ea7d067f48


## Features

- Generate BIP39 mnemonic phrases
- Create Dogecoin, Pepecoin, & Bellscoin wallet addresses
- Built-in entropy generation via Minigame
- QR address display
- Air-gapped operation
- ROM integrity verification


## Security Considerations

### Platform Verification


Before using DogeGB to store crypto it is recommended to run the built-in test suite to verify the platform.


### Healthy Practices

Treat this like a paper wallet. Backup words are not encrypted on device and deleting the backup words won't necessarily prevent forensic recovery.

That means:

- Keep it physically secure
- Keep cameras away from the screen
- For Flash Cartridges, use a dedicated SD Card that never gets plugged into a computer again!



## Building

Requirements:
- GCC, Make
- Python3 (pip: mnemonic, base58, bip32utils)
- GBDK (Game Boy Development Kit):
    - Download from: [https://gbdk-2020.github.io/gbdk-binaries/](https://github.com/gbdk-2020/gbdk-2020)
    - Extract to project root as `gbdk/` folder

To build `DogeGB.gb`
```bash
make
```

The default coin mode on startup is Dogecoin, but the app can be built to default to Pepecoin or Bellscoin:

To build `PepeGB.gb`
```bash
make pepe
```

To build `BellsGB.gb`
```bash
make bells
```

To build all three:
```bash
make all
```

## Building on WSL (Windows)

Install WSL: 
```bash
wsl --install Debian
```

In project directory with linux gbdk:
```bash
sudo apt-get update
sudo apt-get install python3 python3-mnemonic python3-base58 python3-bip32utils gcc make
make
```


## Unique Build-Time Entropy

If you are concerned about runtime entropy generation being insufficient, you can add build-time entropy yourself by running:

```bash
make entropy && make
```


## Testing

You can run crypto validation tests against python3's implimentation by running:

```bash
make test
```

To test ROM integrity checks, you can corrupt a ROM (flip a random bit) by running:

```bash
python3 tools/corrupt_rom.py ./build/DogeGB.gb
```






*Disclaimer: DogeGB is an independent, open-source project made using GBDK and is not affiliated with, endorsed by, or licensed by Nintendo. No Nintendo assets were used in its creation. "Game Boy" and "Game Boy Color" are registered trademarks of Nintendo. All trademarks are the property of their respective owners.*
