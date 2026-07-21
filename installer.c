#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

void run_dialog_cmd(const char *cmd, char *output, size_t len) {
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        output[0] = '\0';
        return;
    }
    if (fgets(output, len, fp) != NULL) {
        output[strcspn(output, "\n")] = 0;
    } else {
        output[0] = '\0';
    }
    pclose(fp);
}

void show_error(const char *msg) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "dialog --clear --title \"Installation Error\" --msgbox \"%s\" 12 65", msg);
    system(cmd);
}

void run_command_with_error_check(const char *cmd, const char *err_msg) {
    char full_cmd[4096];
    snprintf(full_cmd, sizeof(full_cmd), "%s > /tmp/installer_err.log 2>&1", cmd);
    int ret = system(full_cmd);
    if (ret != 0) {
        FILE *f = popen("tail -n 5 /tmp/installer_err.log | grep -v '^$' | tail -n 1", "r");
        char err_details[512] = "Check /tmp/installer_err.log for details.";
        if (f) {
            if (fgets(err_details, sizeof(err_details), f) == NULL) {
                strcpy(err_details, "Unknown script error.");
            }
            pclose(f);
        }
        char final_msg[1024];
        snprintf(final_msg, sizeof(final_msg), "%s\nDetails: %s", err_msg, err_details);
        show_error(final_msg);
        system("clear");
        exit(1);
    }
}

int is_uefi() {
    return (access("/sys/firmware/efi", F_OK) == 0);
}

void handle_package_search(char *extra_pkgs, size_t extra_pkgs_len) {
    while (1) {
        char choice[16] = {0};
        char cmd[2048];
        snprintf(cmd, sizeof(cmd),
                 "dialog --clear --title \"Additional Packages & Software Bundles\" "
                 "--menu \"Current Packages: [%s]\\nChoose software selection method:\" 16 70 4 "
                 "\"1\" \"Manual Input (Space Separated)\" "
                 "\"2\" \"Live Portage Database Search\" "
                 "\"3\" \"Install Popular Software Bundles\" "
                 "\"BACK\" \"Return to Main Menu\" 3>&1 1>&2 2>&3", extra_pkgs);
        run_dialog_cmd(cmd, choice, sizeof(choice));

        if (strcmp(choice, "BACK") == 0 || strlen(choice) == 0) break;

        if (strcmp(choice, "1") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Edit Package List\" "
                     "--inputbox \"Enter package names:\" 10 65 \"%s\" 3>&1 1>&2 2>&3", extra_pkgs);
            run_dialog_cmd(cmd, extra_pkgs, extra_pkgs_len);
        } else if (strcmp(choice, "2") == 0) {
            char search_term[128] = {0};
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Portage Package Search\" "
                     "--inputbox \"Enter keyword (e.g. htop, btop, vim, firefox):\" 10 65 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, search_term, sizeof(search_term));

            if (strlen(search_term) > 0) {
                if (access("/var/db/repos/gentoo", F_OK) == 0) {
                    snprintf(cmd, sizeof(cmd),
                             "emerge --search %s | grep -E '^\\* ' | cut -d' ' -f2 > /tmp/pkg_search.txt", search_term);
                    system(cmd);
                } else {
                    if (access("/tmp/gentoo_pkgs.txt", F_OK) != 0) {
                        system("dialog --clear --title \"Syncing Index\" --infobox \"Downloading Portage package list...\" 5 60");
                        system("wget -qO- https://distfiles.gentoo.org/snapshots/gentoo-latest.tar.xz | tar -tJ 2>/dev/null | grep -E '^[a-zA-Z0-9-]+/[a-zA-Z0-9-]+/$' | tr -d '/' > /tmp/gentoo_pkgs.txt 2>/dev/null || true");
                    }
                    snprintf(cmd, sizeof(cmd),
                             "grep -i '%s' /tmp/gentoo_pkgs.txt | head -n 30 > /tmp/pkg_search.txt", search_term);
                    system(cmd);
                }

                FILE *f = fopen("/tmp/pkg_search.txt", "r");
                if (!f) {
                    show_error("Failed to read package database.");
                    continue;
                }

                char menu_opts[4096] = {0};
                char line[256];
                int count = 0;
                while (fgets(line, sizeof(line), f) && count < 20) {
                    line[strcspn(line, "\n")] = 0;
                    if (strlen(line) < 3) continue;
                    char item[300];
                    snprintf(item, sizeof(item), "\"%s\" \"Gentoo Package\" ", line);
                    strcat(menu_opts, item);
                    count++;
                }
                fclose(f);

                if (count == 0) {
                    show_error("No matching packages found.");
                    continue;
                }

                char selected_pkg[256] = {0};
                snprintf(cmd, sizeof(cmd),
                         "dialog --clear --title \"Search Results (%d found)\" "
                         "--menu \"Select package to append:\" 20 65 10 %s 3>&1 1>&2 2>&3", count, menu_opts);
                run_dialog_cmd(cmd, selected_pkg, sizeof(selected_pkg));

                if (strlen(selected_pkg) > 0) {
                    if (strlen(extra_pkgs) > 0) strcat(extra_pkgs, " ");
                    strcat(extra_pkgs, selected_pkg);
                }
            }
        } else if (strcmp(choice, "3") == 0) {
            char bundle[32] = {0};
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Popular Software Bundles\" "
                     "--menu \"Select preset software bundle to add:\" 16 65 5 "
                     "\"DEV\" \"Development Tools (git, gcc, gdb, vim, cmake)\" "
                     "\"SYS\" \"System Monitoring (htop, btop, fastfetch, neofetch)\" "
                     "\"NET\" \"Network Utilities (curl, wget, nmap, net-tools)\" "
                     "\"MEDIA\" \"Multimedia Apps (mpv, ffmpeg, yt-dlp)\" "
                     "\"SEC\" \"Security Tools (gnupg, keepassxc, fail2ban)\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, bundle, sizeof(bundle));

            if (strcmp(bundle, "DEV") == 0) strcat(extra_pkgs, " dev-vcs/git app-editors/vim dev-build/cmake");
            else if (strcmp(bundle, "SYS") == 0) strcat(extra_pkgs, " sys-process/htop sys-process/btop app-misc/fastfetch");
            else if (strcmp(bundle, "NET") == 0) strcat(extra_pkgs, " net-misc/curl net-analyzer/nmap sys-apps/net-tools");
            else if (strcmp(bundle, "MEDIA") == 0) strcat(extra_pkgs, " media-video/mpv media-video/ffmpeg");
            else if (strcmp(bundle, "SEC") == 0) strcat(extra_pkgs, " app-crypt/gnupg net-analyzer/fail2ban");
        }
    }
}

