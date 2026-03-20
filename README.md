# DogeGB

Game Boy's Cold Storage Wallet for Dogecoin!


https://github.com/user-attachments/assets/fb31d778-f8cc-49e6-b118-f0ea7d067f48



## Building

Requires GBDK (Game Boy Development Kit):
- Download from: [https://gbdk-2020.github.io/gbdk-binaries/](https://github.com/gbdk-2020/gbdk-2020)
- Extract to project root as `gbdk/` folder

Then:
```bash
make
```

## Unique Builds

You can inject new entropy into DogeGB by running:

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
- Create Dogecoin wallet addresses
- Built-in entropy generation via "Bonk Time" game
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

### Hardware Specific Issues

Certain Game Boy emulators, like GameYob for DS, have low button input timing resolution. Bonk Time! will automatically detect this and adjust playtime to account for low entropy generation. You will see an 'EMULATOR DETECTED' message if this happens.




## Preview

![gb](https://github.com/user-attachments/assets/044451bf-7f35-4be1-bdfa-6c1b8b7aa620)
![ds](https://github.com/user-attachments/assets/410771f3-0140-4c07-885f-b7ed9e8cb8a1)


*Disclaimer: DogeGB is an independent, open-source project made using GBDK and is not affiliated with, endorsed by, or licensed by Nintendo. No Nintendo assets were used in its creation. "Game Boy" and "Game Boy Color" are registered trademarks of Nintendo. All trademarks are the property of their respective owners.*
