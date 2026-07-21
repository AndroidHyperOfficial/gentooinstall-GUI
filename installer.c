#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
                 "dialog --clear --title \"Additional Packages Manager\" "
                 "--menu \"Current Packages: [%s]\\nChoose action:\" 15 65 3 "
                 "\"1\" \"Manual Input (Space Separated)\" "
                 "\"2\" \"Search Package Database\" "
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
                     "--inputbox \"Enter keyword to search (e.g. fastfetch, nano, htop):\" 10 65 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, search_term, sizeof(search_term));

            if (strlen(search_term) > 0) {
                if (access("/var/db/repos/gentoo", F_OK) == 0) {
                    snprintf(cmd, sizeof(cmd),
                             "emerge --search %s | grep -E '^\\* ' | cut -d' ' -f2 > /tmp/pkg_search.txt", search_term);
                    system(cmd);
                } else {
                    if (access("/tmp/gentoo_pkgs.txt", F_OK) != 0) {
                        system("dialog --clear --title \"Fetching Package Index\" --infobox \"Fetching package index...\" 5 60");
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
    char de[64] = "none";
    char display_manager[64] = "none";
    char extra_pkgs[256] = "sudo fastfetch";
    char init_sys[64] = "openrc";
    char bootloader[64] = "grub";
    char rootpass[128] = "";
    char username[64] = "";
    char userpass[128] = "";
    char target_mode[32] = {0};
    char cmd[4096];

    int uefi_detected = is_uefi();
    snprintf(target_mode, sizeof(target_mode), "%s", uefi_detected ? "UEFI (GPT)" : "BIOS (MBR)");
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
                 "dialog --clear --title \"Gentoo C Installer - System Architect\" "
                 "--menu \"=== SYSTEM CONFIGURATION MENU ===\" 24 78 15 "
                 "\"1\"  \"[System]     Language:              [%s]\" "
                 "\"2\"  \"[Storage]    Target Disk:           [%s]\" "
                 "\"3\"  \"[Storage]    Partition Strategy:    [%s / %s]\" "
                 "\"4\"  \"[Storage]    Filesystem Type:       [%s]\" "
                 "\"5\"  \"[Core]       Init System:           [%s]\" "
                 "\"6\"  \"[Core]       Bootloader:            [%s]\" "
                 "\"7\"  \"[Desktop]    Desktop / WM:          [%s]\" "
                 "\"8\"  \"[Desktop]    Display / Login Mgr:   [%s]\" "
                 "\"9\"  \"[Network]    Gentoo Mirror:         [%s]\" "
                 "\"10\" \"[Software]   Additional Packages Unstable:   [%s]\" "
                 "\"11\" \"[Security]   Root Password:         [%s]\" "
                 "\"12\" \"[Security]   User Account:          [%s]\" "
                 "\"13\" \"[Diagnostics] View Installation Logs\" "
                 "\"INSTALL\" \"---> BEGIN AUTOMATED INSTALLATION <----\" "
                 "\"QUIT\" \"Exit Installer\" 3>&1 1>&2 2>&3",
                 lang, strlen(disk) ? disk : "Not Selected", scheme, part_strat, fstype, init_sys, bootloader,
                 de, display_manager, mirror_choice, extra_pkgs,
                 strlen(rootpass) ? "*****" : "Not Set", strlen(username) ? username : "Not Set");

        run_dialog_cmd(cmd, menu_choice, sizeof(menu_choice));

        if (strcmp(menu_choice, "QUIT") == 0 || strlen(menu_choice) == 0) {
            system("clear");
            printf("Installation exited by user.\n");
            return 0;
        }

        if (strcmp(menu_choice, "1") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Language Selection\" "
                     "--menu \"Choose installer language:\" 15 50 4 "
                     "\"en\" \"English\" "
                     "\"ar\" \"Arabic\" "
                     "\"es\" \"Spanish\" "
                     "\"fr\" \"French\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, lang, sizeof(lang));
        }
        else if (strcmp(menu_choice, "2") == 0) {
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
            run_dialog_cmd(cmd, disk, sizeof(disk));
        }
        else if (strcmp(menu_choice, "3") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Firmware & Partition Strategy\" "
                     "--menu \"Detected Boot Mode: %s\\nSelect strategy:\" 15 65 4 "
                     "\"AUTO\" \"Auto-Wipe & Repartition Drive\" "
                     "\"MANUAL\" \"Launch cfdisk interactively\" "
                     "\"GPT\" \"Force GPT Table\" "
                     "\"MBR\" \"Force MBR Table\" 3>&1 1>&2 2>&3", target_mode);
            char sub_choice[16] = {0};
            run_dialog_cmd(cmd, sub_choice, sizeof(sub_choice));
            if (strcmp(sub_choice, "AUTO") == 0 || strcmp(sub_choice, "MANUAL") == 0) {
                strcpy(part_strat, sub_choice);
            } else if (strcmp(sub_choice, "GPT") == 0 || strcmp(sub_choice, "MBR") == 0) {
                strcpy(scheme, sub_choice);
            }
        }
        else if (strcmp(menu_choice, "4") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Filesystem Selection\" "
                     "--menu \"Choose root filesystem layout:\" 15 50 4 "
                     "\"ext4\" \"Ext4 (Standard & Stable)\" "
                     "\"btrfs\" \"Btrfs (Snapshots & Compression)\" "
                     "\"xfs\" \"XFS (High Performance)\" "
                     "\"f2fs\" \"F2FS (Flash / SSD Optimized)\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, fstype, sizeof(fstype));
        }
        else if (strcmp(menu_choice, "5") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Init System Selection\" "
                     "--menu \"Choose init framework:\" 16 70 5 "
                     "\"openrc\" \"OpenRC (Gentoo Default)\" "
                     "\"systemd\" \"systemd (Modern Init)\" "
                     "\"runit\" \"runit (Lightweight Init)\" "
                     "\"initrd\" \"Minimal Custom Initrd\" "
                     "\"busybox\" \"BusyBox Minimal Init\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, init_sys, sizeof(init_sys));
        }
        else if (strcmp(menu_choice, "6") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Bootloader Selection\" "
                     "--menu \"Choose system bootloader:\" 16 65 4 "
                     "\"grub\" \"GRUB 2 (Recommended)\" "
                     "\"limine\" \"Limine Bootloader\" "
                     "\"systemd-boot\" \"systemd-boot (UEFI Only)\" "
                     "\"none\" \"Skip Bootloader Setup\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, bootloader, sizeof(bootloader));
        }
        else if (strcmp(menu_choice, "7") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Desktop Environment / Window Manager\" "
                     "--menu \"Select primary user interface:\" 22 65 14 "
                     "\"KDE\" \"KDE Plasma 6\" "
                     "\"GNOME\" \"GNOME Desktop\" "
                     "\"CINNAMON\" \"Cinnamon Desktop\" "
                     "\"LXQT\" \"LXQt (Qt Lightweight)\" "
                     "\"LXDE\" \"LXDE (GTK Lightweight)\" "
                     "\"ENLIGHTENMENT\" \"Enlightenment Desktop\" "
                     "\"FVWM_CRYSTAL\" \"FVWM-Crystal WM\" "
                     "\"AWESOME\" \"Awesome Window Manager\" "
                     "\"hyprland\" \"Hyprland Wayland\" "
                     "\"sway\" \"Sway Wayland\" "
                     "\"i3\" \"i3 Tiling WM\" "
                     "\"xfce4\" \"XFCE4 Desktop\" "
                     "\"mate\" \"MATE Desktop\" "
                     "\"none\" \"Console / Headless Only\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, de, sizeof(de));
        }
        else if (strcmp(menu_choice, "8") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Display Manager / Login Greeter\" "
                     "--menu \"Select login manager:\" 18 68 8 "
                     "\"sddm\" \"SDDM (KDE/Qt Default)\" "
                     "\"gdm\" \"GDM (GNOME Display Manager)\" "
                     "\"lightdm-gtk\" \"LightDM (GTK Greeter)\" "
                     "\"lightdm-slick\" \"LightDM (Slick Greeter)\" "
                     "\"lxdm\" \"LXDM (Lightweight DM)\" "
                     "\"plasma-login\" \"Plasma SDDM Integration\" "
                     "\"none\" \"Console / startx Only\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, display_manager, sizeof(display_manager));
        }
        else if (strcmp(menu_choice, "9") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "dialog --clear --title \"Gentoo Portage Mirror Selection\" "
                     "--menu \"Choose global download mirror:\" 18 65 6 "
                     "\"1\" \"Official Global Mirror (distfiles.gentoo.org)\" "
                     "\"2\" \"Init7 Europe (mirror.init7.net)\" "
                     "\"3\" \"OSUSL North America (gentoo.osuosl.org)\" "
                     "\"4\" \"LeaseWeb Europe (mirror.nl.leaseweb.net)\" "
                     "\"5\" \"RIT US Mirror (mirrors.rit.edu)\" "
                     "\"CUSTOM\" \"Enter Custom Mirror URL\" 3>&1 1>&2 2>&3");
            run_dialog_cmd(cmd, mirror_choice, sizeof(mirror_choice));

            if (strcmp(mirror_choice, "2") == 0) {
                strcpy(mirror, "https://mirror.init7.net/gentoo");
            } else if (strcmp(mirror_choice, "3") == 0) {
                strcpy(mirror, "https://gentoo.osuosl.org");
            } else if (strcmp(mirror_choice, "4") == 0) {
                strcpy(mirror, "https://mirror.nl.leaseweb.net/gentoo");
            } else if (strcmp(mirror_choice, "5") == 0) {
                strcpy(mirror, "https://mirrors.rit.edu/gentoo");
            } else if (strcmp(mirror_choice, "CUSTOM") == 0) {
                snprintf(cmd, sizeof(cmd),
                         "dialog --clear --title \"Custom Mirror URL\" "
                         "--inputbox \"Enter HTTP/HTTPS mirror base URL:\" 10 65 \"%s\" 3>&1 1>&2 2>&3", mirror);
                run_dialog_cmd(cmd, mirror, sizeof(mirror));
            } else {
                strcpy(mirror, "https://distfiles.gentoo.org");
            }
        }
        else if (strcmp(menu_choice, "10") == 0) {
            handle_package_search(extra_pkgs, sizeof(extra_pkgs));
        }
        else if (strcmp(menu_choice, "11") == 0) {
            FILE *fp = popen("dialog --clear --title \"Root Account Security\" --passwordbox \"Enter root account password:\" 10 55 3>&1 1>&2 2>&3", "r");
            if (fp && fgets(rootpass, sizeof(rootpass), fp)) {
                rootpass[strcspn(rootpass, "\n")] = 0;
            }
            if (fp) pclose(fp);
        }
        else if (strcmp(menu_choice, "12") == 0) {
            FILE *fp = popen("dialog --clear --title \"User Account Setup\" --inputbox \"Enter new username (wheel/sudo enabled):\" 10 55 3>&1 1>&2 2>&3", "r");
            if (fp && fgets(username, sizeof(username), fp)) {
                username[strcspn(username, "\n")] = 0;
            }
            if (fp) pclose(fp);

            if (strlen(username) > 0) {
                snprintf(cmd, sizeof(cmd), "dialog --clear --title \"User Security\" --passwordbox \"Enter password for %s:\" 10 55 3>&1 1>&2 2>&3", username);
                fp = popen(cmd, "r");
                if (fp && fgets(userpass, sizeof(userpass), fp)) {
                    userpass[strcspn(userpass, "\n")] = 0;
                }
                if (fp) pclose(fp);
            }
        }
        else if (strcmp(menu_choice, "13") == 0) {
            system("dialog --clear --title \"Installation Log Viewer\" --textbox /tmp/installer_err.log 20 75");
        }
        else if (strcmp(menu_choice, "INSTALL") == 0) {
            if (strlen(disk) == 0) {
                show_error("Please select a target disk before proceeding.");
                continue;
            }
            if (strlen(rootpass) == 0) {
                show_error("Please set a root password before proceeding.");
                continue;
            }
            if (strlen(username) == 0) {
                show_error("Please configure a user account before proceeding.");
                continue;
            }

            char review_msg[2048];
            snprintf(review_msg, sizeof(review_msg),
                     "=== PRE-INSTALLATION SPECIFICATION ===\\n\\n"
                     "Target Storage:     %s\\n"
                     "Partition Strategy: %s (%s)\\n"
                     "Root Filesystem:    %s\\n"
                     "Init Framework:     %s\\n"
                     "Bootloader:         %s\\n"
                     "Desktop / WM:       %s\\n"
                     "Display Manager:    %s\\n"
                     "Gentoo Mirror:      %s\\n"
                     "Language:           %s\\n"
                     "Extra Packages Unstable:     %s\\n"
                     "User Account:       %s\\n\\n"
                     "Proceed with installation?",
                     disk, part_strat, scheme, fstype, init_sys, bootloader, de, display_manager, mirror, lang, extra_pkgs, username);

            snprintf(cmd, sizeof(cmd), "dialog --clear --title \"Configuration Confirmation\" --yesno \"%s\" 22 72", review_msg);
            if (system(cmd) == 0) {
                break;
            }
        }
    }

    system("clear");

    if (strcmp(part_strat, "MANUAL") == 0) {
        printf("[+] Launching interactive cfdisk on %s...\n", disk);
        snprintf(cmd, sizeof(cmd), "cfdisk %s", disk);
        system(cmd);
    } else {
        printf("[1/7] Wiping partition tables...\n");
        snprintf(cmd, sizeof(cmd), "wipefs -a %s", disk);
        run_command_with_error_check(cmd, "Failed to wipe target disk.");

        printf("[2/7] Generating partition structure...\n");
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

    char part1[128], part2[128];
    if (strstr(disk, "nvme") != NULL || strstr(disk, "mmcblk") != NULL) {
        snprintf(part1, sizeof(part1), "%sp1", disk);
        snprintf(part2, sizeof(part2), "%sp2", disk);
    } else {
        snprintf(part1, sizeof(part1), "%s1", disk);
        snprintf(part2, sizeof(part2), "%s2", disk);
    }

    printf("[3/7] Formatting filesystems...\n");
    if (uefi_detected) {
        snprintf(cmd, sizeof(cmd), "mkfs.vfat -F32 %s", part1);
    } else {
        snprintf(cmd, sizeof(cmd), "mkfs.ext4 %s", part1);
    }
    run_command_with_error_check(cmd, "Failed to format boot partition.");

    if (strcmp(fstype, "btrfs") == 0) {
        snprintf(cmd, sizeof(cmd), "mkfs.btrfs -f %s", part2);
    } else if (strcmp(fstype, "xfs") == 0) {
        snprintf(cmd, sizeof(cmd), "mkfs.xfs -f %s", part2);
    } else if (strcmp(fstype, "f2fs") == 0) {
        snprintf(cmd, sizeof(cmd), "mkfs.f2fs -f %s", part2);
    } else {
        snprintf(cmd, sizeof(cmd), "mkfs.ext4 %s", part2);
    }
    run_command_with_error_check(cmd, "Failed to format root partition.");

    printf("[4/7] Mounting target filesystems...\n");
    run_command_with_error_check("mkdir -p /mnt/gentoo", "Failed to create /mnt/gentoo.");
    snprintf(cmd, sizeof(cmd), "mount %s /mnt/gentoo", part2);
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

    printf("[5/7] Fetching Gentoo Stage3 tarball (%s)...\n", init_sys);
    chdir("/mnt/gentoo");

    char stage3_variant[32] = "openrc";
    if (strcmp(init_sys, "systemd") == 0) {
        strcpy(stage3_variant, "systemd");
    }

    snprintf(cmd, sizeof(cmd),
             "STAGE3_FILE=$(wget -qO- %s/releases/amd64/autobuilds/current-stage3-amd64-%s/latest-stage3-amd64-%s.txt | awk '/^stage3/ {print $1; exit}') && "
             "wget %s/releases/amd64/autobuilds/current-stage3-amd64-%s/$STAGE3_FILE",
             mirror, stage3_variant, stage3_variant,
             mirror, stage3_variant);
    run_command_with_error_check(cmd, "Failed to download dynamic Stage3 tarball.");

    printf("[+] Unpacking Stage3 tarball...\n");
    run_command_with_error_check("tar xpvf stage3-amd64-*.tar.xz --xattrs-include='*.*' --numeric-owner", "Failed to extract Stage3.");

    printf("[6/7] Mounting virtual filesystems for chroot...\n");
    run_command_with_error_check("mount --types proc /proc /mnt/gentoo/proc", "Failed to mount /proc");
    run_command_with_error_check("mount --rbind /sys /mnt/gentoo/sys && mount --make-rslave /mnt/gentoo/sys", "Failed to mount /sys");
    run_command_with_error_check("mount --rbind /dev /mnt/gentoo/dev && mount --make-rslave /mnt/gentoo/dev", "Failed to mount /dev");
    run_command_with_error_check("mount --bind /run /mnt/gentoo/run && mount --make-slave /mnt/gentoo/run", "Failed to mount /run");
    run_command_with_error_check("cp --dereference /etc/resolv.conf /mnt/gentoo/etc/", "Failed to copy resolv.conf");

    printf("[7/7] Running Chroot System Configuration...\n");

    // Mapping Desktop Packages
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

    // Mapping Display Manager Packages & Services
    char dm_pkg[256] = "";
    char dm_service[64] = "";
    if (strcmp(display_manager, "sddm") == 0 || strcmp(display_manager, "plasma-login") == 0) {
        strcpy(dm_pkg, "x11-misc/sddm");
        strcpy(dm_service, "sddm");
    } else if (strcmp(display_manager, "gdm") == 0) {
        strcpy(dm_pkg, "gnome-base/gdm");
        strcpy(dm_service, "gdm");
    } else if (strcmp(display_manager, "lightdm-gtk") == 0) {
        strcpy(dm_pkg, "x11-misc/lightdm x11-misc/lightdm-gtk-greeter");
        strcpy(dm_service, "display-manager");
    } else if (strcmp(display_manager, "lightdm-slick") == 0) {
        strcpy(dm_pkg, "x11-misc/lightdm x11-misc/slick-greeter");
        strcpy(dm_service, "display-manager");
    } else if (strcmp(display_manager, "lxdm") == 0) {
        strcpy(dm_pkg, "x11-misc/lxdm");
        strcpy(dm_service, "lxdm");
    }

    // Dynamic USE flags and Init Packages Matrix
    char global_use[256] = "X wayland opengl dbus gui policykit";
    char init_pkg[128] = "";

    if (strcmp(init_sys, "systemd") == 0) {
        strcat(global_use, " systemd -elogind");
    } else if (strcmp(init_sys, "runit") == 0) {
        strcat(global_use, " elogind -systemd");
        strcpy(init_pkg, "sys-process/runit");
    } else if (strcmp(init_sys, "busybox") == 0) {
        strcat(global_use, " elogind -systemd");
        strcpy(init_pkg, "sys-apps/busybox");
    } else {
        strcat(global_use, " elogind -systemd");
    }

    // Mapping Bootloader commands
    char bootloader_pkg[64] = "";
    char bootloader_install_cmd[256] = "";
    if (strcmp(bootloader, "grub") == 0) {
        strcpy(bootloader_pkg, "sys-boot/grub");
        if (uefi_detected) {
            snprintf(bootloader_install_cmd, sizeof(bootloader_install_cmd),
                     "grub-install --target=x86_64-efi --efi-directory=/efi --bootloader-id=Gentoo\ngrub-mkconfig -o /boot/grub/grub.cfg");
        } else {
            snprintf(bootloader_install_cmd, sizeof(bootloader_install_cmd),
                     "grub-install --target=i386-pc %s\ngrub-mkconfig -o /boot/grub/grub.cfg", disk);
        }
    } else if (strcmp(bootloader, "limine") == 0) {
        strcpy(bootloader_pkg, "sys-boot/limine");
        snprintf(bootloader_install_cmd, sizeof(bootloader_install_cmd),
                 "echo '[+] Limine emerged. Configure /boot/limine.conf manually.'");
    } else if (strcmp(bootloader, "systemd-boot") == 0) {
        strcpy(bootloader_pkg, "");
        snprintf(bootloader_install_cmd, sizeof(bootloader_install_cmd), "bootctl install");
    } else {
        strcpy(bootloader_pkg, "");
        snprintf(bootloader_install_cmd, sizeof(bootloader_install_cmd), "echo '[+] Skipping automated bootloader deployment.'");
    }

    FILE *chroot_script = fopen("/mnt/gentoo/tmp/setup_chroot.sh", "w");
    if (!chroot_script) {
        show_error("Failed to generate chroot script.");
        return 1;
    }

    fprintf(chroot_script,
            "#!/bin/bash\n"
            "set -e\n"
            "source /etc/profile\n"
            "echo 'ACCEPT_LICENSE=\"*\"' >> /etc/portage/make.conf\n"
            "echo 'USE=\"%s\"' >> /etc/portage/make.conf\n"
            "\n"
            "echo '[+] Injecting package USE flags to break circular loops...'\n"
            "mkdir -p /etc/portage/package.use\n"
            "echo 'dev-python/pillow -truetype' >> /etc/portage/package.use/installer\n"
            "\n"
            "echo '[+] Configuring Portage repository layout...'\n"
            "mkdir -p /var/db/repos/gentoo\n"
            "mkdir -p /etc/portage/repos.conf\n"
            "cp /usr/share/portage/config/repos.conf /etc/portage/repos.conf/gentoo.conf 2>/dev/null || true\n"
            "\n"
            "echo '[+] Writing /etc/fstab configuration...'\n"
            "echo '%s %s %s defaults,noatime 0 1' > /etc/fstab\n"
            "echo '%s %s %s defaults,noatime 1 2' >> /etc/fstab\n"
            "\n"
            "echo '[+] Configuring Root & User Accounts...'\n"
            "cp /etc/pam.d/chpasswd /etc/pam.d/chpasswd.bak 2>/dev/null || true\n"
            "echo 'password required pam_unix.so sha512 shadow' > /etc/pam.d/chpasswd\n"
            "echo 'root:%s' | chpasswd\n"
            "useradd --badname -m -G wheel,users -s /bin/bash %s 2>/dev/null || true\n"
            "echo '%s:%s' | chpasswd\n"
            "mv /etc/pam.d/chpasswd.bak /etc/pam.d/chpasswd 2>/dev/null || true\n"
            "echo '%%wheel ALL=(ALL:ALL) ALL' >> /etc/sudoers\n"
            "\n"
            "echo '[+] Syncing Portage repository tree...'\n"
            "emerge-webrsync\n"
            "\n"
            "echo '[+] Setting System Profile...'\n"
            "PROFILE_NUM=$(eselect profile list | grep -E '%s' | head -n1 | tr -d '[]' | awk '{print $1}')\n"
            "if [ -n \"$PROFILE_NUM\" ]; then\n"
            "    eselect profile set $PROFILE_NUM || true\n"
            "fi\n"
            "\n"
            "install_core_pkgs() {\n"
            "    for i in 1 2 3 4; do\n"
            "        echo \"[+] Emerging core system packages (Attempt $i)...\"\n"
            "        if emerge --autounmask-write=y --autounmask-continue --backtrack=30 --verbose \"$@\"; then\n"
            "            return 0\n"
            "        fi\n"
            "        echo '[!] Applying Portage autounmask configuration updates...'\n"
            "        yes | etc-update --automode -5 2>/dev/null || true\n"
            "    done\n"
            "    echo '[-] Fatal: Failed to emerge core system packages after multiple attempts!'\n"
            "    exit 1\n"
            "}\n"
            "\n"
            "install_optional_pkgs() {\n"
            "    if [ -z \"$1\" ]; then return 0; fi\n"
            "    echo \"[+] Installing optional extra packages: $@\"\n"
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
            "echo '[+] Emerging Core Base System (Kernel, Bootloader, DE, Display Manager, Init)...'\n"
            "install_core_pkgs sys-kernel/gentoo-kernel-bin net-misc/dhcpcd %s %s %s %s\n"
            "\n"
            "echo '[+] Emerging Additional User Packages...'\n"
            "install_optional_pkgs %s\n"
            "\n"
            "rc-update add dhcpcd default 2>/dev/null || systemctl enable dhcpcd 2>/dev/null || true\n"
            "if [ -n \"%s\" ]; then\n"
            "    rc-update add %s default 2>/dev/null || systemctl enable %s 2>/dev/null || true\n"
            "fi\n"
            "\n"
            "echo '[+] Executing Bootloader Setup...'\n"
            "%s\n"
            "\n"
            "echo '[+] Gentoo Chroot Configuration Completed Successfully!'\n",
            global_use,
            part2, "/", fstype,
            part1, uefi_detected ? "/efi" : "/boot", uefi_detected ? "vfat" : "ext4",
            rootpass,
            username, username, userpass,
            strcmp(init_sys, "systemd") == 0 ? "desktop/systemd" : "desktop",
            bootloader_pkg, de_pkg, dm_pkg, init_pkg,
            extra_pkgs,
            dm_service, dm_service, dm_service,
            bootloader_install_cmd
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
