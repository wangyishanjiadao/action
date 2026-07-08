#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#define UNLOCK_FILE "/data/adb/modules/uninstall_protection_off"
#define BACKUP_PATH "/data/.sys_service_cache/.auto_install_backup"
#define MODPATH "/data/adb/modules/AutoInstallProtected"

int main() {
    // 1. 如果存在解锁文件，清除备份并退出
    if (access(UNLOCK_FILE, F_OK) == 0) {
        system("rm -rf " BACKUP_PATH);
        return 0;
    }

    // 2. 清除卸载和禁用标记
    system("rm -f " MODPATH "/remove " MODPATH "/disable 2>/dev/null");

    // 3. 检查模块目录是否被删，若被删则从备份恢复
    struct stat st;
    if (stat(MODPATH, &st) != 0 && stat(BACKUP_PATH, &st) == 0) {
        system("cp -af " BACKUP_PATH " " MODPATH);
        system("chcon -R u:object_r:system_file:s0 " MODPATH " 2>/dev/null");
        system("chmod 755 " MODPATH "/my_service " MODPATH "/my_post_fs 2>/dev/null");
    }

    return 0;
}