# DogeGB

Game Boy's Cold Storage Wallet for Dogecoin, Pepecoin, and Bellscoin!


https://github.com/user-attachments/assets/fb31d778-f8cc-49e6-b118-f0ea7d067f48



## Building

Requirements:
- Python3
- GBDK (Game Boy Development Kit):
    - Download from: [https://gbdk-2020.github.io/gbdk-binaries/](https://github.com/gbdk-2020/gbdk-2020)
    - Extract to project root as `gbdk/` folder

To build `DogeGB.gb`
```bash
make
```

The default coin mode on startup is Dogecoin, but the app can be built to default to Pepecoin or Bellscoin instead by using the `MODE` flag on build.

To build `PepeGB.gb`
```bash
make MODE=Pepe
```

To Build `BellsGB.gb`
```bash
make MODE=Bells
```


## Unique Build-Time Entropy

If you are concerned about runtime entropy generation being insufficient, you can add build-time entropy yourself by running:

```bash
make entropy && make
```



## Testing

```bash
make test
```

Runs crypto validation tests to help ensure wallet generation is correct.


```bash
python3 tools/corrupt_rom.py ./build/DogeGB.gb
```

Creates a corrupted rom (flips random bit) to verify ROM integrity checks work.



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





## Preview

![gb](https://github.com/user-attachments/assets/044451bf-7f35-4be1-bdfa-6c1b8b7aa620)
![ds](https://github.com/user-attachments/assets/410771f3-0140-4c07-885f-b7ed9e8cb8a1)


*Disclaimer: DogeGB is an independent, open-source project made using GBDK and is not affiliated with, endorsed by, or licensed by Nintendo. No Nintendo assets were used in its creation. "Game Boy" and "Game Boy Color" are registered trademarks of Nintendo. All trademarks are the property of their respective owners.*
