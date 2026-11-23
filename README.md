# A minimal linux distro for raspberry pi 4b 

A learning project: a minimal Buildroot Linux image for the Raspberry Pi 4B, plus a Linux I2C driver for the BMP280 temperature/pressure sensor, written step by step.

## Tested with

| Item | Version |
|---|---|
| Board | Raspberry Pi 4B (4 GB) |
| Kernel | 6.12.x (Raspberry Pi kernel, built by Buildroot) |
| Buildroot | 2026.02.1 |
| Host | Ubuntu / any Linux |

## Repo layout

```
.
├── config/
│   ├── rpi4b_defconfig     # Buildroot config
│   └── linux.config        # kernel config
├── rasberrypi_overlay/     # rootfs overlay (files copied into the image)
│   └── etc/
│       ├── network/interfaces
│       └── ssh/sshd_config
├── examples/
│   └── wpa_supplicant.conf.example   # copy and edit (see step 4)
└── README.md
```
## 1. Host prerequisites

Buildroot needs the usual build tools. On Debian/Ubuntu:

```bash
sudo apt install build-essential git wget cpio unzip rsync bc file libncurses-dev \
    python3 libssl-dev
```

See the Buildroot manual for the full list for your Buildroot version.

## 2. Get the sources

```bash
git clone <this-repo-url> my_rasberrypi_project
git clone https://gitlab.com/buildroot.org/buildroot.git
```

If you want the exact Buildroot revision used here, check it out at the commit listed in the table above.

## 3. Load the Buildroot config

```bash
cd buildroot
cp ../my_rasberrypi_project/config/rpi4b_defconfig configs/
make rpi4b_defconfig
```

### Fix the paths for your machine

The defconfig contains **absolute paths from the author's machine**. Change them to where you cloned this repo. Either use `make menuconfig`:

- System configuration → *Root filesystem overlay directories* → `/path/to/my_rasberrypi_project/rasberrypi_overlay`
- Kernel → *Configuration file path* → `/path/to/my_rasberrypi_project/config/linux.config`

or edit these lines in `.config`/the defconfig before building:

```
BR2_ROOTFS_OVERLAY="/path/to/my_rasberrypi_project/rasberrypi_overlay"
BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE="/path/to/my_rasberrypi_project/config/linux.config"
```

Check that `BR2_LINUX_KERNEL_USE_CUSTOM_CONFIG=y` is set.

## 4. Wi-Fi credentials (not stored in the repo)

```bash
cd my_rasberrypi_project
cp examples/wpa_supplicant.conf.example rasberrypi_overlay/etc/wpa_supplicant.conf
# edit it: set your SSID and password
```

Skip this if you plan to use Ethernet only.

## 5. Login credentials (development only)

The image is configured with:

- user: `root`
- password: `123`
- SSH: root login with password is **enabled** in `sshd_config`

This is a deliberate shortcut for a dev board on a home network. Change the password (`menuconfig` → System configuration → Root password) and do not expose the board to the internet.

## 6. Build

```bash
cd buildroot
make -j"$(nproc)"
```

The first build takes a long time. The image ends up at `output/images/sdcard.img`.

## 7. Write the image

Find your target device first, and check twice:

```bash
lsblk        # run before and after plugging the drive in; the new entry is yours
sudo umount /dev/sdX* 2>/dev/null
sudo dd if=output/images/sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync
```

Replace `/dev/sdX` with the whole disk (not a partition). Writing to the wrong device destroys its data.

## 8. Boot configuration

Mount the boot (FAT) partition and check `config.txt` and `cmdline.txt`.

`config.txt` should contain:

```
enable_uart=1
dtparam=i2c_arm=on
```

`cmdline.txt`: the `root=` value must match where you boot from.

- SD card: `root=/dev/mmcblk0p2` (or the PARTUUID form)
- USB drive: `root=/dev/sda2`

If the kernel log stops at `Waiting for root device`, this is the cause: the root device in `cmdline.txt` does not match the device the system is on.

> first driver i wrote for this image : [custom bmp280 i2c device driver for raspberry pi](https://github.com/codedby-aadarsh/bmp280-i2c-device-driver)