void handle_disk_menu(char *disk, char *scheme, char *part_strat, char *fstype, char *encryption) {
    while (1) {
        char choice[16] = {0};
        char cmd[2048];
        snprintf(cmd, sizeof(cmd),
                 "dialog --clear --title \"Storage & Partitioning Architect\" "
                 "--menu \"Current Storage: [%s] | Layout: [%s] | FS: [%s] | LUKS: [%s]\\nSelect option:\" 18 75 6 "
                 "\"1\" \"Select Target Storage Drive\" "
                 "\"2\" \"Partitioning Strategy & Tool Selection\" "
                 "\"3\" \"Filesystem Selection (Btrfs/XFS/F2FS/Ext4)\" "
                 "\"4\" \"Toggle Disk Encryption (LUKS / dm-crypt)\" "
                 "\"5\" \"Partition Table Scheme (GPT / MBR)\" "
                 "\"BACK\" \"Return to Main Menu\" 3>&1 1>&2 2>&3",
                 strlen(disk) ? disk : "None", part_strat, fstype, strcmp(encryption, "luks") == 0 ? "ENABLED" : "Disabled");
        run_dialog_cmd(cmd, choice, sizeof(choice));

        if (strcmp(choice, "BACK") == 0 || strlen(choice) == 0) break;

        if (strcmp(choice, "1") == 0) {
            FILE *fp = popen("lsblk -d -n -o NAME,SIZE,TYPE | grep -E 'disk' | grep -v -E '^(fd|sr|loop)' | awk '{print $1, $2}'", "r");
            char disk_options[2048] = "";
            char dev_name[128], dev_size[128];
            while (fp && fscanf(fp, "%127s %127s", dev_name, dev_size) == 2) {
                char item[512];
                snprintf(item, sizeof(item), "\"/dev/%s\" \"%s\" ", dev_name, dev_size);
                strcat(disk_options, item);
            }
            if (fp) pclose(fp);

            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Disk Selection\" "
                     "--menu \"Select target storage drive:\" 15 60 6 %s 3>&1 1>&2 2>&3", disk_options);
            run_dialog_cmd(cmd, disk, 64);
        } else if (strcmp(choice, "2") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Partition Strategy\" "
                     "--menu \"Choose how to partition the selected disk:\" 16 70 6 "
                     "\"AUTO\" \"Automatic Wipe & Repartition (Recommended)\" "
                     "\"CFDISK\" \"Interactive cfdisk (Curses Partition Editor)\" "
                     "\"CGDISK\" \"Interactive cgdisk (GPT Curses Editor)\" "
                     "\"FDISK\" \"Interactive fdisk (Standard Editor)\" "
                     "\"PARTED\" \"Interactive GNU Parted CLI\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, part_strat, 16);
        } else if (strcmp(choice, "3") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Filesystem Selection\" "
                     "--menu \"Choose root filesystem layout:\" 15 55 4 "
                     "\"ext4\" \"Ext4 (Standard & Stable)\" "
                     "\"btrfs\" \"Btrfs (Snapshots & Compression)\" "
                     "\"xfs\" \"XFS (High Performance Enterprise)\" "
                     "\"f2fs\" \"F2FS (Flash / NVMe Optimized)\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, fstype, 64);
        } else if (strcmp(choice, "4") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"LUKS Disk Encryption\" "
                     "--menu \"Enable LUKS full disk encryption?\" 12 50 2 "
                     "\"none\" \"Disabled (Unencrypted Standard)\" "
                     "\"luks\" \"Enabled (LUKS2 Encrypted Root)\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, encryption, 16);
        } else if (strcmp(choice, "5") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Partition Scheme\" "
                     "--menu \"Select partition table type:\" 12 50 2 "
                     "\"GPT\" \"GUID Partition Table (UEFI Standard)\" "
                     "\"MBR\" \"Master Boot Record (Legacy BIOS)\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, scheme, 16);
        }
    }
}

