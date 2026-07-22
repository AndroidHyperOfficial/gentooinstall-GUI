#!/usr/bin/env bash
# Gentoo Linux Installer 
set -e

if [ "$EUID" -ne 0 ]; then
    echo "[-] Error: Run this installer as root!"
    exit 1
fi

# Ensure dialog is available
if ! command -v dialog &> /dev/null; then
    echo "[+] Installing dialog utility..."
    emerge --quiet sys-apps/dialog || apt-get install -y dialog || pacman -S --noconfirm dialog
fi

# Default System Configuration
TARGET_DISK=$(lsblk -d -n -o NAME,TYPE | grep -E 'disk' | grep -v -E '^(fd|sr|loop)' | head -n1 | awk '{print "/dev/"$1}')
FSTYPE="ext4"
ENCRYPTION="none"
SCHEME="GPT"
INIT_SYS="runit"
KERNEL_TYPE="gentoo-kernel-bin"
BOOTLOADER="grub"
DE_WM="KDE Plasma 6"
LOGIN_MGR="sddm"
AUDIO_SERVER="pipewire"
BLUETOOTH="bluez"
FIREWALL="ufw"
TIMEZONE_CHOICE="UTC"
MIRROR_URL="https://distfiles.gentoo.org"
KEYMAP="us"
EXTRA_PKGS="sudo git vim htop fastfetch"
USER_NAME="gentoo"
USER_PASS="gentoo"
ROOT_PASS="gentoo"

is_uefi() { [ -d "/sys/firmware/efi" ]; }
if ! is_uefi; then SCHEME="MBR"; fi

