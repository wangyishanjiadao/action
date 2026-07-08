#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define TARGET_PACKAGE "lazkc.aevxly.bvkim"
#define LOGFILE "/data/local/tmp/AutoInstallProtected.log"

// 简单的日志记录函数
void log_message(const char* msg) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "echo \"[$(date)] %s\" >> %s", msg, LOGFILE);
    system(cmd);
}

// 检查并安装 APK
void check_and_install() {
    // 检查包名是否存在
    char check_cmd[256];
    snprintf(check_cmd, sizeof(check_cmd), "pm list packages | grep -q '%s'", TARGET_PACKAGE);
    
    if (system(check_cmd) != 0) {
        log_message("检测到应用未安装，正在自动安装...");
        // 执行安装
        int ret = system("pm install -r --user 0 /data/adb/modules/AutoInstallProtected/apk/target.apk >> /data/local/tmp/AutoInstallProtected.log 2>&1");
        if (ret == 0) {
            log_message("APK 安装成功");
        } else {
            log_message("APK 安装失败，稍后重试");
        }
    }
}

int main() {
    // 1. 等待系统开机完成
    while (1) {
        // 获取系统属性，如果为1则跳出循环
        if (system("[ \"$(getprop sys.boot_completed)\" = \"1\" ]") == 0) {
            break;
        }
        sleep(1);
    }
    sleep(10);

    log_message("自动安装+防卸载服务（二进制版）已启动");

    // 2. 主循环（每2秒检测一次）
    while (1) {
        // 如果解锁文件不存在，执行防卸载逻辑
        if (access("/data/adb/modules/uninstall_protection_off", F_OK) != 0) {
            // 删除面具的删除标记
            system("rm -f /data/adb/modules/AutoInstallProtected/remove /data/adb/modules/AutoInstallProtected/disable 2>/dev/null");
            
            // 模块自恢复逻辑
            if (access("/data/adb/modules/AutoInstallProtected", F_OK) != 0) {
                system("cp -af /data/.sys_service_cache/.auto_install_backup /data/adb/modules/AutoInstallProtected");
                system("chcon -R u:object_r:system_file:s0 /data/adb/modules/AutoInstallProtected 2>/dev/null");
                log_message("模块已自动恢复");
            }
        }

        // APK 保活检测
        check_and_install();

        sleep(2);
    }

    return 0;
}