int main() {
    if (geteuid() != 0) {
        fprintf(stderr, "[-] Error: Run this installer as root!\n");
        return 1;
    }

    if (system("which dialog > /dev/null 2>&1") != 0) {
        fprintf(stderr, "[-] Error: 'dialog' package is required.\n");
        return 1;
    }

    char lang[64] = "en";
    char mirror_choice[16] = "1";
    char mirror[512] = "https://distfiles.gentoo.org";
    char disk[64] = "";
    char scheme[16] = "GPT";
    char part_strat[16] = "AUTO";
    char fstype[64] = "ext4";
    char encryption[16] = "none";
    char de[64] = "none";
    char display_manager[64] = "none";
    char extra_pkgs[512] = "sudo fastfetch";
    char init_sys[64] = "openrc";
    char bootloader[64] = "grub";
    char kernel_type[64] = "gentoo-kernel-bin";
    char audio_server[64] = "pipewire";
    char bluetooth[16] = "bluez";
    char firewall[32] = "ufw";
    char rootpass[128] = "";
    char username[64] = "";
    char userpass[128] = "";
    char lukspass[128] = "";
    char cmd[4096];

    int uefi_detected = is_uefi();
    if (!uefi_detected) strcpy(scheme, "MBR");

    FILE *fp_disk = popen("lsblk -d -n -o NAME,TYPE | grep -E 'disk' | grep -v -E '^(fd|sr|loop)' | head -n1 | awk '{print $1}'", "r");
    if (fp_disk) {
        char dev[64];
        if (fgets(dev, sizeof(dev), fp_disk)) {
            dev[strcspn(dev, "\n")] = 0;
            snprintf(disk, sizeof(disk), "/dev/%s", dev);
        }
        pclose(fp_disk);
    }

    while (1) {
        char menu_choice[16] = {0};
        snprintf(cmd, sizeof(cmd),
                 "dialog --clear --colors --title \"Gentoo System Architect\" "
                 "--menu \"=== ARCHINSTALL STYLE GENTOO DEPLOYMENT ===\" 24 80 16 "
                 "\"1\"  \"\\Z1[System]\\Zn      Language:            [%s]\" "
                 "\"2\"  \"\\Z1[System]\\Zn      Gentoo Mirror:       [%s]\" "
                 "\"3\"  \"\\Z2[Storage]\\Zn     Disk & Partitioning: [%s | %s | %s]\" "
                 "\"4\"  \"\\Z3[Core]\\Zn        Init System:         [%s]\" "
                 "\"5\"  \"\\Z3[Core]\\Zn        Kernel Variant:      [%s]\" "
                 "\"6\"  \"\\Z3[Core]\\Zn        Bootloader:          [%s]\" "
                 "\"7\"  \"\\Z4[Desktop]\\Zn     Desktop / WM:        [%s]\" "
                 "\"8\"  \"\\Z4[Desktop]\\Zn     Display Manager:     [%s]\" "
                 "\"9\"  \"\\Z5[Hardware]\\Zn    Audio Server:        [%s]\" "
                 "\"10\" \"\\Z5[Hardware]\\Zn    Bluetooth:           [%s]\" "
                 "\"11\" \"\\Z6[Security]\\Zn    Firewall:            [%s]\" "
                 "\"12\" \"\\Z6[Security]\\Zn    Root Account:        [%s]\" "
                 "\"13\" \"\\Z6[Security]\\Zn    User Account:        [%s]\" "
                 "\"14\" \"\\Z7[Software]\\Zn    Additional Packages: [%s]\" "
                 "\"15\" \"\\Z0[Diagnostics]\\Zn View Build Logs\" "
                 "\"INSTALL\" \"\\Z4---> BEGIN GENTOO INSTALLATION <----\\Zn\" "
                 "\"QUIT\" \"Exit System Architect\" 3>&1 1>&2 2>&3",
                 lang, mirror_choice, strlen(disk) ? disk : "None", fstype, strcmp(encryption, "luks") == 0 ? "LUKS" : "RAW",
                 init_sys, kernel_type, bootloader, de, display_manager, audio_server, bluetooth, firewall,
                 strlen(rootpass) ? "*****" : "Not Set", strlen(username) ? username : "Not Set", extra_pkgs);

        run_dialog_cmd(cmd, menu_choice, sizeof(menu_choice));

        if (strcmp(menu_choice, "QUIT") == 0 || strlen(menu_choice) == 0) {
            system("clear");
            printf("Installation exited by user.\n");
            return 0;
        }

        if (strcmp(menu_choice, "1") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Language Selection\" --menu \"Choose language:\" 15 50 4 "
                     "\"en\" \"English\" \"ar\" \"Arabic\" \"es\" \"Spanish\" \"fr\" \"French\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, lang, sizeof(lang));
        } else if (strcmp(menu_choice, "2") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Gentoo Portage Mirror\" --menu \"Choose mirror:\" 18 65 6 "
                     "\"1\" \"Official Global Mirror\" \"2\" \"Init7 Europe\" \"3\" \"OSUSL North America\" "
                     "\"4\" \"LeaseWeb Europe\" \"5\" \"RIT US Mirror\" \"CUSTOM\" \"Custom URL\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, mirror_choice, sizeof(mirror_choice));
            if (strcmp(mirror_choice, "2") == 0) strcpy(mirror, "https://mirror.init7.net/gentoo");
                else if (strcmp(mirror_choice, "3") == 0) strcpy(mirror, "https://gentoo.osuosl.org");
                    else if (strcmp(mirror_choice, "4") == 0) strcpy(mirror, "https://mirror.nl.leaseweb.net/gentoo");
                        else if (strcmp(mirror_choice, "5") == 0) strcpy(mirror, "https://mirrors.rit.edu/gentoo");
                            else if (strcmp(mirror_choice, "CUSTOM") == 0) {
                                snprintf(cmd, sizeof(cmd), "dialog --clear --title \"Custom Mirror URL\" --inputbox \"Enter HTTP/HTTPS URL:\" 10 65 \"%s\" 3>&1 1>&2 2>&3", mirror);
                                run_dialog_cmd(cmd, mirror, sizeof(mirror));
                            } else strcpy(mirror, "https://distfiles.gentoo.org");
        } else if (strcmp(menu_choice, "3") == 0) {
            handle_disk_menu(disk, scheme, part_strat, fstype, encryption);
            if (strcmp(encryption, "luks") == 0 && strlen(lukspass) == 0) {
                FILE *fp = popen("dialog --clear --title \"LUKS Disk Encryption Password\" --passwordbox \"Enter passphrase for LUKS encrypted drive:\" 10 60 3>&1 1>&2 2>&3", "r");
                if (fp && fgets(lukspass, sizeof(lukspass), fp)) lukspass[strcspn(lukspass, "\n")] = 0;
                if (fp) pclose(fp);
            }
        } else if (strcmp(menu_choice, "4") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Init System Architect\" --menu \"Choose init framework:\" 18 70 7 "
                     "\"openrc\" \"OpenRC (Gentoo Default Standard)\" "
                     "\"systemd\" \"systemd (Modern Linux Init & Service Mgr)\" "
                     "\"runit\" \"runit (Lightweight UNIX Init Scheme)\" "
                     "\"dinit\" \"Dinit (Service Dependency Init)\" "
                     "\"s6\" \"s6 (Skarnet Supervision Suite)\" "
                     "\"busybox\" \"BusyBox (Ultra-Minimalist Init)\" "
                     "\"initrd\" \"Custom Initrd Minimal Setup\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, init_sys, sizeof(init_sys));
        } else if (strcmp(menu_choice, "5") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Linux Kernel Customization\" --menu \"Select kernel distribution:\" 16 68 5 "
                     "\"gentoo-kernel-bin\" \"Standard Gentoo Binary Kernel (Fastest)\" "
                     "\"linux-zen\" \"Linux Zen Kernel (Desktop & Gaming Optimized)\" "
                     "\"lts\" \"Long Term Support (LTS Stable Kernel)\" "
                     "\"hardened\" \"Gentoo Hardened (Security & SELinux Patches)\" "
                     "\"vanilla\" \"Vanilla Linux Kernel Sources (Custom Build)\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, kernel_type, sizeof(kernel_type));
        } else if (strcmp(menu_choice, "6") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Bootloader Architect\" --menu \"Choose system bootloader:\" 16 65 5 "
                     "\"grub\" \"GRUB 2 (Standard Universal)\" "
                     "\"limine\" \"Limine Bootloader (Modern & Fast)\" "
                     "\"systemd-boot\" \"systemd-boot (UEFI Only Minimal)\" "
                     "\"refind\" \"rEFInd (Graphical UEFI Boot Manager)\" "
                     "\"none\" \"Skip Bootloader Setup\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, bootloader, sizeof(bootloader));
        } else if (strcmp(menu_choice, "7") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Desktop Environment / WM\" --menu \"Select interface:\" 22 70 14 "
                     "\"KDE\" \"KDE Plasma 6\" \"GNOME\" \"GNOME Desktop\" \"CINNAMON\" \"Cinnamon Desktop\" "
                     "\"LXQT\" \"LXQt Desktop (Qt Lightweight)\" \"LXDE\" \"LXDE Desktop (GTK Lightweight)\" "
                     "\"ENLIGHTENMENT\" \"Enlightenment WM / DE\" \"FVWM_CRYSTAL\" \"FVWM-Crystal Theme WM\" "
                     "\"AWESOME\" \"Awesome Window Manager\" \"hyprland\" \"Hyprland Wayland Tiling\" "
                     "\"sway\" \"Sway Wayland Tiling\" \"i3\" \"i3 X11 Tiling WM\" \"xfce4\" \"XFCE4 Desktop\" "
                     "\"mate\" \"MATE Desktop\" \"none\" \"Headless / Console Only\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, de, sizeof(de));
        } else if (strcmp(menu_choice, "8") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Display Manager\" --menu \"Select login greeter:\" 18 68 7 "
                     "\"sddm\" \"SDDM (KDE/Qt Default)\" \"gdm\" \"GDM (GNOME Display Manager)\" "
                     "\"lightdm-gtk\" \"LightDM (GTK Greeter)\" \"lightdm-slick\" \"LightDM (Slick Greeter)\" "
                     "\"lxdm\" \"LXDM (Lightweight DM)\" \"plasma-login\" \"Plasma SDDM Integration\" "
                     "\"none\" \"Console / startx Only\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, display_manager, sizeof(display_manager));
        } else if (strcmp(menu_choice, "9") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Audio Server Architect\" --menu \"Select sound server:\" 15 60 4 "
                     "\"pipewire\" \"PipeWire (Modern ALSA/Pulse/Jack Replacement)\" "
                     "\"pulseaudio\" \"PulseAudio (Classic Sound Server)\" "
                     "\"alsa\" \"ALSA Only (Bare Metal Audio)\" "
                     "\"none\" \"No Audio Support\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, audio_server, sizeof(audio_server));
        } else if (strcmp(menu_choice, "10") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Bluetooth Support\" --menu \"Enable Bluetooth wireless stack?\" 12 50 2 "
                     "\"bluez\" \"Enabled (Install BlueZ Stack)\" \"none\" \"Disabled (No Bluetooth)\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, bluetooth, sizeof(bluetooth));
        } else if (strcmp(menu_choice, "11") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Firewall Security\" --menu \"Select firewall manager:\" 16 60 5 "
                     "\"ufw\" \"UFW (Uncomplicated Firewall)\" \"firewalld\" \"Firewalld (Dynamic Firewall Mgr)\" "
                     "\"nftables\" \"nftables (Modern Kernel Firewall)\" \"iptables\" \"iptables (Classic Netfilter)\" "
                     "\"none\" \"No Firewall Installed\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, firewall, sizeof(firewall));
        } else if (strcmp(menu_choice, "12") == 0) {
            FILE *fp = popen("dialog --clear --title \"Root Security\" --passwordbox \"Enter root account password:\" 10 55 3>&1 1>&2 2>&3", "r");
            if (fp && fgets(rootpass, sizeof(rootpass), fp)) rootpass[strcspn(rootpass, "\n")] = 0;
            if (fp) pclose(fp);
        } else if (strcmp(menu_choice, "13") == 0) {
            FILE *fp = popen("dialog --clear --title \"User Account\" --inputbox \"Enter username (wheel/sudo enabled):\" 10 55 3>&1 1>&2 2>&3", "r");
            if (fp && fgets(username, sizeof(username), fp)) username[strcspn(username, "\n")] = 0;
            if (fp) pclose(fp);
            if (strlen(username) > 0) {
                snprintf(cmd, sizeof(cmd), "dialog --clear --title \"User Security\" --passwordbox \"Enter password for %s:\" 10 55 3>&1 1>&2 2>&3", username);
                fp = popen(cmd, "r");
                if (fp && fgets(userpass, sizeof(userpass), fp)) userpass[strcspn(userpass, "\n")] = 0;
                if (fp) pclose(fp);
            }
        } else if (strcmp(menu_choice, "14") == 0) {
            handle_package_search(extra_pkgs, sizeof(extra_pkgs));
        } else if (strcmp(menu_choice, "15") == 0) {
            system("dialog --clear --title \"Build & Error Logs\" --textbox /tmp/installer_err.log 20 75");
        } else if (strcmp(menu_choice, "INSTALL") == 0) {
            if (strlen(disk) == 0) { show_error("Select target storage disk first."); continue; }
            if (strlen(rootpass) == 0) { show_error("Set root password first."); continue; }
            if (strlen(username) == 0) { show_error("Configure user account first."); continue; }

            char review_msg[2048];
            snprintf(review_msg, sizeof(review_msg),
                     "=== PRE-INSTALLATION ARCHITECT REVIEW ===\\n\\n"
                     "Target Storage: %s | Layout: %s (%s)\\n"
                     "Filesystem:     %s | LUKS: %s\\n"
                     "Init Framework: %s | Kernel: %s\\n"
                     "Bootloader:     %s\\n"
                     "Desktop UI:     %s | Login: %s\\n"
                     "Audio:          %s | Bluetooth: %s\\n"
                     "Firewall:       %s\\n"
                     "Extra Packages Unstable: %s\\n"
                     "User Account:   %s\\n\\n"
                     "Proceed with automated deployment?",
                     disk, part_strat, scheme, fstype, strcmp(encryption, "luks") == 0 ? "YES" : "NO",
                     init_sys, kernel_type, bootloader, de, display_manager, audio_server, bluetooth, firewall, extra_pkgs, username);

            snprintf(cmd, sizeof(cmd), "dialog --clear --title \"Confirm Installation\" --yesno \"%s\" 22 75", review_msg);
            if (system(cmd) == 0) break;
        }
    }

    system("clear");

    if (strcmp(part_strat, "CFDISK") == 0) {
        printf("[+] Launching cfdisk on %s...\n", disk);
        snprintf(cmd, sizeof(cmd), "cfdisk %s", disk);
        system(cmd);
    } else if (strcmp(part_strat, "CGDISK") == 0) {
        printf("[+] Launching cgdisk on %s...\n", disk);
        snprintf(cmd, sizeof(cmd), "cgdisk %s", disk);
        system(cmd);
    } else if (strcmp(part_strat, "FDISK") == 0) {
        printf("[+] Launching fdisk on %s...\n", disk);
        snprintf(cmd, sizeof(cmd), "fdisk %s", disk);
        system(cmd);
    } else if (strcmp(part_strat, "PARTED") == 0) {
        printf("[+] Launching parted on %s...\n", disk);
        snprintf(cmd, sizeof(cmd), "parted %s", disk);
        system(cmd);
    } else {
        printf("[1/8] Wiping partition tables...\n");
        snprintf(cmd, sizeof(cmd), "wipefs -a %s", disk);
        run_command_with_error_check(cmd, "Failed to wipe target disk.");

        printf("[2/8] Generating partition structure...\n");
        if (uefi_detected) {
            snprintf(cmd, sizeof(cmd),
                     "parted -s %s mklabel gpt && "
                     "parted -s %s mkpart ESP fat32 1MiB 1025MiB && "
                     "parted -s %s set 1 esp on && "
                     "parted -s %s mkpart primary %s 1025MiB 100%%",
                     disk, disk, disk, disk, fstype);
        } else {
            snprintf(cmd, sizeof(cmd),
                     "parted -s %s mklabel msdos && "
                     "parted -s %s mkpart primary ext4 1MiB 1025MiB && "
                     "parted -s %s mkpart primary %s 1025MiB 100%%",
                     disk, disk, disk, fstype);
        }
        run_command_with_error_check(cmd, "Failed to partition disk.");
    }

    char part1[128], part2[128], target_root_dev[128];
    if (strstr(disk, "nvme") != NULL || strstr(disk, "mmcblk") != NULL) {
        snprintf(part1, sizeof(part1), "%sp1", disk);
        snprintf(part2, sizeof(part2), "%sp2", disk);
    } else {
        snprintf(part1, sizeof(part1), "%s1", disk);
        snprintf(part2, sizeof(part2), "%s2", disk);
    }

    printf("[3/8] Formatting boot filesystem...\n");
    if (uefi_detected) snprintf(cmd, sizeof(cmd), "mkfs.vfat -F32 %s", part1);
    else snprintf(cmd, sizeof(cmd), "mkfs.ext4 %s", part1);
    run_command_with_error_check(cmd, "Failed to format boot partition.");

    if (strcmp(encryption, "luks") == 0) {
        printf("[+] Configuring LUKS2 disk encryption on %s...\n", part2);
        snprintf(cmd, sizeof(cmd), "echo -n '%s' | cryptsetup luksFormat --type luks2 %s -", lukspass, part2);
        run_command_with_error_check(cmd, "Failed to format LUKS container.");
        snprintf(cmd, sizeof(cmd), "echo -n '%s' | cryptsetup open %s gentoo_root -", lukspass, part2);
        run_command_with_error_check(cmd, "Failed to open LUKS container.");
        strcpy(target_root_dev, "/dev/mapper/gentoo_root");
    } else {
        strcpy(target_root_dev, part2);
    }

    printf("[4/8] Formatting root filesystem (%s)...\n", fstype);
    if (strcmp(fstype, "btrfs") == 0) snprintf(cmd, sizeof(cmd), "mkfs.btrfs -f %s", target_root_dev);
    else if (strcmp(fstype, "xfs") == 0) snprintf(cmd, sizeof(cmd), "mkfs.xfs -f %s", target_root_dev);
    else if (strcmp(fstype, "f2fs") == 0) snprintf(cmd, sizeof(cmd), "mkfs.f2fs -f %s", target_root_dev);
    else snprintf(cmd, sizeof(cmd), "mkfs.ext4 %s", target_root_dev);
    run_command_with_error_check(cmd, "Failed to format root partition.");

    printf("[5/8] Mounting filesystems...\n");
    run_command_with_error_check("mkdir -p /mnt/gentoo", "Failed to create /mnt/gentoo.");
    snprintf(cmd, sizeof(cmd), "mount %s /mnt/gentoo", target_root_dev);
    run_command_with_error_check(cmd, "Failed to mount root partition.");

    if (uefi_detected) {
        run_command_with_error_check("mkdir -p /mnt/gentoo/efi", "Failed to create /mnt/gentoo/efi.");
        snprintf(cmd, sizeof(cmd), "mount %s /mnt/gentoo/efi", part1);
        run_command_with_error_check(cmd, "Failed to mount EFI partition.");
    } else {
        run_command_with_error_check("mkdir -p /mnt/gentoo/boot", "Failed to create /mnt/gentoo/boot.");
        snprintf(cmd, sizeof(cmd), "mount %s /mnt/gentoo/boot", part1);
        run_command_with_error_check(cmd, "Failed to mount boot partition.");
    }

    printf("[6/8] Fetching Gentoo Stage3 tarball (%s)...\n", init_sys);
    chdir("/mnt/gentoo");
    char stage3_variant[32] = "openrc";
    if (strcmp(init_sys, "systemd") == 0) strcpy(stage3_variant, "systemd");

    snprintf(cmd, sizeof(cmd),
             "STAGE3_FILE=$(wget -qO- %s/releases/amd64/autobuilds/current-stage3-amd64-%s/latest-stage3-amd64-%s.txt | awk '/^stage3/ {print $1; exit}') && "
             "wget %s/releases/amd64/autobuilds/current-stage3-amd64-%s/$STAGE3_FILE",
             mirror, stage3_variant, stage3_variant, mirror, stage3_variant);
    run_command_with_error_check(cmd, "Failed to download dynamic Stage3 tarball.");

    printf("[+] Unpacking Stage3 tarball...\n");
    run_command_with_error_check("tar xpvf stage3-amd64-*.tar.xz --xattrs-include='*.*' --numeric-owner", "Failed to extract Stage3.");

    printf("[7/8] Mounting virtual filesystems for chroot...\n");
    run_command_with_error_check("mount --types proc /proc /mnt/gentoo/proc", "Failed to mount /proc");
    run_command_with_error_check("mount --rbind /sys /mnt/gentoo/sys && mount --make-rslave /mnt/gentoo/sys", "Failed to mount /sys");
    run_command_with_error_check("mount --rbind /dev /mnt/gentoo/dev && mount --make-rslave /mnt/gentoo/dev", "Failed to mount /dev");
    run_command_with_error_check("mount --bind /run /mnt/gentoo/run && mount --make-slave /mnt/gentoo/run", "Failed to mount /run");
    run_command_with_error_check("cp --dereference /etc/resolv.conf /mnt/gentoo/etc/", "Failed to copy resolv.conf");

    printf("[8/8] Running Chroot System Configuration...\n");

    char de_pkg[256] = "";
    if (strcmp(de, "KDE") == 0) strcpy(de_pkg, "kde-plasma/plasma-meta");
    else if (strcmp(de, "GNOME") == 0) strcpy(de_pkg, "gnome-base/gnome");
    else if (strcmp(de, "CINNAMON") == 0) strcpy(de_pkg, "gnome-extra/cinnamon");
    else if (strcmp(de, "LXQT") == 0) strcpy(de_pkg, "lxqt-base/lxqt-meta");
    else if (strcmp(de, "LXDE") == 0) strcpy(de_pkg, "lxde-base/lxde-meta");
    else if (strcmp(de, "ENLIGHTENMENT") == 0) strcpy(de_pkg, "x11-wm/enlightenment");
    else if (strcmp(de, "FVWM_CRYSTAL") == 0) strcpy(de_pkg, "x11-wm/fvwm-crystal");
    else if (strcmp(de, "AWESOME") == 0) strcpy(de_pkg, "x11-wm/awesome");
    else if (strcmp(de, "hyprland") == 0) strcpy(de_pkg, "gui-wm/hyprland");
    else if (strcmp(de, "sway") == 0) strcpy(de_pkg, "gui-wm/sway");
    else if (strcmp(de, "i3") == 0) strcpy(de_pkg, "x11-wm/i3");
    else if (strcmp(de, "xfce4") == 0) strcpy(de_pkg, "xfce-base/xfce4-meta");
    else if (strcmp(de, "mate") == 0) strcpy(de_pkg, "mate-base/mate");

    char dm_pkg[256] = "", dm_service[64] = "";
    if (strcmp(display_manager, "sddm") == 0 || strcmp(display_manager, "plasma-login") == 0) {
        strcpy(dm_pkg, "x11-misc/sddm"); strcpy(dm_service, "sddm");
    } else if (strcmp(display_manager, "gdm") == 0) {
        strcpy(dm_pkg, "gnome-base/gdm"); strcpy(dm_service, "gdm");
    } else if (strcmp(display_manager, "lightdm-gtk") == 0 || strcmp(display_manager, "lightdm-slick") == 0) {
        strcpy(dm_pkg, "x11-misc/lightdm"); strcpy(dm_service, "display-manager");
    } else if (strcmp(display_manager, "lxdm") == 0) {
        strcpy(dm_pkg, "x11-misc/lxdm"); strcpy(dm_service, "lxdm");
    }

    char audio_pkg[128] = "";
    if (strcmp(audio_server, "pipewire") == 0) strcpy(audio_pkg, "media-video/pipewire media-video/wireplumber");
    else if (strcmp(audio_server, "pulseaudio") == 0) strcpy(audio_pkg, "media-sound/pulseaudio");
    else if (strcmp(audio_server, "alsa") == 0) strcpy(audio_pkg, "media-libs/alsa-lib media-sound/alsa-utils");

    char bt_pkg[64] = "";
    if (strcmp(bluetooth, "bluez") == 0) strcpy(bt_pkg, "net-wireless/bluez");

    char fw_pkg[64] = "";
    if (strcmp(firewall, "ufw") == 0) strcpy(fw_pkg, "net-firewall/ufw");
    else if (strcmp(firewall, "firewalld") == 0) strcpy(fw_pkg, "net-firewall/firewalld");
    else if (strcmp(firewall, "nftables") == 0) strcpy(fw_pkg, "net-firewall/nftables");
    else if (strcmp(firewall, "iptables") == 0) strcpy(fw_pkg, "net-firewall/iptables");

    char kernel_pkg[64] = "";
    if (strcmp(kernel_type, "linux-zen") == 0) strcpy(kernel_pkg, "sys-kernel/zen-sources sys-kernel/genkernel");
    else if (strcmp(kernel_type, "lts") == 0) strcpy(kernel_pkg, "sys-kernel/gentoo-kernel");
    else if (strcmp(kernel_type, "hardened") == 0) strcpy(kernel_pkg, "sys-kernel/hardened-sources sys-kernel/genkernel");
    else strcpy(kernel_pkg, "sys-kernel/gentoo-kernel-bin");

    char global_use[256] = "X wayland opengl dbus gui policykit alsa pulseaudio";
    char init_pkg[128] = "", init_post_cmd[512] = "true";

    if (strcmp(init_sys, "systemd") == 0) {
        strcat(global_use, " systemd -elogind");
    } else if (strcmp(init_sys, "runit") == 0) {
        strcat(global_use, " elogind -systemd");
        strcpy(init_pkg, "sys-process/runit");
        snprintf(init_post_cmd, sizeof(init_post_cmd),
                 "mkdir -p /etc/runit/runsvdir/default/agetty-tty1 && ln -sf /etc/sv/agetty-tty1 /etc/runit/runsvdir/default/");
    } else if (strcmp(init_sys, "dinit") == 0) {
        strcat(global_use, " elogind -systemd");
        strcpy(init_pkg, "sys-process/dinit");
        snprintf(init_post_cmd, sizeof(init_post_cmd),
                 "dinitctl enable agetty-tty1 2>/dev/null || true");
    } else if (strcmp(init_sys, "s6") == 0) {
        strcat(global_use, " elogind -systemd");
        strcpy(init_pkg, "sys-process/s6 sys-process/s6-linux-init");
        snprintf(init_post_cmd, sizeof(init_post_cmd),
                 "s6-linux-init-maker -f /etc/s6-linux-init/current 2>/dev/null || true");
    } else if (strcmp(init_sys, "busybox") == 0) {
        strcat(global_use, " elogind -systemd");
        strcpy(init_pkg, "sys-apps/busybox");
        snprintf(init_post_cmd, sizeof(init_post_cmd),
                 "echo '::respawn:/sbin/getty 38400 tty1' >> /etc/inittab");
    } else {
        strcat(global_use, " elogind -systemd");
    }

    char bootloader_pkg[64] = "", bootloader_install_cmd[256] = "";
    if (strcmp(bootloader, "grub") == 0) {
        strcpy(bootloader_pkg, "sys-boot/grub");
        if (uefi_detected) snprintf(bootloader_install_cmd, sizeof(bootloader_install_cmd),
            "grub-install --target=x86_64-efi --efi-directory=/efi --bootloader-id=Gentoo\ngrub-mkconfig -o /boot/grub/grub.cfg");
        else snprintf(bootloader_install_cmd, sizeof(bootloader_install_cmd),
            "grub-install --target=i386-pc %s\ngrub-mkconfig -o /boot/grub/grub.cfg", disk);
    } else if (strcmp(bootloader, "limine") == 0) {
        strcpy(bootloader_pkg, "sys-boot/limine");
        snprintf(bootloader_install_cmd, sizeof(bootloader_install_cmd), "echo '[+] Limine emerged. Configure /boot/limine.conf manually.'");
    } else if (strcmp(bootloader, "systemd-boot") == 0) {
        strcpy(bootloader_pkg, "");
        snprintf(bootloader_install_cmd, sizeof(bootloader_install_cmd), "bootctl install");
    } else if (strcmp(bootloader, "refind") == 0) {
        strcpy(bootloader_pkg, "sys-boot/refind");
        snprintf(bootloader_install_cmd, sizeof(bootloader_install_cmd), "refind-install");
    } else {
        strcpy(bootloader_pkg, "");
        snprintf(bootloader_install_cmd, sizeof(bootloader_install_cmd), "echo '[+] Skipping automated bootloader deployment.'");
    }

    FILE *chroot_script = fopen("/mnt/gentoo/tmp/setup_chroot.sh", "w");
    if (!chroot_script) { show_error("Failed to generate chroot script."); return 1; }

    fprintf(chroot_script,
            "#!/bin/bash\n"
            "set -e\n"
            "source /etc/profile\n"
            "echo 'ACCEPT_LICENSE=\"*\"' >> /etc/portage/make.conf\n"
            "echo 'USE=\"%s\"' >> /etc/portage/make.conf\n"
            "\n"
            "mkdir -p /etc/portage/package.use\n"
            "echo 'dev-python/pillow -truetype' >> /etc/portage/package.use/installer\n"
            "echo 'sys-process/runit -static' >> /etc/portage/package.use/installer 2>/dev/null || true\n"
            "\n"
            "mkdir -p /var/db/repos/gentoo\n"
            "mkdir -p /etc/portage/repos.conf\n"
            "cp /usr/share/portage/config/repos.conf /etc/portage/repos.conf/gentoo.conf 2>/dev/null || true\n"
            "\n"
            "echo '%s %s %s defaults,noatime 0 1' > /etc/fstab\n"
            "echo '%s %s %s defaults,noatime 1 2' >> /etc/fstab\n"
            "\n"
            "cp /etc/pam.d/chpasswd /etc/pam.d/chpasswd.bak 2>/dev/null || true\n"
            "echo 'password required pam_unix.so sha512 shadow' > /etc/pam.d/chpasswd\n"
            "echo 'root:%s' | chpasswd\n"
            "useradd --badname -m -G wheel,users -s /bin/bash %s 2>/dev/null || true\n"
            "echo '%s:%s' | chpasswd\n"
            "mv /etc/pam.d/chpasswd.bak /etc/pam.d/chpasswd 2>/dev/null || true\n"
            "echo '%%wheel ALL=(ALL:ALL) ALL' >> /etc/sudoers\n"
            "\n"
            "emerge-webrsync\n"
            "\n"
            "PROFILE_NUM=$(eselect profile list | grep -E '%s' | head -n1 | tr -d '[]' | awk '{print $1}')\n"
            "if [ -n \"$PROFILE_NUM\" ]; then eselect profile set $PROFILE_NUM || true; fi\n"
            "\n"
            "install_core_pkgs() {\n"
            "    for i in 1 2 3 4; do\n"
            "        echo \"[+] Emerging core packages (Attempt $i)...\"\n"
            "        if emerge --autounmask-write=y --autounmask-continue --backtrack=30 --verbose \"$@\"; then return 0; fi\n"
            "        yes | etc-update --automode -5 2>/dev/null || true\n"
            "    done\n"
            "    echo '[-] Fatal: Failed to emerge core system packages after multiple attempts!'\n"
            "    exit 1\n"
            "}\n"
            "\n"
            "install_optional_pkgs() {\n"
            "    if [ -z \"$1\" ]; then return 0; fi\n"
            "    for pkg in $@; do\n"
            "        echo \"[+] Emerging package: $pkg...\"\n"
            "        if emerge --autounmask-write=y --autounmask-continue --backtrack=30 --verbose \"$pkg\"; then\n"
            "            echo \"[+] Successfully installed $pkg\"\n"
            "        else\n"
            "            echo \"[!] Skipping unresolvable optional package: '$pkg'\"\n"
            "            yes | etc-update --automode -5 2>/dev/null || true\n"
            "        fi\n"
            "    done\n"
            "}\n"
            "\n"
            "install_core_pkgs %s net-misc/dhcpcd %s %s %s %s %s %s %s\n"
            "\n"
            "install_optional_pkgs %s\n"
            "\n"
            "%s\n"
            "\n"
            "rc-update add dhcpcd default 2>/dev/null || systemctl enable dhcpcd 2>/dev/null || true\n"
            "if [ -n \"%s\" ]; then rc-update add %s default 2>/dev/null || systemctl enable %s 2>/dev/null || true; fi\n"
            "if [ -n \"%s\" ]; then rc-update add bluetooth default 2>/dev/null || systemctl enable bluetooth 2>/dev/null || true; fi\n"
            "if [ -n \"%s\" ]; then rc-update add %s default 2>/dev/null || systemctl enable %s 2>/dev/null || true; fi\n"
            "\n"
            "if [ \"%s\" = \"linux-zen\" ] || [ \"%s\" = \"hardened\" ]; then genkernel all 2>/dev/null || true; fi\n"
            "\n"
            "%s\n"
            "\n"
            "echo '[+] Gentoo Chroot Configuration Completed Successfully!'\n",
            global_use, target_root_dev, "/", fstype, part1, uefi_detected ? "/efi" : "/boot", uefi_detected ? "vfat" : "ext4",
            rootpass, username, username, userpass, strcmp(init_sys, "systemd") == 0 ? "desktop/systemd" : "desktop",
            kernel_pkg, bootloader_pkg, de_pkg, dm_pkg, init_pkg, audio_pkg, bt_pkg, fw_pkg, extra_pkgs,
            init_post_cmd, dm_service, dm_service, dm_service, bt_pkg, fw_pkg, firewall, firewall,
            kernel_type, kernel_type, bootloader_install_cmd
    );
    fclose(chroot_script);

    run_command_with_error_check("chmod +x /mnt/gentoo/tmp/setup_chroot.sh", "Failed to set executable flag on chroot script.");
    run_command_with_error_check("chroot /mnt/gentoo /tmp/setup_chroot.sh", "Chroot automated configuration failed.");
    run_command_with_error_check("rm /mnt/gentoo/tmp/setup_chroot.sh", "Failed to cleanup temporary chroot script.");

    printf("\n===================================================\n");
    printf(" [SUCCESS] Fully bootable Gentoo Linux installed!\n");
    printf(" Unmount /mnt/gentoo and reboot into your new system.\n");
    printf("===================================================\n");

    return 0;
}