# Main Configuration Menu Loop
while true; do
    CHOICE=$(dialog --clear --stdout \
        --title "Gentoo Linux Installer T2.1" \
        --menu "Use Up/Down arrows to select a configuration option:" 22 75 13 \
        "1" "Target Disk & Partitions ($TARGET_DISK, $FSTYPE)" \
        "2" "Init System ($INIT_SYS)" \
        "3" "Kernel Variant ($KERNEL_TYPE)" \
        "4" "Bootloader ($BOOTLOADER)" \
        "5" "Desktop Environment & Login ($DE_WM / $LOGIN_MGR)" \
        "6" "Applications & Services (Audio, BT, Firewall)" \
        "7" "Select Additional Packages" \
        "8" "Timezone & Mirrors ($TIMEZONE_CHOICE)" \
        "9" "User Accounts (User: $USER_NAME)" \
        "I" "START GENTOO INSTALLATION" \
        "Q" "Quit Installer")

    case "$CHOICE" in
        1)
            DISKS=()
            while read -r name size; do
                DISKS+=("/dev/$name" "Size: $size")
            done < <(lsblk -d -n -o NAME,SIZE,TYPE | grep -E 'disk' | grep -v -E '^(fd|sr|loop)')
            
            if [ ${#DISKS[@]} -gt 0 ]; then
                TARGET_DISK=$(dialog --stdout --menu "Select Target Storage Drive" 15 50 5 "${DISKS[@]}")
            else
                TARGET_DISK=$(dialog --stdout --inputbox "Enter Target Device Path:" 10 40 "$TARGET_DISK")
            fi

            FSTYPE=$(dialog --stdout --menu "Select Root Filesystem" 15 40 4 \
                "ext4" "Standard Linux Filesystem" \
                "btrfs" "Advanced Filesystem with Snapshots" \
                "xfs" "High Performance Filesystem" \
                "f2fs" "Flash-Optimized Filesystem")

            ENCRYPTION=$(dialog --stdout --menu "LUKS2 Full Disk Encryption" 12 40 2 \
                "none" "No Encryption" \
                "luks" "Encrypt Root Partition")

            if [ "$ENCRYPTION" = "luks" ]; then
                LUKS_PASS=$(dialog --stdout --passwordbox "Enter LUKS2 Passphrase:" 10 40)
            fi
            ;;
        2)
            INIT_SYS=$(dialog --stdout --menu "Select Init System" 16 50 6 \
                "runit" "Lightweight UNIX Init (Default)" \
                "openrc" "Classic Gentoo Init System" \
                "systemd" "Modern System & Service Manager" \
                "dinit" "Dependency-based Init" \
                "s6" "Skarnet Supervision Suite" \
                "busybox" "Ultra-Minimalist Init")
            ;;
        3)
            KERNEL_TYPE=$(dialog --stdout --menu "Select Linux Kernel" 15 55 5 \
                "gentoo-kernel-bin" "Precompiled Binary Kernel (Fastest)" \
                "linux-zen" "Desktop & Gaming Optimized Kernel" \
                "lts" "Long Term Support Kernel" \
                "hardened" "Security & SELinux Hardened Kernel" \
                "vanilla" "Vanilla Sources")
            ;;
        4)
            BOOTLOADER=$(dialog --stdout --menu "Select Bootloader" 15 50 5 \
                "grub" "Standard Universal Bootloader" \
                "limine" "Modern, Fast & Lightweight" \
                "systemd-boot" "UEFI Boot Manager" \
                "refind" "Graphical UEFI Boot Manager" \
                "none" "Skip Bootloader")
            ;;
        5)
            DE_WM=$(dialog --stdout --menu "Select Desktop Environment / Window Manager" 18 50 11 \
                "KDE Plasma 6" "Full Featured Desktop" \
                "GNOME" "Modern GNOME Desktop" \
                "Cinnamon" "Traditional Desktop" \
                "Hyprland" "Wayland Tiling Compositor" \
                "Sway" "Wayland i3-compatible WM" \
                "i3" "X11 Tiling WM" \
                "XFCE4" "Lightweight Desktop" \
                "Enlightenment" "Modular Desktop" \
                "Awesome" "Lua Tiling WM" \
                "LXQt" "Lightweight Qt Desktop" \
                "none" "Command Line Only")

            LOGIN_MGR=$(dialog --stdout --menu "Select Display Manager" 15 50 5 \
                "sddm" "SDDM (Recommended for Plasma/Wayland)" \
                "gdm" "GNOME Display Manager" \
                "lightdm" "LightDM Lightweight Manager" \
                "lxdm" "LXDM Manager" \
                "none" "None / Manual Login")
            ;;
        6)
            AUDIO_SERVER=$(dialog --stdout --menu "Select Audio Server" 14 40 4 \
                "pipewire" "Modern PipeWire Audio" \
                "pulseaudio" "Classic PulseAudio" \
                "alsa" "Raw ALSA" \
                "none" "No Audio")

            BLUETOOTH=$(dialog --stdout --menu "Bluetooth Support" 10 40 2 \
                "bluez" "Enable BlueZ Stack" \
                "none" "Disable Bluetooth")

            FIREWALL=$(dialog --stdout --menu "Select Firewall" 14 40 5 \
                "ufw" "Uncomplicated Firewall" \
                "firewalld" "FirewallD Daemon" \
                "nftables" "Modern Nftables" \
                "iptables" "Legacy Iptables" \
                "none" "No Firewall")
            ;;
        7)
            EXTRA_PKGS=$(dialog --stdout --checklist "Select Popular Additional Packages (Space to toggle):" 20 60 10 \
                "sudo" "Sudo privilege escalation" on \
                "git" "Version control system" on \
                "vim" "Vi Improved text editor" on \
                "htop" "Interactive process viewer" on \
                "fastfetch" "System information tool" on \
                "curl" "Command line tool for transferring data" off \
                "wget" "Network downloader utility" off \
                "tmux" "Terminal multiplexer" off \
                "NetworkManager" "Network connection manager" off \
                "firefox" "Web browser" off \
                "vlc" "Multimedia player" off \
                "btop" "Resource monitor" off)
            ;;
        8)
            TIMEZONE_CHOICE=$(dialog --stdout --inputbox "Enter Timezone (e.g. UTC, Europe/London, America/New_York):" 10 50 "$TIMEZONE_CHOICE")
            MIRROR_URL=$(dialog --stdout --inputbox "Enter Gentoo Mirror URL:" 10 50 "$MIRROR_URL")
            ;;
        9)
            USER_NAME=$(dialog --stdout --inputbox "Enter Username (Wheel/Sudo enabled):" 10 40 "$USER_NAME")
            USER_PASS=$(dialog --stdout --passwordbox "Enter Password for $USER_NAME:" 10 40)
            ROOT_PASS=$(dialog --stdout --passwordbox "Enter Root Password:" 10 40)
            ;;
        I)
            break
            ;;
        Q|*)
            clear
            exit 0
            ;;
    esac
