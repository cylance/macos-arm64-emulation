# macos-arm64-emulation
Use the following guide to download and configure all of the necessary tools and files for emulating the macOS arm64e kernel. The guide begins in this project's root directory, ie. the same directory as this README file.
# Install decompression tools
Get, patch, and build xar:
```
git clone https://github.com/mackyle/xar.git
cd xar/xar
sed -i 's/OpenSSL_add_all_ciphers/OPENSSL_init_crypto/g' configure.ac
cat > ext2.patch << EOF
--- ./lib/ext2.c
+++ ./lib/ext2.c
@@ -140,8 +140,10 @@
    if(! (flags & ~EXT2_NOCOMPR_FL) )
        x_addprop(f, "NoCompBlock");
 #endif
+#ifdef EXT2_ECOMPR_FL
    if(! (flags & ~EXT2_ECOMPR_FL) )
        x_addprop(f, "CompError");
+#endif
    if(! (flags & ~EXT2_BTREE_FL) )
        x_addprop(f, "BTree");
    if(! (flags & ~EXT2_INDEX_FL) )
@@ -229,8 +231,10 @@
    if( e2prop_get(f, "NoCompBlock", (char **)&tmp) == 0 )
        flags |= EXT2_NOCOMPR_FL ;
 #endif
+#ifdef EXT2_ECOMPR_FL
    if( e2prop_get(f, "CompError", (char **)&tmp) == 0 )
        flags |= EXT2_ECOMPR_FL ;
+#endif
    if( e2prop_get(f, "BTree", (char **)&tmp) == 0 )
        flags |= EXT2_BTREE_FL ;
    if( e2prop_get(f, "HashIndexed", (char **)&tmp) == 0 )
EOF
git apply --ignore-whitespace ext2.patch
./autogen.sh
make
cd ../..
XAR=./xar/xar/src/xar
```
Get and build lzfse:
```
git clone https://github.com/lzfse/lzfse.git
cd lzfse
make
cd ..
LZFSE=./lzfse/build/bin/lzfse
```
# Getting the files
Fetch the installer package (NOTE: this is a very large ~12GB file):
```
wget http://swcdn.apple.com/content/downloads/00/55/001-86606-A_9SF1TL01U7/5duug9lar1gypwunjfl96dza0upa854qgg/InstallAssistant.pkg
```
(UPDATE: Unfortunately, Apple has removed the above link and it is no longer valid. Click [here](https://mega.nz/file/GZwzGYKb#HscZIOg_K5JdUIvbLwwwW7_Ntc1z9c7QPOcEQRKwp8c) to download the files, extract to the root of the project, do [Patching the Device Tree](#Patching the Device Tree), and then skip to the [Building QEMU](#building-qemu) section.)

Extract the kernel binaries:
```
cd ../../
$XAR -xf InstallAssistant.pkg SharedSupport.dmg
7z e SharedSupport.dmg 5.hfs
rm SharedSupport.dmg ._SharedSupport.dmg
7z e -so 5.hfs "Shared Support/SFR/com_apple_MobileAsset_SFRSoftwareUpdate/aabc1798a59cc185ea5a87bfd4dec012f4b7feb1.zip" > sfr.zip
7z e -so 5.hfs "Shared Support/com_apple_MobileAsset_MacSoftwareUpdate/6c799f422b6d995ccc7f3fb669fe3246fd9f61aa.zip" > mac.zip
rm 5.hfs
7z e sfr.zip AssetData/usr/standalone/update/ramdisk/arm64eSURamDisk.dmg
7z e sfr.zip AssetData/boot/Firmware/all_flash/DeviceTree.j273aap.im4p
7z e sfr.zip AssetData/boot/kernelcache.release.j273
```
Decode the kernel binaries:
```
git clone https://github.com/alephsecurity/xnu-qemu-arm64-tools.git
SCRIPTS=xnu-qemu-arm64-tools/bootstrap_scripts
python $SCRIPTS/asn1kerneldecode.py kernelcache.release.j273 kernelcache.release.j273.asn1decoded
python $SCRIPTS/asn1rdskdecode.py arm64eSURamDisk.dmg arm64eSURamDisk.dmg.asn1decoded
python $SCRIPTS/asn1dtredecode.py DeviceTree.j273aap.im4p DeviceTree.j273aap.im4p.asn1decoded
$LZFSE -decode -i kernelcache.release.j273.asn1decoded -o kernelcache.release.j273.out
$LZFSE -decode -i DeviceTree.j273aap.im4p.asn1decoded -o DeviceTree.j273aap.im4p.out
cp arm64eSURamDisk.dmg.asn1decoded arm64eSURamDisk.dmg.out
```
# Patching the Device Tree
Build `dtetool` and patch the device tree file:
```
cd dtetool
./build.sh
./dtetool ../DeviceTree.j273aap.im4p.out -d dtediff_20C69 -o ../DeviceTree.j273aap.im4p.out.patched
cd ..
```
# Expanding the ramdisk in macOS
This step can only be done on a macOS system. Copy the ramdisk onto a macOS system and expand it:
```
hdiutil resize -size 1.5G -imagekey diskimage-class=CRawDiskImage arm64eSURamDisk.dmg.out
```
# Getting the user binaries
Copy the expanded ramdisk back onto the Linux system and extract the user binaries:
```
7z e mac.zip AssetData/Restore/022-10310-098.dmg
rm mac.zip
7z e 022-10310-098.dmg "3 - Apple_APFS"
rm 022-10310-098.dmg
```
Mount the filesystem and ramdisk to two new directories:
```
mkdir apfs ramdisk
apfs-fuse -o allow_other "3 - Apple_APFS" apfs
sudo mount -t hfsplus -o force,rw arm64eSURamDisk.dmg.out ramdisk
```
Transfer the user binaries to ramdisk:
```
sudo cp -rn apfs/root/bin/* ramdisk/bin/
sudo cp -rn apfs/root/sbin/* ramdisk/sbin/
sudo cp -rn apfs/root/usr/bin/* ramdisk/usr/bin/
sudo cp -rn apfs/root/usr/sbin/* ramdisk/usr/sbin/
```
Remove all existing launchd profiles and create a profile for bash:
```
sudo rm -rf ramdisk/System/Library/LaunchDaemons/*
cat > com.apple.bash.plist << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "https://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
        <key>Label</key>
        <string>com.apple.bash</string>
        <key>Umask</key>
        <integer>0</integer>
        <key>RunAtLoad</key>
        <true/>
        <key>ProgramArguments</key>
        <array>
                <string>/bin/bash</string>
        </array>
        <key>StandardInPath</key>
        <string>/dev/console</string>
        <key>StandardOutPath</key>
        <string>/dev/console</string>
        <key>StandardErrorPath</key>
        <string>/dev/console</string>
        <key>POSIXSpawnType</key>
        <string>Interactive</string>
        <key>EnablePressuredExit</key>
        <false/>
        <key>UserName</key>
        <string>root</string>
</dict>
</plist>
EOF 
sudo cp com.apple.bash.plist ramdisk/System/Library/LaunchDaemons/
```
Unmount the disk images:
```
sudo umount apfs ramdisk
```
# Building QEMU
Download, extract, and patch the QEMU 5.1.0 source:
```
wget https://download.qemu.org/qemu-5.1.0.tar.xz
tar xf qemu-5.1.0.tar.xz
mv qemu-5.1.0 xnu-qemu-arm64-5.1.0
git apply xnu-qemu-arm64-5.1.0.diff
```
Configure and build the source:
```
cd xnu-qemu-arm64-5.1.0
./configure --target-list=aarch64-softmmu --disable-capstone --disable-pie --disable-slirp
make -j6
cd ..
```
Modify the `-j6` option according to the number of cores on your CPU times 1.5.
# Start the emulator
Start the emulator with the following script:
```
./xnu-qemu-arm64-5.1.0/aarch64-softmmu/qemu-system-aarch64 \
-M macos11-j273-a12z,\
kernel-filename=kernelcache.release.j273.out,\
dtb-filename=DeviceTree.j273aap.im4p.out.patched,\
ramdisk-filename=arm64eSURamDisk.dmg.out,\
kern-cmd-args="kextlog=0xfff cpus=1 rd=md0 serial=2 -noprogress",\
xnu-ramfb=off \
-cpu max \
-m 6G \
-serial mon:stdio \
-nographic \