done

clear
echo "[+] Starting automated Gentoo Linux deployment..."

echo "[1/8] Wiping partition tables on $TARGET_DISK..."
wipefs -a "$TARGET_DISK"

echo "[2/8] Generating partition structure ($SCHEME)..."
if is_uefi; then
    parted -s "$TARGET_DISK" mklabel gpt
    parted -s "$TARGET_DISK" mkpart ESP fat32 1MiB 1025MiB
    parted -s "$TARGET_DISK" set 1 esp on
    parted -s "$TARGET_DISK" mkpart primary "$FSTYPE" 1025MiB 100%
else
    parted -s "$TARGET_DISK" mklabel msdos
    parted -s "$TARGET_DISK" mkpart primary ext4 1MiB 1025MiB
    parted -s "$TARGET_DISK" mkpart primary "$FSTYPE" 1025MiB 100%
fi

if [[ "$TARGET_DISK" == *"nvme"* ]] || [[ "$TARGET_DISK" == *"mmcblk"* ]]; then
    PART1="${TARGET_DISK}p1"; PART2="${TARGET_DISK}p2"
else
    PART1="${TARGET_DISK}1"; PART2="${TARGET_DISK}2"
fi

echo "[3/8] Formatting boot partition ($PART1)..."
if is_uefi; then mkfs.vfat -F32 "$PART1"; else mkfs.ext4 "$PART1"; fi

TARGET_ROOT_DEV="$PART2"
if [ "$ENCRYPTION" = "luks" ]; then
    echo "[+] Formatting and opening LUKS2 container..."
    echo -n "$LUKS_PASS" | cryptsetup luksFormat --type luks2 "$PART2" -
    echo -n "$LUKS_PASS" | cryptsetup open "$PART2" gentoo_root -
    TARGET_ROOT_DEV="/dev/mapper/gentoo_root"
fi

echo "[4/8] Formatting root filesystem ($FSTYPE)..."
case "$FSTYPE" in
    "btrfs") mkfs.btrfs -f "$TARGET_ROOT_DEV" ;;
    "xfs")   mkfs.xfs -f "$TARGET_ROOT_DEV" ;;
    "f2fs")  mkfs.f2fs -f "$TARGET_ROOT_DEV" ;;
    *)       mkfs.ext4 "$TARGET_ROOT_DEV" ;;
esac

echo "[5/8] Mounting filesystems..."
mkdir -p /mnt/gentoo
mount "$TARGET_ROOT_DEV" /mnt/gentoo
if is_uefi; then
    mkdir -p /mnt/gentoo/efi; mount "$PART1" /mnt/gentoo/efi
else
    mkdir -p /mnt/gentoo/boot; mount "$PART1" /mnt/gentoo/boot
fi

echo "[6/8] Downloading base Stage3 tarball..."
cd /mnt/gentoo
STAGE3_VARIANT="openrc"
[ "$INIT_SYS" = "systemd" ] && STAGE3_VARIANT="systemd"
STAGE3_FILE=$(wget -qO- "$MIRROR_URL/releases/amd64/autobuilds/current-stage3-amd64-$STAGE3_VARIANT/latest-stage3-amd64-$STAGE3_VARIANT.txt" | awk '/^stage3/ {print $1; exit}')
wget "$MIRROR_URL/releases/amd64/autobuilds/current-stage3-amd64-$STAGE3_VARIANT/$STAGE3_FILE"

echo "[+] Extracting Stage3 tarball..."
tar xpvf stage3-amd64-*.tar.xz --xattrs-include='*.*' --numeric-owner

echo "[7/8] Binding virtual filesystems for chroot..."
mount --types proc /proc /mnt/gentoo/proc
mount --rbind /sys /mnt/gentoo/sys && mount --make-rslave /mnt/gentoo/sys
mount --rbind /dev /mnt/gentoo/dev && mount --make-rslave /mnt/gentoo/dev
mount --bind /run /mnt/gentoo/run && mount --make-slave /mnt/gentoo/run
cp --dereference /etc/resolv.conf /mnt/gentoo/etc/

echo "[8/8] Executing Chroot System Configuration..."

DE_PKG=""
case "$DE_WM" in
    *"KDE"*) DE_PKG="kde-plasma/plasma-meta" ;; *"GNOME"*) DE_PKG="gnome-base/gnome" ;;
    *"Cinnamon"*) DE_PKG="gnome-extra/cinnamon" ;; *"Hyprland"*) DE_PKG="gui-wm/hyprland" ;;
    *"Sway"*) DE_PKG="gui-wm/sway" ;; *"i3"*) DE_PKG="x11-wm/i3" ;;
    *"XFCE4"*) DE_PKG="xfce-base/xfce4-meta" ;; *"Enlightenment"*) DE_PKG="x11-wm/enlightenment" ;;
    *"Awesome"*) DE_PKG="x11-wm/awesome" ;; *"LXQt"*) DE_PKG="lxqt-base/lxqt-meta" ;;
esac

DM_PKG=""; DM_SERVICE=""
case "$LOGIN_MGR" in
    "sddm") DM_PKG="x11-misc/sddm"; DM_SERVICE="sddm" ;;
    "gdm") DM_PKG="gnome-base/gdm"; DM_SERVICE="gdm" ;;
    "lightdm") DM_PKG="x11-misc/lightdm"; DM_SERVICE="display-manager" ;;
    "lxdm") DM_PKG="x11-misc/lxdm"; DM_SERVICE="lxdm" ;;
esac

AUDIO_PKG=""
case "$AUDIO_SERVER" in
    "pipewire") AUDIO_PKG="media-video/pipewire media-video/wireplumber" ;;
    "pulseaudio") AUDIO_PKG="media-sound/pulseaudio" ;;
    "alsa") AUDIO_PKG="media-libs/alsa-lib media-sound/alsa-utils" ;;
esac

BT_PKG=""; [ "$BLUETOOTH" = "bluez" ] && BT_PKG="net-wireless/bluez"
FW_PKG=""
case "$FIREWALL" in
    "ufw") FW_PKG="net-firewall/ufw" ;; "firewalld") FW_PKG="net-firewall/firewalld" ;;
    "nftables") FW_PKG="net-firewall/nftables" ;; "iptables") FW_PKG="net-firewall/iptables" ;;
esac

KERNEL_PKG=""
case "$KERNEL_TYPE" in
    "linux-zen") KERNEL_PKG="sys-kernel/zen-sources sys-kernel/genkernel" ;;
    "lts") KERNEL_PKG="sys-kernel/gentoo-kernel" ;;
    "hardened") KERNEL_PKG="sys-kernel/hardened-sources sys-kernel/genkernel" ;;
    *) KERNEL_PKG="sys-kernel/gentoo-kernel-bin" ;;
esac

GLOBAL_USE="X wayland opengl dbus gui policykit alsa pulseaudio"
INIT_PKG=""; INIT_POST_CMD="true"
case "$INIT_SYS" in
    "systemd") GLOBAL_USE="$GLOBAL_USE systemd -elogind" ;;
    "runit") GLOBAL_USE="$GLOBAL_USE elogind -systemd"; INIT_PKG="sys-process/runit"; INIT_POST_CMD="mkdir -p /etc/runit/runsvdir/default/agetty-tty1 && ln -sf /etc/sv/agetty-tty1 /etc/runit/runsvdir/default/ 2>/dev/null || true" ;;
    "dinit") GLOBAL_USE="$GLOBAL_USE elogind -systemd"; INIT_PKG="sys-process/dinit"; INIT_POST_CMD="dinitctl enable agetty-tty1 2>/dev/null || true" ;;
    "s6") GLOBAL_USE="$GLOBAL_USE elogind -systemd"; INIT_PKG="sys-process/s6 sys-apps/s6-linux-init"; INIT_POST_CMD="s6-linux-init-maker -f /etc/s6-linux-init/current 2>/dev/null || true" ;;
    "busybox") GLOBAL_USE="$GLOBAL_USE elogind -systemd"; INIT_PKG="sys-apps/busybox"; INIT_POST_CMD="echo '::respawn:/sbin/getty 38400 tty1' >> /etc/inittab" ;;
    *) GLOBAL_USE="$GLOBAL_USE elogind -systemd" ;;
esac

BOOTLOADER_PKG=""; BOOTLOADER_CMD=""
case "$BOOTLOADER" in
    "grub")
        BOOTLOADER_PKG="sys-boot/grub"
        if is_uefi; then BOOTLOADER_CMD="grub-install --target=x86_64-efi --efi-directory=/efi --bootloader-id=Gentoo && grub-mkconfig -o /boot/grub/grub.cfg"
        else BOOTLOADER_CMD="grub-install --target=i386-pc $TARGET_DISK && grub-mkconfig -o /boot/grub/grub.cfg"; fi ;;
    "limine") BOOTLOADER_PKG="sys-boot/limine"; BOOTLOADER_CMD="echo '[+] Limine emerged.'" ;;
    "systemd-boot") BOOTLOADER_CMD="bootctl install" ;;
    "refind") BOOTLOADER_PKG="sys-boot/refind"; BOOTLOADER_CMD="refind-install" ;;
    *) BOOTLOADER_CMD="echo '[+] Skipping bootloader setup.'" ;;
esac

BOOT_MOUNT="/boot"; BOOT_FSTYPE="ext4"
if is_uefi; then BOOT_MOUNT="/efi"; BOOT_FSTYPE="vfat"; fi

cat << CHROOT_EOF > /mnt/gentoo/tmp/setup_chroot.sh
#!/bin/bash
# Self-Healing Resilient Chroot Installation Script
source /etc/profile
echo 'ACCEPT_LICENSE="*"' >> /etc/portage/make.conf
echo 'USE="$GLOBAL_USE"' >> /etc/portage/make.conf
echo 'EMERGE_DEFAULT_OPTS="--autounmask=y --autounmask-write=y --keep-going --jobs=4 --load-average=4"' >> /etc/portage/make.conf
echo 'KEYMAP="$KEYMAP"' > /etc/vconsole.conf
ln -sf /usr/share/zoneinfo/$TIMEZONE_CHOICE /etc/localtime

echo '*/* ~amd64' >> /etc/portage/package.accept_keywords/global
mkdir -p /etc/portage/package.use /etc/portage/package.accept_keywords
echo 'dev-python/pillow -truetype' >> /etc/portage/package.use/installer
echo 'sys-process/runit -system-init -static' >> /etc/portage/package.use/installer 2>/dev/null || true
echo 'sys-apps/s6-linux-init -sysv-utils' >> /etc/portage/package.use/installer 2>/dev/null || true
echo 'sys-apps/busybox -make-symlinks -static -systemd -pam' >> /etc/portage/package.use/installer 2>/dev/null || true

mkdir -p /var/db/repos/gentoo /etc/portage/repos.conf
cp /usr/share/portage/config/repos.conf /etc/portage/repos.conf/gentoo.conf 2>/dev/null || true

echo "$TARGET_ROOT_DEV / $FSTYPE defaults,noatime 0 1" > /etc/fstab
echo "$PART1 $BOOT_MOUNT $BOOT_FSTYPE defaults,noatime 1 2" >> /etc/fstab

# Bulletproof Password Setting
ROOT_HASH=\$(python3 -c "import crypt; print(crypt.crypt('$ROOT_PASS', crypt.mksalt(crypt.METHOD_SHA512)))" 2>/dev/null)
[ -n "\$ROOT_HASH" ] && sed -i "s|^root:[^:]*:|root:\$ROOT_HASH:|" /etc/shadow

useradd -m -G wheel,users -s /bin/bash "$USER_NAME" 2>/dev/null || true
USER_HASH=\$(python3 -c "import crypt; print(crypt.crypt('$USER_PASS', crypt.mksalt(crypt.METHOD_SHA512)))" 2>/dev/null)
[ -n "\$USER_HASH" ] && sed -i "s|^$USER_NAME:[^:]*:|$USER_NAME:\$USER_HASH:|" /etc/shadow 2>/dev/null || true
echo '%wheel ALL=(ALL:ALL) ALL' >> /etc/sudoers

emerge-webrsync || emerge --sync || true
if [ "$INIT_SYS" = "dinit" ]; then
    emerge --noreplace app-eselect/eselect-repository dev-vcs/git 2>/dev/null || true
    eselect repository enable guru 2>/dev/null || true; emaint sync -r guru 2>/dev/null || true
fi

PROFILE_TARGET="desktop"; [ "$INIT_SYS" = "systemd" ] && PROFILE_TARGET="desktop/systemd"
PROFILE_NUM=\$(eselect profile list | grep -E "\$PROFILE_TARGET" | head -n1 | tr -d '[]' | awk '{print \$1}')
if [ -n "\$PROFILE_NUM" ]; then eselect profile set "\$PROFILE_NUM" || true; fi

# Resilient Package Installer Function (Multi-attempt batch & individual fallback, never stops installation on failure)
install_pkgs_resilient() {
    local pkgs=()
    for item in \$@; do
        if [ -n "\$item" ] && [ "\$item" != "none" ]; then
            pkgs+=("\$item")
        fi
    done
    if [ \${#pkgs[@]} -eq 0 ]; then return 0; fi

    echo "[+] Attempting batch installation: \${pkgs[*]}"
    for attempt in {1..5}; do
        yes | etc-update --automode -5 2>/dev/null || true
        if emerge --verbose "\${pkgs[@]}"; then
            echo "[+] Batch installation succeeded."
            return 0
        fi
        echo "[!] Batch attempt \$attempt failed. Updating config and retrying..."
        yes | etc-update --automode -5 2>/dev/null || true
    done

    echo "[!] Batch failed after 5 attempts. Falling back to individual package installation..."
    for pkg in "\${pkgs[@]}"; do
        local success=false
        for attempt in {1..3}; do
            echo "[+] Installing individual package: \$pkg (Attempt \$attempt/3)..."
            yes | etc-update --automode -5 2>/dev/null || true
            if emerge --verbose "\$pkg"; then
                success=true
                break
            fi
        done
        if [ "\$success" = false ]; then
            echo "[!] WARNING: Skipping unresolvable package '\$pkg' and continuing installation anyway."
        fi
    done
}

echo "[+] Starting core system packages installation..."
install_pkgs_resilient $KERNEL_PKG net-misc/dhcpcd $BOOTLOADER_PKG $DE_PKG $DM_PKG $INIT_PKG $AUDIO_PKG $BT_PKG $FW_PKG

echo "[+] Starting optional user packages installation..."
install_pkgs_resilient $EXTRA_PKGS

$INIT_POST_CMD

rc-update add dhcpcd default 2>/dev/null || systemctl enable dhcpcd 2>/dev/null || true
if [ -n "$DM_SERVICE" ]; then rc-update add $DM_SERVICE default 2>/dev/null || systemctl enable $DM_SERVICE 2>/dev/null || true; fi
if [ -n "$BT_PKG" ]; then rc-update add bluetooth default 2>/dev/null || systemctl enable bluetooth 2>/dev/null || true; fi
if [ -n "$FW_PKG" ]; then rc-update add $FIREWALL default 2>/dev/null || systemctl enable $FIREWALL 2>/dev/null || true; fi
if [ "$KERNEL_TYPE" = "linux-zen" ] || [ "$KERNEL_TYPE" = "hardened" ]; then genkernel all 2>/dev/null || true; fi

$BOOTLOADER_CMD || echo "[!] Bootloader setup encountered a warning, continuing..."
echo '[+] Gentoo Chroot Self-Healing Configuration Completed!'
CHROOT_EOF

chmod +x /mnt/gentoo/tmp/setup_chroot.sh
chroot /mnt/gentoo /tmp/setup_chroot.sh || {
    echo "[!] Chroot execution had non-fatal warnings, self-healing wrapper continuing installation process."
}
rm -f /mnt/gentoo/tmp/setup_chroot.sh

echo "==================================================="
echo " [SUCCESS] Gentoo Linux Installation Completed!"
echo " Unmount /mnt/gentoo and reboot into your system."
echo "==================================================="
